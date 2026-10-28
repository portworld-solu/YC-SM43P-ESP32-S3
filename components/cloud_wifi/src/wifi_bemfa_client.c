#include "wifi_bemfa_client.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"

#include "bt_management.h"
#include "gui_task.h"
#include "ui_runtime.h"

static const char *TAG = "bemfa_mqtt";

#define BEMFA_MQTT_URI            "mqtt://bemfa.com:9501"
/* MQTT 保活与云端状态上报周期：偏大以降低空闲时唤醒 Wi‑Fi（见 docs/LOW_POWER_DESIGN.md）。 */
#define BEMFA_MQTT_KEEPALIVE_SEC    120
#define BEMFA_SNAPSHOT_PERIOD_US    (60ULL * 1000000ULL)
#define BEMFA_UID_DEFAULT         "1ab39688601b47beb814e4c1bf001173"
#define BEMFA_TOPIC_CMD_DEFAULT    "JeStBHKBQ006"
#define BEMFA_TOPIC_STATUS_DEFAULT "4BE3K6ebb005"

typedef struct {
    char uid[64];
    char cmd_topic[128];
    char status_topic[128];
} bemfa_cfg_t;

static bemfa_cfg_t s_cfg = {0};
static bool s_started = false;
static esp_mqtt_client_handle_t s_mqtt = NULL;
static esp_timer_handle_t s_snap_timer = NULL;
static volatile bool s_sync_scheduled = false;

void wifi_bemfa_client_stop(void)
{
    if (!s_started) {
        return;
    }
    s_started = false;

    if (s_snap_timer) {
        (void)esp_timer_stop(s_snap_timer);
    }
    s_sync_scheduled = false;

    if (s_mqtt) {
        (void)esp_mqtt_client_stop(s_mqtt);
        esp_mqtt_client_destroy(s_mqtt);
        s_mqtt = NULL;
    }
    ESP_LOGI(TAG, "mqtt client stopped");
}

static void str_trim_inplace(char *s)
{
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = '\0';
        n--;
    }
    size_t i = 0;
    while (s[i] && isspace((unsigned char)s[i])) i++;
    if (i > 0) {
        memmove(s, s + i, strlen(s + i) + 1);
    }
}

static bool str_ieq(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

static bool str_eq(const char *a, const char *b)
{
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

/* Parse first two unsigned integers in string. */
static bool parse_u8_pair_anywhere(const char *s, uint8_t *a, uint8_t *b)
{
    if (!s || !a || !b) return false;
    uint32_t out[2] = {0, 0};
    int found = 0;
    const char *p = s;
    while (*p && found < 2) {
        while (*p && !isdigit((unsigned char)*p)) p++;
        if (!*p) break;
        uint32_t acc = 0;
        while (*p && isdigit((unsigned char)*p)) {
            acc = acc * 10u + (uint32_t)(*p - '0');
            if (acc > 255u) break; /* we can still keep scanning */
            p++;
        }
        out[found++] = acc <= 255u ? acc : 255u;
    }
    if (found < 2) return false;
    *a = (uint8_t)out[0];
    *b = (uint8_t)out[1];
    return true;
}

static bool topic_contains(const char *topic, const char *needle)
{
    if (!topic || !needle) return false;
    return (strstr(topic, needle) != NULL);
}

static bool topic_match_base_or_suffix(const char *topic, const char *base)
{
    if (!topic || !base) return false;
    if (str_eq(topic, base)) return true;
    char t_set[160];
    char t_up[160];
    snprintf(t_set, sizeof(t_set), "%s/set", base);
    snprintf(t_up, sizeof(t_up), "%s/up", base);
    return str_eq(topic, t_set) || str_eq(topic, t_up);
}

static uint8_t switch_item_id_from_topic(const char *topic)
{
    if (!topic) return 0;
    if (topic_contains(topic, "Switch004") || topic_contains(topic, "power_sw4") || topic_contains(topic, "sw4")) return 1;
    if (topic_contains(topic, "Switch003") || topic_contains(topic, "power_sw3") || topic_contains(topic, "sw3")) return 2;
    if (topic_contains(topic, "Switch001") || topic_contains(topic, "power_sw1") || topic_contains(topic, "sw1")) return 3;
    return 0;
}

static uint8_t temp_step_item_id_from_topic(const char *topic, bool is_up)
{
    if (!topic) return 0;
    if (topic_contains(topic, "temp3")) return is_up ? 5 : 4; /* 5: +1 , 4: -1 */
    if (topic_contains(topic, "temp5")) return is_up ? 7 : 6; /* 7: +1 , 6: -1 */
    return 0;
}

static void bemfa_publish_status_text(const char *text);

/* Short tag for status line (kept ASCII for compact MQTT payload). */
static const char *bemfa_item_tag(uint8_t item_id)
{
    switch (item_id) {
    case 1: return "sw4";
    case 2: return "sw3";
    case 3: return "sw1";
    case 4: return "t3-";
    case 5: return "t3+";
    case 6: return "t5-";
    case 7: return "t5+";
    case 10: return "mesh";
    default: return "?";
    }
}

static void bemfa_publish_status_u8(uint8_t item_id, uint8_t value, esp_err_t result)
{
    if (s_mqtt == NULL) return;
    if (s_cfg.uid[0] == '\0' || s_cfg.status_topic[0] == '\0') return;

    /* Compact line for app: "item value rc tag"
     * rc: 0=OK, 1=FAIL. tag mirrors item (optional 4th field for UI). */
    char msg[80];
    snprintf(msg, sizeof(msg), "%u %u %u %s",
             (unsigned)item_id, (unsigned)value, (unsigned)((result == ESP_OK) ? 0 : 1),
             bemfa_item_tag(item_id));

    char t_set[160];
    char t_up[160];
    snprintf(t_set, sizeof(t_set), "%s/set", s_cfg.status_topic);
    snprintf(t_up, sizeof(t_up), "%s/up", s_cfg.status_topic);
    int mid1 = esp_mqtt_client_publish(s_mqtt, t_set, msg, 0, 0, 0);
    int mid2 = esp_mqtt_client_publish(s_mqtt, t_up, msg, 0, 0, 0);
    if (mid1 < 0 || mid2 < 0) ESP_LOGW(TAG, "status_u8 publish failed");
    else ESP_LOGI(TAG, "status_u8 tx: %s", msg);
}

static void bemfa_publish_status_text(const char *text)
{
    if (s_mqtt == NULL || text == NULL || text[0] == '\0') return;
    if (s_cfg.uid[0] == '\0' || s_cfg.status_topic[0] == '\0') return;
    char t_set[160];
    char t_up[160];
    snprintf(t_set, sizeof(t_set), "%s/set", s_cfg.status_topic);
    snprintf(t_up, sizeof(t_up), "%s/up", s_cfg.status_topic);
    int mid1 = esp_mqtt_client_publish(s_mqtt, t_set, text, 0, 0, 0);
    int mid2 = esp_mqtt_client_publish(s_mqtt, t_up, text, 0, 0, 0);
    if (mid1 < 0 || mid2 < 0) ESP_LOGW(TAG, "status_text publish failed text=%s", text);
    else ESP_LOGI(TAG, "status_text tx: %s", text);
}

static void bemfa_publish_snapshot_lvgl_cb(void *arg);

static void bemfa_snap_timer_cb(void *arg)
{
    (void)arg;
    if (s_mqtt == NULL) {
        return;
    }
    (void)gui_task_post_lvgl(bemfa_publish_snapshot_lvgl_cb, NULL);
}

static void bemfa_publish_snapshot_lvgl_cb(void *arg)
{
    (void)arg;
    if (s_mqtt == NULL || s_cfg.status_topic[0] == '\0') {
        s_sync_scheduled = false;
        return;
    }
    ui_runtime_cloud_snapshot_t snap;
    ui_runtime_fill_cloud_snapshot(&snap);
    char buf[192];
    snprintf(buf, sizeof(buf),
             "{\"v\":1,\"i1\":%u,\"i2\":%u,\"i3\":%u,\"t3\":%d,\"t5\":%d,\"e3\":%u,\"e5\":%u,\"m\":%u}",
             (unsigned)snap.i1, (unsigned)snap.i2, (unsigned)snap.i3, (int)snap.t3, (int)snap.t5,
             (unsigned)snap.e3, (unsigned)snap.e5, (unsigned)snap.m);
    bemfa_publish_status_text(buf);
    s_sync_scheduled = false;
}

void wifi_bemfa_client_schedule_sync(void)
{
    if (s_sync_scheduled) {
        return;
    }
    s_sync_scheduled = true;
    if (!gui_task_post_lvgl(bemfa_publish_snapshot_lvgl_cb, NULL)) {
        s_sync_scheduled = false;
    }
}

void wifi_bemfa_client_publish_status_u8(uint8_t item_id, uint8_t value, bool ok)
{
    bemfa_publish_status_u8(item_id, value, ok ? ESP_OK : ESP_FAIL);
}

static esp_err_t bemfa_apply_item_value(uint8_t item_id, uint8_t value)
{
    return bt_management_apply_set_state(item_id, value);
}

static void bemfa_apply_command(const char *topic, const char *msg)
{
    if (!msg || !msg[0]) return;

    ESP_LOGI(TAG, "rx cloud msg topic='%s' msg='%s'", topic ? topic : "-", msg);

    /* All commands must come from CMD topic (base or /set,/up suffix form). */
    if (topic == NULL || !topic_match_base_or_suffix(topic, s_cfg.cmd_topic)) {
        ESP_LOGW(TAG, "ignore topic='%s' expect='%s'", topic ? topic : "-", s_cfg.cmd_topic);
        return;
    }

    /* 1) If msg contains "item_id value" (e.g. "3 1" or "3,1"), use it directly. */
    uint8_t item = 0, value = 0;
    if (parse_u8_pair_anywhere(msg, &item, &value)) {
        esp_err_t err = bemfa_apply_item_value(item, value);
        /* Temp steps 4–7: ack + sync come from LVGL after power gating. */
        if (item >= 4 && item <= 7) {
            if (err != ESP_OK) {
                bemfa_publish_status_u8(item, value, err);
            }
        } else {
            bemfa_publish_status_u8(item, value, err);
        }
        return;
    }

    /* Create a lowercase copy for keyword matching (tolerant to "SW1 ON", etc.). */
    char msg_lc[256];
    snprintf(msg_lc, sizeof(msg_lc), "%s", msg);
    for (size_t i = 0; msg_lc[i] != '\0'; i++) {
        msg_lc[i] = (char)tolower((unsigned char)msg_lc[i]);
    }

    /* 2) Legacy "on/off" in the single CMD topic:
     * We map textual commands to item_id/value.
     *
     * Supported examples:
     *  - "sw1 on" / "sw1 off"
     *  - "sw3=1"
     *  - "mesh on" / "mesh off"
     *  - "temp3 up" / "temp3 down"
     *  - "temp5 +" / "temp5 -"
     */
    bool is_on = str_ieq(msg_lc, "on");
    bool is_off = str_ieq(msg_lc, "off");
    if (is_on || is_off) {
        /* Bare "on/off" is ambiguous in single-topic mode; ignore. */
        return;
    }

    /* Keyword-based commands inside msg. */
    bool msg_has_sw1 = (strstr(msg_lc, "sw1") != NULL) || (strstr(msg_lc, "switch001") != NULL);
    bool msg_has_sw3 = (strstr(msg_lc, "sw3") != NULL) || (strstr(msg_lc, "switch003") != NULL);
    bool msg_has_sw4 = (strstr(msg_lc, "sw4") != NULL) || (strstr(msg_lc, "switch004") != NULL);
    bool msg_has_mesh = (strstr(msg_lc, "mesh") != NULL) || (strstr(msg_lc, "prov") != NULL);
    bool msg_has_temp3 = (strstr(msg_lc, "temp3") != NULL) || (strstr(msg_lc, "t3") != NULL);
    bool msg_has_temp5 = (strstr(msg_lc, "temp5") != NULL) || (strstr(msg_lc, "t5") != NULL);

    bool want_on = (strstr(msg_lc, "on") != NULL) || (strstr(msg_lc, "=1") != NULL);
    bool want_off = (strstr(msg_lc, "off") != NULL) || (strstr(msg_lc, "=0") != NULL);
    bool want_up = (strstr(msg_lc, "up") != NULL) || (strstr(msg_lc, "inc") != NULL) || (strstr(msg_lc, "+") != NULL) || (strstr(msg_lc, "add") != NULL);
    bool want_down = (strstr(msg_lc, "down") != NULL) || (strstr(msg_lc, "dec") != NULL) || (strstr(msg_lc, "-") != NULL) || (strstr(msg_lc, "sub") != NULL);

    if (msg_has_mesh && (want_on || want_off)) {
        uint8_t v = want_on ? 1 : 0;
        esp_err_t err = bemfa_apply_item_value(10, v);
        bemfa_publish_status_u8(10, v, err);
        return;
    }
    if (msg_has_sw4 && (want_on || want_off)) {
        uint8_t v = want_on ? 1 : 0;
        esp_err_t err = bemfa_apply_item_value(1, v);
        bemfa_publish_status_u8(1, v, err);
        return;
    }
    if (msg_has_sw3 && (want_on || want_off)) {
        uint8_t v = want_on ? 1 : 0;
        esp_err_t err = bemfa_apply_item_value(2, v);
        bemfa_publish_status_u8(2, v, err);
        return;
    }
    if (msg_has_sw1 && (want_on || want_off)) {
        uint8_t v = want_on ? 1 : 0;
        esp_err_t err = bemfa_apply_item_value(3, v);
        bemfa_publish_status_u8(3, v, err);
        return;
    }
    if (msg_has_temp3 && (want_up || want_down)) {
        uint8_t iid = want_up ? 5 : 4;
        esp_err_t err = bemfa_apply_item_value(iid, 1);
        if (err != ESP_OK) {
            bemfa_publish_status_u8(iid, 1, err);
        }
        return;
    }
    if (msg_has_temp5 && (want_up || want_down)) {
        uint8_t iid = want_up ? 7 : 6;
        esp_err_t err = bemfa_apply_item_value(iid, 1);
        if (err != ESP_OK) {
            bemfa_publish_status_u8(iid, 1, err);
        }
        return;
    }

    ESP_LOGW(TAG, "unhandled command topic='%s' msg='%s'", topic ? topic : "-", msg);
    bemfa_publish_status_text("err unhandled cmd");
}

static void bemfa_parse_and_apply_mqtt(const char *topic, const char *payload)
{
    if (!topic || !payload) return;
    char tbuf[128];
    char pbuf[256];
    snprintf(tbuf, sizeof(tbuf), "%s", topic);
    snprintf(pbuf, sizeof(pbuf), "%s", payload);
    str_trim_inplace(tbuf);
    str_trim_inplace(pbuf);
    if (tbuf[0] == '\0' || pbuf[0] == '\0') return;
    bemfa_apply_command(tbuf, pbuf);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    if (!event) return;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "mqtt connected");
        char t_set[160];
        snprintf(t_set, sizeof(t_set), "%s/set", s_cfg.cmd_topic);
        int mid1 = esp_mqtt_client_subscribe(s_mqtt, s_cfg.cmd_topic, 0);
        int mid2 = esp_mqtt_client_subscribe(s_mqtt, t_set, 0);
        ESP_LOGI(TAG, "subscribe topic=%s mid=%d", s_cfg.cmd_topic, mid1);
        ESP_LOGI(TAG, "subscribe topic=%s mid=%d", t_set, mid2);
        bemfa_publish_status_text("sys online");
        if (s_snap_timer == NULL) {
            const esp_timer_create_args_t targs = {
                .callback = &bemfa_snap_timer_cb,
                .name = "bemfa_snap",
            };
            (void)esp_timer_create(&targs, &s_snap_timer);
        }
        if (s_snap_timer) {
            (void)esp_timer_stop(s_snap_timer);
            (void)esp_timer_start_periodic(s_snap_timer, BEMFA_SNAPSHOT_PERIOD_US);
        }
        (void)gui_task_post_lvgl(bemfa_publish_snapshot_lvgl_cb, NULL);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "mqtt disconnected");
        if (s_snap_timer) {
            (void)esp_timer_stop(s_snap_timer);
        }
        break;
    case MQTT_EVENT_DATA: {
        if (event->topic_len <= 0 || event->data_len <= 0) break;
        if (event->current_data_offset != 0) break;
        if (event->total_data_len != event->data_len) break;

        char topic[128] = {0};
        char payload[256] = {0};
        int tlen = event->topic_len < (int)sizeof(topic) - 1 ? event->topic_len : (int)sizeof(topic) - 1;
        int dlen = event->data_len < (int)sizeof(payload) - 1 ? event->data_len : (int)sizeof(payload) - 1;
        memcpy(topic, event->topic, (size_t)tlen);
        memcpy(payload, event->data, (size_t)dlen);
        topic[tlen] = '\0';
        payload[dlen] = '\0';
        bemfa_parse_and_apply_mqtt(topic, payload);
        break;
    }
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "mqtt error");
        break;
    default:
        break;
    }
}

esp_err_t wifi_bemfa_client_start(void)
{
    if (s_started) return ESP_OK;
    s_started = true;

    snprintf(s_cfg.uid, sizeof(s_cfg.uid), "%s", BEMFA_UID_DEFAULT);
    snprintf(s_cfg.cmd_topic, sizeof(s_cfg.cmd_topic), "%s", BEMFA_TOPIC_CMD_DEFAULT);
    snprintf(s_cfg.status_topic, sizeof(s_cfg.status_topic), "%s", BEMFA_TOPIC_STATUS_DEFAULT);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = BEMFA_MQTT_URI,
        .credentials.client_id = BEMFA_UID_DEFAULT,
        .credentials.username = BEMFA_UID_DEFAULT,
        .session.keepalive = BEMFA_MQTT_KEEPALIVE_SEC,
        .network.disable_auto_reconnect = false,
    };
    s_mqtt = esp_mqtt_client_init(&cfg);
    if (!s_mqtt) {
        return ESP_FAIL;
    }
    esp_err_t err = esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_mqtt_client_start(s_mqtt);
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "mqtt client started uri=%s", BEMFA_MQTT_URI);
    return ESP_OK;
}

