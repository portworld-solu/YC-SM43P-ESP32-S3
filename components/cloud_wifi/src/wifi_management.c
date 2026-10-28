#include "wifi_management.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "sdkconfig.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"

#if defined(CONFIG_ESP_COEX_SW_COEXIST_ENABLE) && CONFIG_ESP_COEX_SW_COEXIST_ENABLE
#include "esp_coexist.h"
#endif

#include "gui_task.h"
#include "ui.h"
#include "wifi_ui_font.h"
#include "ui_runtime.h"
#include "wifi_bemfa_client.h"
#include "app_features.h"

#define WIFI_SCAN_MAX_AP            20
#define WIFI_SCAN_OPTIONS_BUF_SIZE  768
/* 未连接/认证失败：周期性扫 AP 更新下拉框，间隔不宜过短以免徒增射频耗电。 */
#define WIFI_SCAN_PERIOD_IDLE_MS    60000u
/* 已连接等：不做周期扫描，仅阻塞等待通知；用较长周期兜底唤醒（状态机边缘情况）。 */
#define WIFI_SCAN_PERIOD_QUIET_MS   (10u * 60u * 1000u)
#define WIFI_RECONNECT_BASE_MS      2000
#define WIFI_RECONNECT_MAX_ATTEMPTS 8
/* After burst retries fail, cool down then try again (do not give up forever). */
#define WIFI_RECONNECT_COOLDOWN_MS  60000u
#define WIFI_CONNECTED_BIT          BIT0

/* esp_wifi_scan_get_ap_records() needs N×wifi_ap_record_t; ~5–6KB for N=20 — must not live on
 * wifi_scan_task stack (4096) or FreeRTOS reports stack overflow. */
static wifi_ap_record_t s_wifi_scan_ap_buf[WIFI_SCAN_MAX_AP];

static const char *TAG = "wifi_mgmt";
static bool s_wifi_started = false;
static bool s_wifi_driver_inited = false;
static bool s_wifi_start_failed = false;
static bool s_background_scan = true;
static esp_event_handler_instance_t s_wifi_event_inst = NULL;
static esp_event_handler_instance_t s_ip_event_inst = NULL;
static esp_timer_handle_t s_reconnect_timer = NULL;
static EventGroupHandle_t s_wifi_event_group = NULL;

typedef struct {
    char mac[18];
    char ip[16];
} wifi_netinfo_payload_t;

static void wifi_netinfo_update_cb(void *user_data)
{
    wifi_netinfo_payload_t *p = (wifi_netinfo_payload_t *)user_data;
    if (p == NULL) {
        return;
    }
    ui_runtime_set_network_info(p->mac, p->ip);
    free(p);
}

static void wifi_bemfa_client_stop_lvgl_cb(void *user_data)
{
    (void)user_data;
#if APP_FEATURE_MQTT
    wifi_bemfa_client_stop();
#endif
}

static void wifi_format_bssid(char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }
    out[0] = '\0';
    wifi_ap_record_t ap = {0};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
                 ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);
    }
}

typedef enum {
    WIFI_SM_IDLE = 0,
    WIFI_SM_CONNECTING,
    WIFI_SM_CONNECTED,
    WIFI_SM_RETRY_WAIT,
    WIFI_SM_AUTH_FAILED,
} wifi_sm_state_t;

static wifi_sm_state_t s_state = WIFI_SM_IDLE;
static TaskHandle_t s_wifi_scan_task_handle = NULL;
static volatile bool s_scan_refresh_requested = false;
static int s_retry_count = 0;
static bool s_has_credentials = false;
static wifi_config_t s_last_cfg;
static bool s_time_task_started = false;
static bool s_sntp_inited = false;
static lv_obj_t *s_screen7_blocker = NULL;
static bool s_saved_has_cred = false;
static bool s_boot_auto_connect = true;
static bool s_auto_reconnect = false;
/* connect() 主动 disconnect 时抑制一次重连调度，避免与 esp_wifi_connect 竞态. */
static volatile bool s_suppress_disconnect_reconnect = false;
static char s_saved_ssid[33] = {0};
static char s_saved_password[65] = {0};
static int s_wifi_dropdown_update_depth = 0;
/* Coalesce AP-list refreshes: at most one dropdown update may sit in the GUI
 * queue at a time, so a busy/stuck GUI never makes the scan task pile on
 * redundant (heavy) lv_dropdown_set_options jobs. */
static volatile bool s_wifi_dropdown_pending = false;
/* Scan results deferred while the dropdown list is open: lv_dropdown_open() binds the
 * list label to dropdown->options via lv_label_set_text_static(); lv_dropdown_set_options()
 * frees that buffer — updating in-place causes the list to show garbage/blank. */
static char *s_deferred_dropdown_options = NULL;
static SemaphoreHandle_t s_scan_mutex = NULL;

static void wifi_load_saved_credentials(void)
{
    nvs_handle_t h;
    if (nvs_open("wifi_cfg", NVS_READONLY, &h) != ESP_OK) {
        return;
    }

    size_t ssid_len = sizeof(s_saved_ssid);
    size_t pwd_len = sizeof(s_saved_password);
    if (nvs_get_str(h, "ssid", s_saved_ssid, &ssid_len) == ESP_OK &&
        nvs_get_str(h, "pwd", s_saved_password, &pwd_len) == ESP_OK &&
        s_saved_ssid[0] != '\0') {
        s_saved_has_cred = true;
        memset(&s_last_cfg, 0, sizeof(s_last_cfg));
        strlcpy((char *)s_last_cfg.sta.ssid, s_saved_ssid, sizeof(s_last_cfg.sta.ssid));
        strlcpy((char *)s_last_cfg.sta.password, s_saved_password, sizeof(s_last_cfg.sta.password));
        s_has_credentials = true;
        ESP_LOGI(TAG, "NVS wifi_cfg loaded ssid=%s", s_saved_ssid);
    }

    nvs_close(h);
}

static void wifi_save_credentials_to_nvs(const wifi_config_t *cfg)
{
    if (cfg == NULL || cfg->sta.ssid[0] == '\0') {
        return;
    }

    nvs_handle_t h;
    if (nvs_open("wifi_cfg", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }

    (void)nvs_set_str(h, "ssid", (const char *)cfg->sta.ssid);
    (void)nvs_set_str(h, "pwd", (const char *)cfg->sta.password);
    (void)nvs_commit(h);
    nvs_close(h);

    strlcpy(s_saved_ssid, (const char *)cfg->sta.ssid, sizeof(s_saved_ssid));
    strlcpy(s_saved_password, (const char *)cfg->sta.password, sizeof(s_saved_password));
    s_saved_has_cred = true;
}

typedef struct {
    char *options;
} wifi_dropdown_payload_t;

typedef struct {
    char status[64];
    char ssid[33];
    bool show_ssid;
} wifi_ui_status_payload_t;

static void wifi_time_update_ui_cb(void *user_data)
{
    (void)user_data;
    time_t now = 0;
    struct tm info = {0};
    time(&now);
    localtime_r(&now, &info);

    char date_buf[32];
    char time_buf[32];

    /* Label1: time in 24h format, e.g. 16:16 */
    strftime(time_buf, sizeof(time_buf), "%H:%M", &info);

    /* Label3: Western abbreviated date, e.g. Apr 3, Fri */
    static const char *mon_abbr[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    static const char *wday_abbr[7] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    const char *m = (info.tm_mon >= 0 && info.tm_mon < 12) ? mon_abbr[info.tm_mon] : "";
    const char *w = (info.tm_wday >= 0 && info.tm_wday < 7) ? wday_abbr[info.tm_wday] : "";
    snprintf(date_buf, sizeof(date_buf), "%s %d, %s", m, info.tm_mday, w);

    if (ui_Label1) {
        lv_label_set_text(ui_Label1, time_buf);
    }
    if (ui_Label3) {
        lv_label_set_text(ui_Label3, date_buf);
    }
}

static void wifi_time_task(void *arg)
{
    (void)arg;

    /* SNTP init once, keep running in background. */
    if (!s_sntp_inited) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        /* Multi-source improves sync stability in CN networks. */
        esp_sntp_setservername(0, "ntp.aliyun.com");
        esp_sntp_setservername(1, "cn.pool.ntp.org");
        esp_sntp_setservername(2, "pool.ntp.org");
        esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
        esp_sntp_init();
        s_sntp_inited = true;
    }

    /* Default to China Standard Time; adjust if needed */
    setenv("TZ", "CST-8", 1);
    tzset();

    ESP_LOGI(TAG, "SNTP waiting for sync (servers=%d)",
#if defined(CONFIG_LWIP_SNTP_MAX_SERVERS)
             CONFIG_LWIP_SNTP_MAX_SERVERS
#else
             1
#endif
    );

    /* Wait first sync, then keep updating UI periodically. */
    int retry = 0;
    const int max_retry = 120; /* up to ~60s (120 * 500ms) */
    const TickType_t tick_500ms = pdMS_TO_TICKS(500);
    const TickType_t tick_1s = pdMS_TO_TICKS(1000);
    time_t now = 0;
    struct tm info = {0};

    while (retry < max_retry) {
        time(&now);
        localtime_r(&now, &info);
        sntp_sync_status_t st = esp_sntp_get_sync_status();

        if (st == SNTP_SYNC_STATUS_COMPLETED || info.tm_year >= (2024 - 1900)) {
            break;
        }
        vTaskDelay(tick_500ms);
        retry++;
    }

    /* Keep clock moving even between SNTP polls. */
    while (true) {
        gui_task_post_lvgl(wifi_time_update_ui_cb, NULL);
        vTaskDelay(tick_1s);
    }
}

static bool ssid_exists(char *const list[], size_t count, const char *ssid)
{
    for (size_t i = 0; i < count; i++) {
        if (strcmp(list[i], ssid) == 0) {
            return true;
        }
    }
    return false;
}

/* options: LVGL dropdown string, SSIDs separated by '\n' */
static int wifi_option_index_for_ssid(const char *options, const char *ssid)
{
    if (options == NULL || ssid == NULL || ssid[0] == '\0') {
        return 0;
    }

    int idx = 0;
    const char *p = options;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t line_len = nl ? (size_t)(nl - p) : strlen(p);
        size_t ssid_len = strlen(ssid);
        if (line_len == ssid_len && strncmp(p, ssid, line_len) == 0) {
            return idx;
        }
        if (nl) {
            p = nl + 1;
            idx++;
        } else {
            break;
        }
    }
    return 0;
}

static void wifi_ui_status_update_cb(void *user_data)
{
    wifi_ui_status_payload_t *payload = (wifi_ui_status_payload_t *)user_data;
    if (payload == NULL) {
        return;
    }

    if (ui_Label42 != NULL) {
        lv_label_set_text(ui_Label42, payload->status);
    }

    if (ui_Label43 != NULL) {
        if (payload->show_ssid) {
            lv_label_set_text(ui_Label43, payload->ssid);
            lv_obj_set_style_text_font(ui_Label43, wifi_ui_font_ssid(), LV_PART_MAIN | LV_STATE_DEFAULT);
        } else {
            lv_label_set_text(ui_Label43, "");
        }
    }

    /* Mirror connected SSID to Screen6 Wi-Fi button label */
    if (ui_Label24 != NULL && payload->show_ssid) {
        if (strncmp(payload->status, "Connected", 9) == 0) {
            lv_label_set_text(ui_Label24, payload->ssid);
            lv_obj_set_style_text_font(ui_Label24, wifi_ui_font_ssid(), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    /* Block Screen7 interaction until Wi-Fi fully connected */
    if (ui_Screen7 != NULL) {
        /* Only block during an active connection attempt / IP acquiring / retry.
         * Do NOT block on initial "Not connected" after reboot, otherwise user
         * can't enter SSID/password to start a new connection.
         */
        const char *st = payload->status;
        bool should_block = false;
        if (st != NULL) {
            if (strncmp(st, "Connecting", 9) == 0) {
                should_block = true;
            } else if (strncmp(st, "Connected, getting IP", 21) == 0) {
                should_block = true;
            } else if (strncmp(st, "Signal weak", 12) == 0) {
                /* Auto reconnect backoff in progress */
                should_block = true;
            } else if (strncmp(st, "Reconnecting", 12) == 0) {
                should_block = true;
            } else {
                should_block = false;
            }
        }

        if (should_block) {
            if (!s_screen7_blocker) {
                s_screen7_blocker = lv_obj_create(ui_Screen7);
                lv_obj_remove_style_all(s_screen7_blocker);
                lv_obj_set_size(s_screen7_blocker, LV_PCT(100), LV_PCT(100));
                lv_obj_set_align(s_screen7_blocker, LV_ALIGN_CENTER);

                /* Very transparent but still hit-testable */
                lv_obj_set_style_bg_color(s_screen7_blocker, lv_color_hex(0x000000),
                                           LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_bg_opa(s_screen7_blocker, 1,
                                         LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_border_opa(s_screen7_blocker, 0,
                                            LV_PART_MAIN | LV_STATE_DEFAULT);

                lv_obj_clear_flag(s_screen7_blocker, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_add_flag(s_screen7_blocker,
                                LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);

                lv_obj_move_foreground(s_screen7_blocker);
            }
            lv_obj_clear_flag(s_screen7_blocker, LV_OBJ_FLAG_HIDDEN);
        } else {
            if (s_screen7_blocker) {
                lv_obj_add_flag(s_screen7_blocker, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    free(payload);
}

static void wifi_post_status_to_ui(const char *status, const char *ssid_or_null)
{
    wifi_ui_status_payload_t *payload = (wifi_ui_status_payload_t *)calloc(1, sizeof(wifi_ui_status_payload_t));
    if (payload == NULL) {
        return;
    }

    strlcpy(payload->status, status ? status : "", sizeof(payload->status));
    if (ssid_or_null != NULL && ssid_or_null[0] != '\0') {
        payload->show_ssid = true;
        strlcpy(payload->ssid, ssid_or_null, sizeof(payload->ssid));
    } else {
        payload->show_ssid = false;
    }

    if (!gui_task_post_lvgl(wifi_ui_status_update_cb, payload)) {
        free(payload);
    }
}

bool wifi_management_is_dropdown_refresh(void)
{
    return s_wifi_dropdown_update_depth > 0;
}

static void wifi_dropdown_apply_options(const char *prev_ssid, char *options)
{
    if (ui_Dropdown1 == NULL || options == NULL) {
        free(options);
        return;
    }

    lv_dropdown_set_options(ui_Dropdown1, options);

    int sel = 0;
    if (prev_ssid != NULL && prev_ssid[0] != '\0' && strcmp(prev_ssid, "Searching...") != 0) {
        sel = wifi_option_index_for_ssid(options, prev_ssid);
    }
    lv_dropdown_set_selected(ui_Dropdown1, (uint16_t)sel);
    /* Important: lv_dropdown_set_text() only keeps the pointer, so never pass a stack buffer.
     * When dropdown->text is NULL LVGL draws the selected option automatically.
     */
    lv_dropdown_set_text(ui_Dropdown1, NULL);
    {
        const lv_font_t *f = wifi_ui_font_ssid();
        lv_obj_set_style_text_font(ui_Dropdown1, &lv_font_montserrat_14, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_Dropdown1, f, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_Dropdown1, f, LV_PART_SELECTED | LV_STATE_DEFAULT);
        lv_obj_t *dd_list = lv_dropdown_get_list(ui_Dropdown1);
        if (dd_list) {
            lv_obj_set_style_text_font(dd_list, f, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(dd_list, f, LV_PART_SELECTED | LV_STATE_DEFAULT);
        }
    }

    free(options);
}

void wifi_management_dropdown_list_closed(void)
{
    if (s_deferred_dropdown_options == NULL || ui_Dropdown1 == NULL) {
        return;
    }

    char prev_ssid[64] = {0};
    lv_dropdown_get_selected_str(ui_Dropdown1, prev_ssid, sizeof(prev_ssid));

    char *options = s_deferred_dropdown_options;
    s_deferred_dropdown_options = NULL;

    s_wifi_dropdown_update_depth++;
    wifi_dropdown_apply_options(prev_ssid, options);
    s_wifi_dropdown_update_depth--;
    ui_runtime_rgb_commit_active_screen();
}

static void wifi_dropdown_update_cb(void *user_data)
{
    wifi_dropdown_payload_t *payload = (wifi_dropdown_payload_t *)user_data;
    /* Clear early: a fresh scan that arrives while we render may queue the next
     * refresh, keeping the list current without unbounded queue growth. */
    s_wifi_dropdown_pending = false;
    if (payload == NULL) {
        return;
    }

    s_wifi_dropdown_update_depth++;

    char prev_ssid[64] = {0};
    if (ui_Dropdown1 != NULL) {
        lv_dropdown_get_selected_str(ui_Dropdown1, prev_ssid, sizeof(prev_ssid));
    }

    if (ui_Dropdown1 != NULL && payload->options != NULL) {
        if (lv_dropdown_is_open(ui_Dropdown1)) {
            free(s_deferred_dropdown_options);
            s_deferred_dropdown_options = payload->options;
            payload->options = NULL;
        } else {
            wifi_dropdown_apply_options(prev_ssid, payload->options);
            payload->options = NULL;
        }
    }

    free(payload->options);
    free(payload);

    s_wifi_dropdown_update_depth--;
    ui_runtime_rgb_commit_active_screen();
}

static esp_err_t wifi_scan_once_and_publish(void)
{
    if (s_scan_mutex == NULL || xSemaphoreTake(s_scan_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(s_wifi_scan_ap_buf, 0, sizeof(s_wifi_scan_ap_buf));

    uint16_t number = WIFI_SCAN_MAX_AP;
    esp_err_t scan_err = esp_wifi_scan_start(NULL, true);
    if (scan_err != ESP_OK) {
        xSemaphoreGive(s_scan_mutex);
        return scan_err;
    }
    esp_err_t rec_err = esp_wifi_scan_get_ap_records(&number, s_wifi_scan_ap_buf);
    if (rec_err != ESP_OK) {
        xSemaphoreGive(s_scan_mutex);
        return rec_err;
    }

    char *options = (char *)calloc(1, WIFI_SCAN_OPTIONS_BUF_SIZE);
    if (options == NULL) {
        xSemaphoreGive(s_scan_mutex);
        return ESP_ERR_NO_MEM;
    }

    size_t offset = 0;
    char *dedup[WIFI_SCAN_MAX_AP];
    size_t dedup_count = 0;

    for (uint16_t i = 0; i < number; i++) {
        const char *ssid = (const char *)s_wifi_scan_ap_buf[i].ssid;
        if (ssid[0] == '\0') {
            continue;
        }
        if (ssid_exists(dedup, dedup_count, ssid)) {
            continue;
        }
        dedup[dedup_count++] = (char *)ssid;

        int written = snprintf(options + offset, WIFI_SCAN_OPTIONS_BUF_SIZE - offset, "%s\n", ssid);
        if (written <= 0 || (size_t)written >= (WIFI_SCAN_OPTIONS_BUF_SIZE - offset)) {
            break;
        }
        offset += (size_t)written;
    }

    if (offset == 0) {
        snprintf(options, WIFI_SCAN_OPTIONS_BUF_SIZE, "Searching...");
    } else {
        options[offset - 1] = '\0'; /* remove trailing '\n' */
    }

    /* NVS auto-connect runs only from wifi_management_start(); do not connect from
     * scan here — it races with Screen7 and user SSID choice in the dropdown.
     */

    /* A previous AP-list refresh is still waiting in the GUI queue: skip posting
     * another. The pending one renders the (near-identical) list shortly; this
     * caps the scan task's footprint on the GUI queue to a single job. */
    if (s_wifi_dropdown_pending) {
        free(options);
        xSemaphoreGive(s_scan_mutex);
        ESP_LOGD(TAG, "scan ok (%u AP); dropdown refresh already queued", number);
        return ESP_OK;
    }

    wifi_dropdown_payload_t *payload = (wifi_dropdown_payload_t *)malloc(sizeof(wifi_dropdown_payload_t));
    if (payload == NULL) {
        free(options);
        xSemaphoreGive(s_scan_mutex);
        return ESP_ERR_NO_MEM;
    }

    payload->options = options;
    s_wifi_dropdown_pending = true;
    if (!gui_task_post_lvgl(wifi_dropdown_update_cb, payload)) {
        /* GUI momentarily busy. The scan itself succeeded — not an error; the
         * next scan/refresh will re-publish. Avoid log-storming a benign drop. */
        s_wifi_dropdown_pending = false;
        free(payload->options);
        free(payload);
        ESP_LOGD(TAG, "scan ok (%u AP); UI refresh deferred (gui busy)", number);
        xSemaphoreGive(s_scan_mutex);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Wi-Fi scan published %u AP(s)", number);
    xSemaphoreGive(s_scan_mutex);
    return ESP_OK;
}

static void wifi_scan_task(void *arg)
{
    (void)arg;
    while (1) {
        bool do_scan = false;
        if (s_scan_refresh_requested) {
            s_scan_refresh_requested = false;
            /* 已关联/连接中全信道扫描易 beacon miss → 断线；仅空闲时允许 UI 刷新扫描。 */
            if (s_state == WIFI_SM_CONNECTED || s_state == WIFI_SM_CONNECTING ||
                s_state == WIFI_SM_RETRY_WAIT) {
                ESP_LOGD(TAG, "skip AP scan while link active (state=%d)", (int)s_state);
            } else {
                do_scan = true;
            }
        } else if (s_background_scan && (s_state == WIFI_SM_IDLE || s_state == WIFI_SM_AUTH_FAILED)) {
            do_scan = true;
        }
        if (do_scan) {
            esp_err_t err = wifi_scan_once_and_publish();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "scan/publish failed: %s", esp_err_to_name(err));
            }
        }
        uint32_t wait_ms = ((s_state == WIFI_SM_IDLE || s_state == WIFI_SM_AUTH_FAILED)
                                ? WIFI_SCAN_PERIOD_IDLE_MS
                                : WIFI_SCAN_PERIOD_QUIET_MS);
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
    }
}

void wifi_management_set_background_scan(bool enable)
{
    s_background_scan = enable;
}

void wifi_management_request_scan_refresh(void)
{
    s_scan_refresh_requested = true;
    if (s_wifi_scan_task_handle != NULL) {
        xTaskNotifyGive(s_wifi_scan_task_handle);
    }
}

static void wifi_reconnect_timer_cb(void *arg);
static void wifi_schedule_reconnect(uint32_t delay_ms);
static void wifi_log_heap(const char *when);

static bool wifi_reason_is_auth_error(wifi_err_reason_t reason)
{
    if (reason == WIFI_REASON_AUTH_FAIL ||
        reason == WIFI_REASON_HANDSHAKE_TIMEOUT ||
        reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) {
        return true;
    }
#ifdef WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT
    if (reason == WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT) {
        return true;
    }
#endif
#ifdef WIFI_REASON_802_1X_AUTH_FAILED
    if (reason == WIFI_REASON_802_1X_AUTH_FAILED) {
        return true;
    }
#endif
    return false;
}

static bool wifi_reason_is_signal_error(wifi_err_reason_t reason)
{
    if (reason == WIFI_REASON_NO_AP_FOUND ||
        reason == WIFI_REASON_BEACON_TIMEOUT ||
        reason == WIFI_REASON_ASSOC_EXPIRE) {
        return true;
    }
#ifdef WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD
    if (reason == WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD) {
        return true;
    }
#endif
    return false;
}

static void wifi_reconnect_timer_cb(void *arg)
{
    (void)arg;
    if (!s_has_credentials || !s_auto_reconnect) {
        return;
    }
    ESP_LOGI(TAG, "reconnect timer fired, attempt=%d", s_retry_count);
    s_state = WIFI_SM_CONNECTING;
    wifi_post_status_to_ui("Reconnecting...", NULL);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
}

static void wifi_schedule_reconnect(uint32_t delay_ms)
{
    if (s_reconnect_timer == NULL) {
        return;
    }
    esp_timer_stop(s_reconnect_timer);
    s_state = WIFI_SM_RETRY_WAIT;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_start_once(s_reconnect_timer, (uint64_t)delay_ms * 1000ULL));
    ESP_LOGW(TAG, "schedule reconnect in %lu ms", (unsigned long)delay_ms);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (event_base != WIFI_EVENT) {
        return;
    }

    switch (event_id) {
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
        wifi_post_status_to_ui("WiFi started", NULL);
        break;
    case WIFI_EVENT_SCAN_DONE:
        ESP_LOGI(TAG, "WIFI_EVENT_SCAN_DONE");
        break;
    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED");
        s_state = WIFI_SM_CONNECTING;
        wifi_post_status_to_ui("Connected, getting IP...", NULL);
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
    {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        wifi_err_reason_t reason = disc ? disc->reason : WIFI_REASON_UNSPECIFIED;
        ESP_LOGW(TAG, "WIFI_EVENT_STA_DISCONNECTED, reason=%d", (int)reason);
        wifi_log_heap("on disconnect");
        /* Wi-Fi not ready: stop cloud client to prevent futile reconnect attempts. */
        /* Defer MQTT stop out of esp_event callback context to avoid race with
         * esp_event / mqtt internals (esp_mqtt_client_destroy may touch mutexes/queues). */
        (void)gui_task_post_lvgl(wifi_bemfa_client_stop_lvgl_cb, NULL);

        /* connect() 前主动 disconnect：忽略这一拍，避免与 esp_wifi_connect 双路径竞态。 */
        if (s_suppress_disconnect_reconnect) {
            s_suppress_disconnect_reconnect = false;
            if (s_wifi_event_group != NULL) {
                xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            }
            if (s_state == WIFI_SM_CONNECTING) {
                wifi_post_status_to_ui("Connecting...", NULL);
            }
            ESP_LOGI(TAG, "suppress reconnect after intentional disconnect (reason=%d)", (int)reason);
            break;
        }

        if (!s_has_credentials || !s_auto_reconnect) {
            s_state = WIFI_SM_IDLE;
            if (s_wifi_event_group != NULL) {
                xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            }
            wifi_post_status_to_ui("Not connected", NULL);
            /* Clear About page network info when Wi-Fi is not connected. */
            {
                wifi_netinfo_payload_t *p = (wifi_netinfo_payload_t *)calloc(1, sizeof(wifi_netinfo_payload_t));
                if (p) {
                    snprintf(p->mac, sizeof(p->mac), "-");
                    p->ip[0] = '\0';
                    if (!gui_task_post_lvgl(wifi_netinfo_update_cb, p)) {
                        free(p);
                    }
                }
            }
            break;
        }

        if (wifi_reason_is_auth_error(reason)) {
            s_state = WIFI_SM_AUTH_FAILED;
            s_retry_count = 0;
            ESP_LOGE(TAG, "auth failed, stop auto reconnect");
            if (s_wifi_event_group != NULL) {
                xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            }
            wifi_post_status_to_ui("Authentication failed", NULL);
            break;
        }

        if (s_retry_count >= WIFI_RECONNECT_MAX_ATTEMPTS) {
            s_retry_count = 0;
            if (s_wifi_event_group != NULL) {
                xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            }
            ESP_LOGW(TAG, "reconnect burst exhausted; cooldown %lu ms",
                     (unsigned long)WIFI_RECONNECT_COOLDOWN_MS);
            wifi_post_status_to_ui("Reconnect paused...", NULL);
            wifi_schedule_reconnect(WIFI_RECONNECT_COOLDOWN_MS);
            break;
        }

        uint32_t delay_ms = WIFI_RECONNECT_BASE_MS;
        const char *retry_ui = "Retrying...";
        if (wifi_reason_is_signal_error(reason)) {
            uint32_t backoff_scale = (uint32_t)(1U << (s_retry_count > 4 ? 4 : s_retry_count));
            delay_ms = WIFI_RECONNECT_BASE_MS * backoff_scale;
            retry_ui = "Signal weak, retrying...";
        } else if (reason == WIFI_REASON_AUTH_EXPIRE || reason == WIFI_REASON_ASSOC_LEAVE ||
                   reason == WIFI_REASON_AUTH_LEAVE) {
            /* AUTH_EXPIRE(2) 常见于共存/内部堆紧张，短退避后重试，勿当永久失败。 */
            delay_ms = WIFI_RECONNECT_BASE_MS + (s_retry_count * 500u);
            retry_ui = "Auth timeout, retrying...";
        }
        s_retry_count++;
        if (s_wifi_event_group != NULL) {
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
        wifi_post_status_to_ui(retry_ui, NULL);
        wifi_schedule_reconnect(delay_ms);
        break;
    }
    default:
        break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base != IP_EVENT || event_id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP: " IPSTR, IP2STR(&event->ip_info.ip));
    s_state = WIFI_SM_CONNECTED;
    s_retry_count = 0;
#if defined(CONFIG_ESP_COEX_SW_COEXIST_ENABLE) && CONFIG_ESP_COEX_SW_COEXIST_ENABLE
    (void)esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
#endif
    if (s_wifi_event_group != NULL) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
    wifi_post_status_to_ui("Connected", (const char *)s_last_cfg.sta.ssid);

    if (esp_sntp_enabled()) {
        esp_sntp_restart();
    }

    /* Start cloud client only after IP is ready. */
#if APP_FEATURE_MQTT
    (void)wifi_bemfa_client_start();
#endif

    /* Update About page (Screen11): Wi-Fi BSSID (AP MAC) + STA IP */
    {
        wifi_netinfo_payload_t *p = (wifi_netinfo_payload_t *)calloc(1, sizeof(wifi_netinfo_payload_t));
        if (p) {
            wifi_format_bssid(p->mac, sizeof(p->mac));
            snprintf(p->ip, sizeof(p->ip), IPSTR, IP2STR(&event->ip_info.ip));

            if (!gui_task_post_lvgl(wifi_netinfo_update_cb, p)) {
                free(p);
            }
        }
    }
    if (!s_time_task_started) {
        s_time_task_started = true;
        xTaskCreatePinnedToCore(wifi_time_task, "wifi_time_task", 4096, NULL, 3, NULL, 0);
    }
    if (s_reconnect_timer != NULL) {
        esp_timer_stop(s_reconnect_timer);
    }

    /* Persist the last successful credentials so that next boot can auto-connect
     * when the same SSID is seen again.
     */
    wifi_save_credentials_to_nvs(&s_last_cfg);
}

esp_err_t wifi_management_foundation_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    /* Screen1 时钟在 GOT_IP 前也会刷；尽早设 TZ，避免 UTC/CST 跳变过大。 */
    setenv("TZ", "CST-8", 1);
    tzset();

    return ESP_OK;
}

void wifi_management_set_boot_auto_connect(bool enable)
{
    s_boot_auto_connect = enable;
}

bool wifi_management_is_connected(void)
{
    return s_state == WIFI_SM_CONNECTED;
}

static void wifi_tune_init_config_for_coexist(wifi_init_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    bool low_mem = false;
#if defined(CONFIG_ESP_COEX_SW_COEXIST_ENABLE) && CONFIG_ESP_COEX_SW_COEXIST_ENABLE
#if (defined(CONFIG_BT_ENABLED) && CONFIG_BT_ENABLED) || (defined(CONFIG_BLE_MESH) && CONFIG_BLE_MESH)
    low_mem = true;
#endif
#endif
#if defined(CONFIG_FRAME_UI_BACKEND_TESTBENCH) && CONFIG_FRAME_UI_BACKEND_TESTBENCH
    low_mem = true;
#endif
    if (!low_mem) {
        return;
    }
    if (cfg->static_rx_buf_num > 6) {
        cfg->static_rx_buf_num = 6;
    }
    if (cfg->dynamic_rx_buf_num > 12) {
        cfg->dynamic_rx_buf_num = 12;
    }
    if (cfg->mgmt_sbuf_num > 12) {
        cfg->mgmt_sbuf_num = 12;
    }
    /* TRY_ALLOCATE_WIFI_LWIP 时只能用静态 TX；16×1.6KB 会挤爆 RGB bounce / BLE 之后的 DMA */
    if (cfg->static_tx_buf_num > 8) {
        cfg->static_tx_buf_num = 8;
    }
    if (cfg->cache_tx_buf_num > 16) {
        cfg->cache_tx_buf_num = 16;
    }
    ESP_LOGI(TAG, "coexist WiFi buf: static_rx=%d dynamic_rx=%d mgmt=%d static_tx=%d cache_tx=%d",
             cfg->static_rx_buf_num, cfg->dynamic_rx_buf_num, cfg->mgmt_sbuf_num,
             cfg->static_tx_buf_num, cfg->cache_tx_buf_num);
}

static void wifi_log_heap(const char *when)
{
    ESP_LOGI(TAG, "heap %s: int=%u largest_int=%u dma=%u largest_dma=%u",
             when,
             (unsigned)esp_get_free_internal_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
}

esp_err_t wifi_management_driver_init(void)
{
    if (s_wifi_driver_inited) {
        return ESP_OK;
    }

    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif == NULL) {
        sta_netif = esp_netif_create_default_wifi_sta();
        if (sta_netif == NULL) {
            return ESP_FAIL;
        }
    }

    wifi_log_heap("before wifi_init");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_tune_init_config_for_coexist(&cfg);
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        wifi_log_heap("after wifi_init fail");
        ESP_LOGW(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        /* IDF 失败路径已 deinit；再调会刷 ESP_ERR_WIFI_NOT_INIT(0x3001) */
        return ret;
    }
    ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        return ret;
    }
    s_wifi_driver_inited = true;
    wifi_log_heap("after wifi_init");
    return ESP_OK;
}

esp_err_t wifi_management_start(void)
{
    if (s_wifi_started) {
        return ESP_OK;
    }
    if (s_wifi_start_failed) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = wifi_management_driver_init();
    if (ret != ESP_OK) {
        return ret;
    }
    if (s_wifi_event_inst == NULL) {
        ret = esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &s_wifi_event_inst);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    if (s_ip_event_inst == NULL) {
        ret = esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL, &s_ip_event_inst);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        if (ret != ESP_ERR_NO_MEM) {
            s_wifi_start_failed = true;
        }
        return ret;
    }

    /* Modem PS: MIN_MODEM 省电但 STA 在 DTIM 间休眠，弱信号或与 BLE 共存时易漏 beacon → 断线重连。
     * 墙面屏/常显设备可关 PS（menuconfig: APP_WIFI_STA_DISABLE_PS）。 */
#if defined(CONFIG_APP_WIFI_STA_DISABLE_PS) && CONFIG_APP_WIFI_STA_DISABLE_PS
    ret = esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "Wi-Fi PS disabled (APP_WIFI_STA_DISABLE_PS)");
#else
    ret = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
#endif
    if (ret != ESP_OK) {
        return ret;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_scan_mutex = xSemaphoreCreateMutex();
    if (s_scan_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = wifi_reconnect_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_reconn",
        .skip_unhandled_events = true,
    };
    ret = esp_timer_create(&timer_args, &s_reconnect_timer);
    if (ret != ESP_OK) {
        return ret;
    }

    s_wifi_started = true;

    /* Load previously saved credentials (if any) BEFORE scan task starts. */
    wifi_load_saved_credentials();

#if defined(CONFIG_APP_WIFI_AUTO_CONNECT_ON_BOOT) && CONFIG_APP_WIFI_AUTO_CONNECT_ON_BOOT
    if (s_saved_has_cred && s_boot_auto_connect) {
        ESP_LOGI(TAG, "boot auto-connect ssid=%s", s_saved_ssid);
        (void)wifi_management_connect(s_saved_ssid, s_saved_password);
    } else {
        s_auto_reconnect = false;
    }
#elif defined(CONFIG_FRAME_WIFI_AUTO_CONNECT_ON_BOOT) && CONFIG_FRAME_WIFI_AUTO_CONNECT_ON_BOOT
    if (s_saved_has_cred && s_boot_auto_connect) {
        (void)wifi_management_connect(s_saved_ssid, s_saved_password);
    } else {
        s_auto_reconnect = false;
    }
#else
    /* 未开 boot 自动连：保留 NVS 凭据，等 Screen7 用户连接后再开 auto_reconnect。 */
    s_auto_reconnect = false;
    ESP_LOGI(TAG, "boot auto-connect disabled (saved=%d)", s_saved_has_cred ? 1 : 0);
#endif

    /* Keep scan task lower priority than UI/BT control to improve smoothness. */
    BaseType_t ok = xTaskCreatePinnedToCore(
        wifi_scan_task, "wifi_scan_task", 4096, NULL, 2, &s_wifi_scan_task_handle, 0);
    if (ok != pdPASS) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t wifi_management_connect(const char *ssid, const char *password)
{
    if (!s_wifi_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_last_cfg, 0, sizeof(s_last_cfg));
    strlcpy((char *)s_last_cfg.sta.ssid, ssid, sizeof(s_last_cfg.sta.ssid));
    strlcpy((char *)s_last_cfg.sta.password, password ? password : "", sizeof(s_last_cfg.sta.password));
    s_last_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    s_last_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    s_last_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &s_last_cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    s_has_credentials = true;
    s_auto_reconnect = true;
    s_retry_count = 0;
    s_state = WIFI_SM_CONNECTING;
    if (s_wifi_event_group != NULL) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
    wifi_post_status_to_ui("Connecting...", NULL);

    if (s_reconnect_timer != NULL) {
        esp_timer_stop(s_reconnect_timer);
    }

    /*
     * 仅在已关联时 disconnect，并只抑制那一拍 DISCONNECTED。
     * 未关联时仍设 suppress=true 会吞掉随后真正的 AUTH_EXPIRE(2)，表现为“永远连不上”。
     */
    wifi_ap_record_t ap_info;
    const bool was_assoc = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
    if (was_assoc) {
        s_suppress_disconnect_reconnect = true;
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_disconnect());
    } else {
        s_suppress_disconnect_reconnect = false;
    }

    wifi_log_heap("before connect");
#if defined(CONFIG_ESP_COEX_SW_COEXIST_ENABLE) && CONFIG_ESP_COEX_SW_COEXIST_ENABLE
    (void)esp_coex_preference_set(ESP_COEX_PREFER_WIFI);
#endif
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        s_suppress_disconnect_reconnect = false;
#if defined(CONFIG_ESP_COEX_SW_COEXIST_ENABLE) && CONFIG_ESP_COEX_SW_COEXIST_ENABLE
        (void)esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
#endif
        return ret;
    }

    ESP_LOGI(TAG, "connect request ssid=%s (was_assoc=%d)", ssid, was_assoc ? 1 : 0);
    return ESP_OK;
}

void wifi_management_disconnect_user(void)
{
    s_auto_reconnect = false;
    s_suppress_disconnect_reconnect = true;
    if (s_reconnect_timer != NULL) {
        esp_timer_stop(s_reconnect_timer);
    }
    (void)esp_wifi_scan_stop();
    (void)esp_wifi_disconnect();
    s_state = WIFI_SM_IDLE;
    s_retry_count = 0;
    if (s_wifi_event_group != NULL) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
    wifi_post_status_to_ui("Disconnected", NULL);
}

esp_err_t wifi_management_scan_blocking(wifi_ap_record_t *recs, uint16_t *inout_count)
{
    if (!s_wifi_started) {
        if (s_wifi_start_failed) {
            return ESP_ERR_INVALID_STATE;
        }
        esp_err_t start_err = wifi_management_start();
        if (start_err != ESP_OK) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    if (recs == NULL || inout_count == NULL || *inout_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_scan_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_scan_mutex, pdMS_TO_TICKS(8000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_reconnect_timer != NULL) {
        esp_timer_stop(s_reconnect_timer);
    }
    (void)esp_wifi_scan_stop();

    if (s_state == WIFI_SM_CONNECTING || s_state == WIFI_SM_RETRY_WAIT) {
        (void)esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));
        s_state = WIFI_SM_IDLE;
    }

    uint16_t n = *inout_count;
    esp_err_t err = esp_wifi_scan_start(NULL, true);
    if (err == ESP_OK) {
        err = esp_wifi_scan_get_ap_records(&n, recs);
        *inout_count = n;
        ESP_LOGI(TAG, "blocking scan: %u AP(s), err=%s", (unsigned)n, esp_err_to_name(err));
    } else {
        ESP_LOGW(TAG, "blocking scan start failed: %s", esp_err_to_name(err));
    }

    xSemaphoreGive(s_scan_mutex);
    return err;
}

bool wifi_management_is_started(void)
{
    return s_wifi_started;
}

bool wifi_management_start_failed(void)
{
    return s_wifi_start_failed;
}

