#include "hub_standby.h"

#include "hub_ui.h"
#include "hub_model.h"
#include "hub_device_ui.h"
#include "hub_i18n.h"

#include "withthewind_board_lvgl_init.h"
#include "nvs.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "lvgl.h"

#include <stdio.h>

static const char *TAG = "hub_standby";

#define HUB_STBY_NS "hub_ui"
#define HUB_STBY_KEY_EN "stby_en"
#define HUB_STBY_KEY_TMO "stby_tmo"

static const uint32_t s_allowed[] = {15u, 30u, 60u, 120u, 300u, 600u};
static bool s_en = true;
static uint32_t s_idle_sec = 120u;
static lv_timer_t *s_idle_tmr;
static int s_saved_bright = -1;
static bool s_dimmed;

static bool idle_valid(uint32_t s)
{
    for (unsigned i = 0; i < sizeof(s_allowed) / sizeof(s_allowed[0]); i++) {
        if (s_allowed[i] == s) {
            return true;
        }
    }
    return false;
}

static void load_nvs(void)
{
    s_en = true;
#if defined(CONFIG_UI_AMBIENT_TIMEOUT_SEC)
    s_idle_sec = (uint32_t)CONFIG_UI_AMBIENT_TIMEOUT_SEC;
#else
    s_idle_sec = 120u;
#endif
    if (!idle_valid(s_idle_sec)) {
        s_idle_sec = 120u;
    }

    nvs_handle_t h;
    if (nvs_open(HUB_STBY_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t en = 1;
    if (nvs_get_u8(h, HUB_STBY_KEY_EN, &en) == ESP_OK) {
        s_en = (en != 0);
    }
    uint32_t t = 0;
    if (nvs_get_u32(h, HUB_STBY_KEY_TMO, &t) == ESP_OK && idle_valid(t)) {
        s_idle_sec = t;
    }
    nvs_close(h);
}

static void save_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(HUB_STBY_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    (void)nvs_set_u8(h, HUB_STBY_KEY_EN, s_en ? 1 : 0);
    (void)nvs_set_u32(h, HUB_STBY_KEY_TMO, s_idle_sec);
    (void)nvs_commit(h);
    nvs_close(h);
}

static int ambient_bl_pct(void)
{
#if defined(CONFIG_UI_AMBIENT_BACKLIGHT_PCT)
    return board_backlight_clamp_percent(CONFIG_UI_AMBIENT_BACKLIGHT_PCT);
#else
    return board_backlight_clamp_percent(30);
#endif
}

static void enter_standby(void)
{
    if (hub_ui_route() == HUB_ROUTE_STANDBY) {
        return;
    }
    if (!s_dimmed) {
        s_saved_bright = hub_device_brightness_get();
        s_dimmed = true;
        (void)board_backlight_set(board_backlight_quantize_percent(ambient_bl_pct()));
    }
    ESP_LOGI(TAG, "enter low-power standby (idle=%lus)", (unsigned long)s_idle_sec);
    hub_ui_go(HUB_ROUTE_STANDBY);
}

void hub_standby_on_exit_route(void)
{
    if (!s_dimmed) {
        return;
    }
    s_dimmed = false;
    int b = s_saved_bright >= 0 ? s_saved_bright : hub_device_brightness_get();
    s_saved_bright = -1;
    hub_model_t *m = hub_model();
    if (m && m->settings.night_mode) {
        b = (b * 70) / 100;
    }
    b = board_backlight_clamp_percent(b);
    (void)board_backlight_set(board_backlight_quantize_percent(b));
}

static void idle_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_en) {
        return;
    }
    if (hub_ui_route() == HUB_ROUTE_STANDBY) {
        return;
    }
    lv_disp_t *d = lv_disp_get_default();
    if (!d) {
        return;
    }
    const uint32_t need_ms = s_idle_sec * 1000u;
    if (lv_disp_get_inactive_time(d) < need_ms) {
        return;
    }
    enter_standby();
}

void hub_standby_init(void)
{
    load_nvs();
    if (s_idle_tmr) {
        return;
    }
    s_idle_tmr = lv_timer_create(idle_timer_cb, 1000, NULL);
    if (s_idle_tmr) {
        lv_timer_set_repeat_count(s_idle_tmr, -1);
    }
    ESP_LOGI(TAG, "init en=%d idle=%lus", s_en ? 1 : 0, (unsigned long)s_idle_sec);
}

bool hub_standby_enabled(void)
{
    return s_en;
}

void hub_standby_set_enabled(bool on)
{
    s_en = on;
    save_nvs();
    hub_model_toast(on ? hub_tr("低功耗待机已开启", "Standby on")
                       : hub_tr("低功耗待机已关闭", "Standby off"));
}

uint32_t hub_standby_idle_sec(void)
{
    return s_idle_sec;
}

void hub_standby_set_idle_sec(uint32_t sec)
{
    if (!idle_valid(sec)) {
        sec = 120u;
    }
    s_idle_sec = sec;
    save_nvs();
}

const char *hub_standby_idle_label(void)
{
    switch (s_idle_sec) {
    case 15:
        return hub_tr("15 秒", "15 sec");
    case 30:
        return hub_tr("30 秒", "30 sec");
    case 60:
        return hub_tr("1 分钟", "1 min");
    case 120:
        return hub_tr("2 分钟", "2 min");
    case 300:
        return hub_tr("5 分钟", "5 min");
    case 600:
        return hub_tr("10 分钟", "10 min");
    default:
        return hub_tr("2 分钟", "2 min");
    }
}

const char *hub_standby_idle_options(void)
{
    return hub_tr("15 秒\n30 秒\n1 分钟\n2 分钟\n5 分钟\n10 分钟",
                  "15 sec\n30 sec\n1 min\n2 min\n5 min\n10 min");
}

int hub_standby_idle_index(void)
{
    for (unsigned i = 0; i < sizeof(s_allowed) / sizeof(s_allowed[0]); i++) {
        if (s_allowed[i] == s_idle_sec) {
            return (int)i;
        }
    }
    return 3; /* 120s */
}

void hub_standby_set_idle_index(int idx)
{
    if (idx < 0 || (unsigned)idx >= sizeof(s_allowed) / sizeof(s_allowed[0])) {
        idx = 3;
    }
    hub_standby_set_idle_sec(s_allowed[idx]);
}
