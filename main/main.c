#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_memory_utils.h"
#include "esp_task_wdt.h"
#include "lvgl.h"
#include "app_ui.h"
#include "gui_task.h"
#include "lvgl_rt_tuning.h"
#include "ui_bg_task.h"
#include "wifi_management.h"
#include "bt_management.h"
#include "esp_lvgl_port.h"

#include "withthewind_board_lvgl_init.h"
#include "board_ethernet_ch390.h"
#include "app_features.h"
#include "sdkconfig.h"
#if defined(CONFIG_APP_USE_SPIFFS_UI_ASSETS) && CONFIG_APP_USE_SPIFFS_UI_ASSETS
#include "app_spiffs.h"
#endif
#include "ui_assets_warm.h"
#include "app_low_power.h"
#include "app_health.h"
#include "app_beep.h"
#include "app_coexist.h"
#include "app_modbus_map.h"
#if defined(CONFIG_APP_ENABLE_RS485) && CONFIG_APP_ENABLE_RS485
#include "app_rs485.h"
#include "app_rs485_bench.h"
#include "app_bus_bridge.h"
#endif

static lv_timer_t *s_refr_timer_paused;
static esp_task_wdt_config_t s_wdt_cfg_saved;
static bool s_wdt_cfg_saved_valid;

/*
 * First full refresh + PNG decode from SPIFFS can block taskLVGL for >15s.
 * Pause refr timer until ui_init completes; relax TWDT idle check on CPU1 (LVGL core).
 */
static void app_lvgl_first_refr_then_backlight(void *user_data)
{
    (void)user_data;

    lv_disp_t *disp = lv_disp_get_default();
    if (disp != NULL) {
        lv_obj_t *scr = lv_scr_act();
        if (scr != NULL) {
            lv_obj_invalidate(scr);
        }
        lv_refr_now(disp);
    }

    if (s_refr_timer_paused != NULL) {
        lv_timer_resume(s_refr_timer_paused);
        s_refr_timer_paused = NULL;
    }

    (void)board_backlight_on();

#if defined(CONFIG_APP_USE_SPIFFS_UI_ASSETS) && CONFIG_APP_USE_SPIFFS_UI_ASSETS
    ui_assets_warm_spiffs_png_cache();
#endif

    if (s_wdt_cfg_saved_valid) {
        (void)esp_task_wdt_reconfigure(&s_wdt_cfg_saved);
        s_wdt_cfg_saved_valid = false;
    }
}

static void app_wdt_relax_for_lvgl_init(void)
{
#if CONFIG_ESP_TASK_WDT_EN && CONFIG_ESP_TASK_WDT_INIT
    s_wdt_cfg_saved = (esp_task_wdt_config_t) {
        .timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000,
        .idle_core_mask = 0,
        .trigger_panic = false,
    };
#if CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
    s_wdt_cfg_saved.idle_core_mask |= (1 << 0);
#endif
#if CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
    s_wdt_cfg_saved.idle_core_mask |= (1 << 1);
#endif

    esp_task_wdt_config_t relaxed = s_wdt_cfg_saved;
    relaxed.timeout_ms = 60000;
    /* taskLVGL runs on Core 1; long PNG decode starves IDLE1 — do not watch it here. */
    relaxed.idle_core_mask &= ~(1 << 1);

    if (esp_task_wdt_reconfigure(&relaxed) == ESP_OK) {
        s_wdt_cfg_saved_valid = true;
    }
#else
    s_wdt_cfg_saved_valid = false;
#endif
}

void app_main(void)
{
    ESP_ERROR_CHECK(wifi_management_foundation_init());
    ESP_ERROR_CHECK(app_low_power_init());
#if APP_FEATURE_BLE || APP_FEATURE_MESH
    ESP_ERROR_CHECK(bt_management_init(&(bt_mgmt_config_t){
        .enabled_ble = APP_FEATURE_BLE,
        .enabled_bredr = false, /* ESP32-S3 has no Classic BT */
        .enabled_mesh = APP_FEATURE_MESH,
        .enable_pairing_ui = false,
        .single_controller_lock = true,
        .ble_mtu = 185,
        .device_name = "lvglframe",
    }));
#endif

#if defined(CONFIG_APP_USE_SPIFFS_UI_ASSETS) && CONFIG_APP_USE_SPIFFS_UI_ASSETS
    ESP_ERROR_CHECK(app_spiffs_mount());
#endif

    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_rt_fill_port_cfg(&lvgl_cfg);

    /* Display owns I2C1 + NCA9555 first; CH390 CS/RST share the same expander. */
    ESP_ERROR_CHECK(board_display_start_with_lvgl_cfg(&lvgl_cfg));
    ESP_ERROR_CHECK(lvgl_rt_start_hw_tick_1ms());

    {
        esp_err_t eth_err = board_ethernet_ch390_init();
        if (eth_err != ESP_OK && eth_err != ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW("app", "board_ethernet_ch390_init: %s", esp_err_to_name(eth_err));
        } else if (board_ethernet_ch390_is_ready()) {
            esp_err_t start_err = board_ethernet_ch390_try_start();
            if (start_err != ESP_OK) {
                ESP_LOGW("app", "board_ethernet_ch390_try_start: %s", esp_err_to_name(start_err));
            }
        }
    }

    /* PWM init only; channel starts at 0% until first frame is rendered (see board_backlight_init). */
    (void)board_backlight_init();

#if defined(CONFIG_APP_ENABLE_RS485) && CONFIG_APP_ENABLE_RS485 && \
    defined(CONFIG_APP_RS485_AUTO_INIT) && CONFIG_APP_RS485_AUTO_INIT
    {
        esp_err_t rs_err = app_rs485_init(NULL);
        if (rs_err != ESP_OK) {
            ESP_LOGW("app", "app_rs485_init: %s", esp_err_to_name(rs_err));
        }
    }
#endif

    lvgl_port_lock(0);

    lv_disp_t *disp = lv_disp_get_default();
    s_refr_timer_paused = NULL;
    if (disp != NULL) {
        /* Board init already disabled invalidation once; do not decrement inv_en_cnt again. */
        s_refr_timer_paused = _lv_disp_get_refr_timer(disp);
        if (s_refr_timer_paused != NULL) {
            lv_timer_pause(s_refr_timer_paused);
        }
    }

    app_wdt_relax_for_lvgl_init();

    app_ui_start();

    if (disp != NULL) {
        lv_disp_enable_invalidation(disp, true);
        lv_obj_invalidate(lv_scr_act());
    }

    /* Single full refresh on LVGL task after ui_init (PNG decode stays off IDLE1 WDT path). */
    if (lv_async_call(app_lvgl_first_refr_then_backlight, NULL) != LV_RES_OK) {
        ESP_LOGW("app", "lv_async_call(first_refr) failed; painting synchronously");
        app_lvgl_first_refr_then_backlight(NULL);
    }

    lvgl_port_unlock();

    gui_task_init();
    ui_bg_task_init();
    (void)app_modbus_map_init();
#if defined(CONFIG_APP_ENABLE_RS485) && CONFIG_APP_ENABLE_RS485
    {
        esp_err_t br_err = app_bus_bridge_start();
        if (br_err != ESP_OK && br_err != ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW("app", "app_bus_bridge_start: %s", esp_err_to_name(br_err));
        }
        /* Quiet logs + disable background poll when CONFIG_APP_RS485_BENCH_MODE */
        app_rs485_bench_apply();
    }
#endif
    (void)app_health_monitor_start();
    ESP_ERROR_CHECK(wifi_management_start());
#if APP_FEATURE_BLE || APP_FEATURE_MESH
    {
        esp_err_t bt_err = bt_management_start();
        if (bt_err != ESP_OK) {
            /* Allow running without BT enabled in sdkconfig */
            ESP_LOGW("app", "bt_management_start skipped: %s", esp_err_to_name(bt_err));
        }
    }
#endif
}
