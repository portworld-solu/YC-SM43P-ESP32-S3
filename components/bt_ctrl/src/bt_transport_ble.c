#include "bt_management.h"
#include "bt_gatt_star.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "bt_proto.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

#if defined(CONFIG_BT_ENABLED)

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#if CONFIG_BLE_MESH
bool bt_mesh_node_is_unprov_beacon_active(void);
void bt_mesh_node_suspend_for_ble_link(void);
void bt_mesh_node_resume_after_ble_link(void);
#endif

static const char *TAG = "bt_ble";

/* 128-bit UUID: 9E6B0001-5C3A-4E2B-A1E5-1234567890AB */
static const uint8_t UUID_SVC[16] = { 0xAB,0x90,0x78,0x56,0x34,0x12,0xE5,0xA1,0x2B,0x4E,0x3A,0x5C,0x01,0x00,0x6B,0x9E };
/* RX: 9E6B0002-... */
static const uint8_t UUID_RX[16]  = { 0xAB,0x90,0x78,0x56,0x34,0x12,0xE5,0xA1,0x2B,0x4E,0x3A,0x5C,0x02,0x00,0x6B,0x9E };
/* TX: 9E6B0003-... */
static const uint8_t UUID_TX[16]  = { 0xAB,0x90,0x78,0x56,0x34,0x12,0xE5,0xA1,0x2B,0x4E,0x3A,0x5C,0x03,0x00,0x6B,0x9E };

enum {
    IDX_SVC = 0,
    IDX_RX_CHAR,
    IDX_RX_VAL,
    IDX_TX_CHAR,
    IDX_TX_VAL,
    IDX_TX_CCC,
    IDX_NB,
};

static uint16_t s_gatt_handle_table[IDX_NB];
static esp_gatt_if_t s_gatts_if = ESP_GATT_IF_NONE;
static uint16_t s_conn_id;
static bool s_connected;
static bool s_notify_enabled;
static bool s_service_started;

#define BLE_SCAN_MAX_DEV 12

typedef struct {
    bool active;
    bool done;
    bool failed;
    int count;
    uint32_t duration_sec;
    struct {
        uint8_t bda[6];
        int8_t rssi;
        char name[32];
    } devs[BLE_SCAN_MAX_DEV];
} ble_scan_ctx_t;

static ble_scan_ctx_t s_ble_scan;
static uint32_t s_pending_scan_sec = 0;
static bool s_scan_mesh_suspended = false;
static esp_timer_handle_t s_scan_delay_timer = NULL;
static esp_timer_handle_t s_adv_stop_watchdog = NULL;
static uint8_t s_scan_start_retries = 0;

#define BLE_SCAN_DELAY_US           (250 * 1000)
#define BLE_SCAN_ADV_STOP_TIMEOUT_US (600 * 1000)

/* 由 frame_coexist 提供强符号；无测试台时为 no-op */
__attribute__((weak)) void frame_coexist_before_ble_scan(void) {}
__attribute__((weak)) void frame_coexist_after_ble_scan(void) {}

static void ble_start_advertising(void);
static void ble_scan_cancel_adv_watchdog(void);
static void ble_scan_schedule_begin(void);

static void ble_scan_finish_restart(bool failed)
{
    if (!s_ble_scan.active && s_ble_scan.done) {
        return;
    }
    ble_scan_cancel_adv_watchdog();
    s_scan_start_retries = 0;
    s_ble_scan.failed = failed;
    s_ble_scan.done = true;
    s_ble_scan.active = false;
    s_pending_scan_sec = 0;
    frame_coexist_after_ble_scan();
#if CONFIG_BLE_MESH
    if (s_scan_mesh_suspended) {
        s_scan_mesh_suspended = false;
        bt_mesh_node_resume_after_ble_link();
    } else
#endif
    {
        ble_start_advertising();
    }
}

static void ble_scan_cancel_adv_watchdog(void)
{
    if (s_adv_stop_watchdog != NULL) {
        (void)esp_timer_stop(s_adv_stop_watchdog);
    }
}

static void ble_adv_stop_watchdog_cb(void *arg)
{
    (void)arg;
    if (!s_ble_scan.active || s_pending_scan_sec == 0) {
        return;
    }
    ESP_LOGW(TAG, "ADV stop timeout, forcing scan");
    s_pending_scan_sec = 0;
    ble_scan_schedule_begin();
}

static void ble_scan_arm_adv_watchdog(void)
{
    if (s_adv_stop_watchdog == NULL) {
        const esp_timer_create_args_t args = {
            .callback = ble_adv_stop_watchdog_cb,
            .name = "ble_adv_wdt",
        };
        if (esp_timer_create(&args, &s_adv_stop_watchdog) != ESP_OK) {
            return;
        }
    }
    (void)esp_timer_stop(s_adv_stop_watchdog);
    (void)esp_timer_start_once(s_adv_stop_watchdog, BLE_SCAN_ADV_STOP_TIMEOUT_US);
}

static esp_err_t ble_scan_begin(void)
{
    /* Do not call stop_scanning first: Bluedroid errors if no scan is active. */

    esp_ble_scan_params_t scan_params = {
        .scan_type = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval = 0x60,
        .scan_window = 0x30,
        .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
    };
    esp_err_t err = esp_ble_gap_set_scan_params(&scan_params);
    if (err != ESP_OK) {
        return err;
    }
    return esp_ble_gap_start_scanning(s_ble_scan.duration_sec > 0 ? s_ble_scan.duration_sec : 3);
}

static void ble_scan_delay_timer_cb(void *arg)
{
    (void)arg;
    if (!s_ble_scan.active) {
        return;
    }
    esp_err_t err = ble_scan_begin();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan begin (delayed): %s", esp_err_to_name(err));
        ble_scan_finish_restart(true);
    }
}

static void ble_scan_schedule_begin(void)
{
    if (s_scan_delay_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = ble_scan_delay_timer_cb,
            .name = "ble_scan_dly",
        };
        if (esp_timer_create(&args, &s_scan_delay_timer) != ESP_OK) {
            esp_err_t err = ble_scan_begin();
            if (err != ESP_OK) {
                ble_scan_finish_restart(true);
            }
            return;
        }
    }
    (void)esp_timer_stop(s_scan_delay_timer);
    (void)esp_timer_start_once(s_scan_delay_timer, BLE_SCAN_DELAY_US);
}

static int ble_scan_find_bda(const uint8_t *bda)
{
    for (int i = 0; i < s_ble_scan.count; i++) {
        if (memcmp(s_ble_scan.devs[i].bda, bda, 6) == 0) {
            return i;
        }
    }
    return -1;
}

static void ble_scan_add_result(const esp_ble_gap_cb_param_t *param)
{
    if (param == NULL) {
        return;
    }
    if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
        if (s_ble_scan.active) {
            ble_scan_finish_restart(false);
        }
        return;
    }
    if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) {
        return;
    }
    const uint8_t *bda = param->scan_rst.bda;
    const int8_t rssi = param->scan_rst.rssi;

    int idx = ble_scan_find_bda(bda);
    if (idx < 0) {
        if (s_ble_scan.count >= BLE_SCAN_MAX_DEV) {
            return;
        }
        idx = s_ble_scan.count++;
        memcpy(s_ble_scan.devs[idx].bda, bda, 6);
        s_ble_scan.devs[idx].name[0] = '\0';
    }
    s_ble_scan.devs[idx].rssi = rssi;

    if (param->scan_rst.adv_data_len > 0) {
        const uint8_t *adv = param->scan_rst.ble_adv;
        uint8_t len = param->scan_rst.adv_data_len;
        uint8_t i = 0;
        while (i + 1 < len) {
            const uint8_t field_len = adv[i];
            if (field_len == 0 || i + field_len >= len) {
                break;
            }
            const uint8_t type = adv[i + 1];
            if (type == ESP_BLE_AD_TYPE_NAME_CMPL || type == ESP_BLE_AD_TYPE_NAME_SHORT) {
                const uint8_t name_len = (uint8_t)(field_len - 1);
                const uint8_t copy = name_len < 31 ? name_len : 31;
                memcpy(s_ble_scan.devs[idx].name, &adv[i + 2], copy);
                s_ble_scan.devs[idx].name[copy] = '\0';
                break;
            }
            i = (uint8_t)(i + field_len + 1);
        }
    }
}

esp_err_t bt_transport_ble_scan_start(uint32_t duration_sec)
{
    if (s_ble_scan.active || s_pending_scan_sec > 0) {
        return ESP_ERR_INVALID_STATE;
    }

    frame_coexist_before_ble_scan();

    memset(&s_ble_scan, 0, sizeof(s_ble_scan));
    s_ble_scan.active = true;
    s_ble_scan.duration_sec = duration_sec > 0 ? duration_sec : 3;
    s_pending_scan_sec = s_ble_scan.duration_sec;
    s_scan_start_retries = 0;
    ble_scan_cancel_adv_watchdog();

#if CONFIG_BLE_MESH
    bt_mesh_node_suspend_for_ble_link();
    s_scan_mesh_suspended = true;
#endif

    esp_err_t err = esp_ble_gap_stop_advertising();
    if (err == ESP_ERR_INVALID_STATE) {
        s_pending_scan_sec = 0;
        ble_scan_schedule_begin();
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ble_scan_finish_restart(true);
        return err;
    }
    ble_scan_arm_adv_watchdog();
    return ESP_OK;
}

bool bt_transport_ble_scan_failed(void)
{
    return s_ble_scan.failed;
}

void bt_transport_ble_scan_abort_if_active(void)
{
    if (!s_ble_scan.active) {
        return;
    }
    if (s_scan_delay_timer != NULL) {
        (void)esp_timer_stop(s_scan_delay_timer);
    }
    ble_scan_cancel_adv_watchdog();
    s_pending_scan_sec = 0;
    esp_err_t err = esp_ble_gap_stop_scanning();
    if (err != ESP_OK) {
        /* Already idle (duration elapsed) — finish without a second stop. */
        ble_scan_finish_restart(false);
    }
}

bool bt_transport_ble_scan_done(void)
{
    return s_ble_scan.done;
}

int bt_transport_ble_scan_count(void)
{
    return s_ble_scan.count;
}

void bt_transport_ble_scan_summary(char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return;
    }
    if (s_ble_scan.count <= 0) {
        snprintf(buf, buf_len, "(无蓝牙设备)");
        return;
    }
    int best = 0;
    for (int i = 1; i < s_ble_scan.count; i++) {
        if (s_ble_scan.devs[i].rssi > s_ble_scan.devs[best].rssi) {
            best = i;
        }
    }
    const char *name = s_ble_scan.devs[best].name[0] ? s_ble_scan.devs[best].name : "未知";
    snprintf(buf, buf_len, "%d 台, 最强 %s %d dBm", s_ble_scan.count, name,
             (int)s_ble_scan.devs[best].rssi);
}
static uint16_t s_mtu = 23;
static esp_bd_addr_t s_remote_bda;

static uint8_t s_tx_value[20];
static uint8_t s_rx_accum[768];
static size_t s_rx_accum_len;
static uint8_t s_btctrl_tx_buf[256];
static uint8_t s_btctrl_body_buf[192];

static esp_err_t ble_send_notify(const uint8_t *data, size_t len);
static void ble_notify_link_status(uint16_t seq);
static void ble_reply_data_ind(uint16_t seq, uint8_t status, const uint8_t *payload, uint16_t payload_len);

static bool ascii_extract_two_u8(const uint8_t *data, size_t len, uint8_t *a, uint8_t *b)
{
    if (!data || len == 0 || !a || !b) return false;
    int found = 0;
    unsigned v[2] = {0, 0};
    size_t i = 0;
    while (i < len && found < 2) {
        while (i < len && (data[i] < '0' || data[i] > '9')) i++;
        if (i >= len) break;
        unsigned acc = 0;
        while (i < len && (data[i] >= '0' && data[i] <= '9')) {
            acc = acc * 10u + (unsigned)(data[i] - '0');
            i++;
        }
        v[found++] = acc;
    }
    if (found < 2) return false;
    if (v[0] > 255 || v[1] > 255) return false;
    *a = (uint8_t)v[0];
    *b = (uint8_t)v[1];
    return true;
}

static void ble_reply_ack(uint16_t seq, uint8_t status)
{
    size_t out_len = 0;
    s_btctrl_body_buf[0] = status;
    if (btctrl_build(BTCTRL_MSG_ACK, seq, 0, s_btctrl_body_buf, 1, s_btctrl_tx_buf, sizeof(s_btctrl_tx_buf), &out_len) == ESP_OK) {
        (void)ble_send_notify(s_btctrl_tx_buf, out_len);
    }
}

static void ble_sync_link_state(void)
{
    bt_gatt_star_set_link(s_connected, s_notify_enabled, s_mtu, s_remote_bda);
}

static void ble_notify_link_status(uint16_t seq)
{
    uint8_t body[12] = {0};
    body[0] = s_connected ? 1 : 0;
    body[1] = s_notify_enabled ? 1 : 0;
    memcpy(&body[2], &s_mtu, sizeof(uint16_t));
    memcpy(&body[4], s_remote_bda, 6);
    body[10] = 0; /* role: peripheral */

    size_t out_len = 0;
    if (btctrl_build(BTCTRL_MSG_LINK_STATUS, seq, 0, body, sizeof(body), s_btctrl_tx_buf, sizeof(s_btctrl_tx_buf), &out_len) == ESP_OK) {
        (void)ble_send_notify(s_btctrl_tx_buf, out_len);
    }
}

static void ble_reply_data_ind(uint16_t seq, uint8_t status, const uint8_t *payload, uint16_t payload_len)
{
    if (payload_len > 180) {
        payload_len = 180;
    }
    s_btctrl_body_buf[0] = status;
    s_btctrl_body_buf[1] = (uint8_t)payload_len;
    if (payload_len && payload) {
        memcpy(&s_btctrl_body_buf[2], payload, payload_len);
    }
    size_t out_len = 0;
    uint16_t blen = (uint16_t)(2 + payload_len);
    if (btctrl_build(BTCTRL_MSG_DATA_IND, seq, 0, s_btctrl_body_buf, blen, s_btctrl_tx_buf, sizeof(s_btctrl_tx_buf), &out_len) == ESP_OK) {
        bt_gatt_star_log_tx(s_btctrl_body_buf, blen);
        (void)ble_send_notify(s_btctrl_tx_buf, out_len);
    }
}

esp_err_t bt_transport_ble_send_notify_payload(const uint8_t *data, size_t len)
{
    return ble_send_notify(data, len);
}

static const uint16_t GATTS_APP_ID = 0x45;

/* Attribute UUIDs (16-bit, LSB first in memory — same pattern as IDF gatts_table_creat_demo) */
static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

#define CHAR_DECLARATION_SIZE (sizeof(uint8_t))

static const uint8_t PROP_WRITE = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static const uint8_t PROP_NOTIFY = ESP_GATT_CHAR_PROP_BIT_NOTIFY;

/* Writable CCC + RX buffer must be non-const for stack updates */
static uint8_t s_ccc_value[2] = { 0x00, 0x00 };
static uint8_t s_rx_value_buf[512];

static const esp_gatts_attr_db_t s_gatt_db[IDX_NB] = {
    /* Service Declaration (Primary Service, 128-bit UUID in value) */
    [IDX_SVC] = {
        { ESP_GATT_AUTO_RSP },
        { ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
          ESP_UUID_LEN_128, ESP_UUID_LEN_128, (uint8_t *)UUID_SVC }
    },

    /* RX Characteristic Declaration */
    [IDX_RX_CHAR] = {
        { ESP_GATT_AUTO_RSP },
        { ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
          CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&PROP_WRITE }
    },
    /* RX Characteristic Value (Write / Write Without Response) */
    [IDX_RX_VAL] = {
        { ESP_GATT_RSP_BY_APP },
        /* Require encrypted link to write control commands (forces pairing/encryption). */
        { ESP_UUID_LEN_128, (uint8_t *)UUID_RX, ESP_GATT_PERM_WRITE_ENCRYPTED,
          sizeof(s_rx_value_buf), 0, s_rx_value_buf }
    },

    /* TX Characteristic Declaration */
    [IDX_TX_CHAR] = {
        { ESP_GATT_AUTO_RSP },
        { ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
          CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&PROP_NOTIFY }
    },
    /* TX Characteristic Value (Notify) */
    [IDX_TX_VAL] = {
        { ESP_GATT_AUTO_RSP },
        { ESP_UUID_LEN_128, (uint8_t *)UUID_TX, ESP_GATT_PERM_READ,
          sizeof(s_tx_value), sizeof(s_tx_value), s_tx_value }
    },
    /* Client Characteristic Configuration (Notify enable) */
    [IDX_TX_CCC] = {
        { ESP_GATT_AUTO_RSP },
        /* CCCD write should also require encryption to prevent unauthenticated subscriptions. */
        { ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE_ENCRYPTED,
          sizeof(uint16_t), sizeof(s_ccc_value), s_ccc_value }
    },
};

static void ble_start_gap_advertising(void)
{
    esp_ble_adv_params_t adv_params = {
        .adv_int_min = 0x20,
        .adv_int_max = 0x40,
        .adv_type = ADV_TYPE_IND,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .channel_map = ADV_CHNL_ALL,
        .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    };
    (void)esp_ble_gap_start_advertising(&adv_params);
}

static void ble_start_advertising(void)
{
#if CONFIG_BLE_MESH
    /* Mesh prov (PB-ADV/GATT) owns the controller adv slot; skip GAP to avoid OOM with WiFi. */
    if (bt_mesh_node_is_unprov_beacon_active()) {
        return;
    }
#endif
    ble_start_gap_advertising();
}

void bt_transport_ble_refresh_advertising(void)
{
    if (s_gatts_if == ESP_GATT_IF_NONE || !s_service_started) {
        return;
    }
    if (s_connected) {
        return;
    }
    ble_start_advertising();
}

static void ble_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "adv data set complete");
        ble_start_advertising();
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param && param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGW(TAG, "adv start failed: %d", param->adv_start_cmpl.status);
        } else {
            ESP_LOGI(TAG, "adv start ok (gap)");
        }
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (param) {
            const esp_ble_auth_cmpl_t *a = &param->ble_security.auth_cmpl;
            if (a->success) {
                ESP_LOGI(TAG, "auth cmpl ok addr_type=%d auth_mode=%u", (int)a->addr_type, (unsigned)a->auth_mode);
            } else {
                ESP_LOGW(TAG, "auth cmpl failed addr_type=%d reason=0x%02x", (int)a->addr_type, a->fail_reason);
            }
        }
        break;
    case ESP_GAP_BLE_KEY_EVT:
        if (param) {
            ESP_LOGI(TAG, "key evt type=%u", (unsigned)param->ble_security.ble_key.key_type);
        }
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        if (param) {
            ESP_LOGI(TAG, "sec req evt; accept");
            (void)esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        }
        break;
    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
        if (param) {
            uint32_t pk = param->ble_security.key_notif.passkey;
            ESP_LOGI(TAG, "passkey notif: %06" PRIu32 " (auto-confirm)", pk);
            (void)esp_ble_passkey_reply(param->ble_security.key_notif.bd_addr, true, pk);
        }
        break;
    case ESP_GAP_BLE_PASSKEY_REQ_EVT:
        ESP_LOGW(TAG, "passkey req (no UI); reject");
        (void)esp_ble_passkey_reply(param->ble_security.ble_req.bd_addr, false, 0);
        break;
    case ESP_GAP_BLE_NC_REQ_EVT:
        if (param) {
            uint32_t pk = param->ble_security.key_notif.passkey;
            ESP_LOGI(TAG, "numeric comparison: %06" PRIu32 " (auto-confirm)", pk);
            (void)esp_ble_confirm_reply(param->ble_security.ble_req.bd_addr, true);
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        ble_scan_cancel_adv_watchdog();
        if (s_pending_scan_sec > 0) {
            s_pending_scan_sec = 0;
            ble_scan_schedule_begin();
        }
        break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (param && param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGW(TAG, "scan start failed: %d (retry=%u)", param->scan_start_cmpl.status,
                     (unsigned)s_scan_start_retries);
            if (s_scan_start_retries < 2) {
                s_scan_start_retries++;
                ble_scan_schedule_begin();
            } else {
                ble_scan_finish_restart(true);
            }
        } else {
            s_scan_start_retries = 0;
        }
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
        ble_scan_add_result(param);
        break;
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        ble_scan_finish_restart(false);
        break;
    default:
        break;
    }
}

static esp_err_t ble_send_notify(const uint8_t *data, size_t len)
{
    if (!s_connected || !s_notify_enabled || s_gatts_if == ESP_GATT_IF_NONE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (len > 512) {
        return ESP_ERR_INVALID_SIZE;
    }
    return esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id, s_gatt_handle_table[IDX_TX_VAL],
                                       (uint16_t)len, (uint8_t *)data, false);
}

static void handle_rx_write(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) {
        return;
    }

    /* Fast-path: allow raw 2-byte SET_STATE (item_id,value) for tool testing. */
    if (len == 2 && data[0] >= 1 && data[0] <= 3) {
        esp_err_t aerr = bt_management_apply_set_state(data[0], data[1]);
        ble_reply_ack(0, (aerr == ESP_OK) ? 0x00 : 0x01);
        return;
    }

    /* Compatibility: accept ASCII commands when user sends strings like:
     * - "1 1"
     * - "sw1=0"
     * - "item:2,val:1"
     */
    if (!(len >= 2 && data[0] == BTCTRL_MAGIC0 && data[1] == BTCTRL_MAGIC1)) {
        uint8_t item_id = 0, value = 0;
        if (ascii_extract_two_u8(data, len, &item_id, &value) && item_id >= 1 && item_id <= 3) {
            ESP_LOGI(TAG, "ascii SET_STATE item=%u value=%u", (unsigned)item_id, (unsigned)value);
            esp_err_t aerr = bt_management_apply_set_state(item_id, value);
            ble_reply_ack(0, (aerr == ESP_OK) ? 0x00 : 0x01);
            return;
        }
    }

    /* Append fragment */
    if (s_rx_accum_len + len > sizeof(s_rx_accum)) {
        s_rx_accum_len = 0; /* drop */
    }
    memcpy(s_rx_accum + s_rx_accum_len, data, len);
    s_rx_accum_len += len;

    /* Parse zero or more frames */
    while (1) {
        if (s_rx_accum_len < sizeof(btctrl_hdr_t)) {
            return;
        }

        /* Try to align to magic */
        if (!(s_rx_accum[0] == BTCTRL_MAGIC0 && s_rx_accum[1] == BTCTRL_MAGIC1)) {
            size_t i;
            for (i = 1; i + 1 < s_rx_accum_len; i++) {
                if (s_rx_accum[i] == BTCTRL_MAGIC0 && s_rx_accum[i + 1] == BTCTRL_MAGIC1) {
                    break;
                }
            }
            if (i + 1 >= s_rx_accum_len) {
                /* keep last byte in case it's magic0 */
                s_rx_accum[0] = s_rx_accum[s_rx_accum_len - 1];
                s_rx_accum_len = 1;
                return;
            }
            memmove(s_rx_accum, s_rx_accum + i, s_rx_accum_len - i);
            s_rx_accum_len -= i;
            if (s_rx_accum_len < sizeof(btctrl_hdr_t)) {
                return;
            }
        }

        btctrl_hdr_t hdr;
        memcpy(&hdr, s_rx_accum, sizeof(hdr));

        if (hdr.ver != BTCTRL_VERSION) {
            /* drop one byte and resync */
            memmove(s_rx_accum, s_rx_accum + 1, s_rx_accum_len - 1);
            s_rx_accum_len -= 1;
            continue;
        }

        size_t total = sizeof(btctrl_hdr_t) + (size_t)hdr.len;
        if (total > sizeof(s_rx_accum)) {
            /* invalid length; resync */
            memmove(s_rx_accum, s_rx_accum + 2, s_rx_accum_len - 2);
            s_rx_accum_len -= 2;
            continue;
        }
        if (s_rx_accum_len < total) {
            return; /* wait more */
        }

        btctrl_frame_view_t f;
        esp_err_t err = btctrl_parse_view(s_rx_accum, total, &f);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "btctrl parse fail: %s", esp_err_to_name(err));
            /* drop magic and try resync */
            memmove(s_rx_accum, s_rx_accum + 2, s_rx_accum_len - 2);
            s_rx_accum_len -= 2;
            continue;
        }

        size_t out_len = 0;

        switch (f.hdr.msg_type) {
    case BTCTRL_MSG_HELLO: {
        /* Body v1 (fixed 20 bytes):
         * u32 cap_mask
         * u16 proto_ver (0x0100)
         * u16 mtu
         * u32 uptime_ms (low 32)
         * u32 free_heap
         * u32 min_free_heap
         */
        uint8_t body[24] = {0};
        uint32_t cap = bt_management_caps_mask();
        uint16_t pver = 0x0100;
        uint16_t mtu = s_mtu;
        uint32_t up = (uint32_t)(esp_timer_get_time() / 1000ULL);
        uint32_t freeh = (uint32_t)esp_get_free_heap_size();
        uint32_t minfree = (uint32_t)esp_get_minimum_free_heap_size();

        memcpy(&body[0], &cap, sizeof(cap));
        memcpy(&body[4], &pver, sizeof(pver));
        memcpy(&body[6], &mtu, sizeof(mtu));
        memcpy(&body[8], &up, sizeof(up));
        memcpy(&body[12], &freeh, sizeof(freeh));
        memcpy(&body[16], &minfree, sizeof(minfree));

        if (btctrl_build(BTCTRL_MSG_HELLO_RSP, f.hdr.seq, 0, body, 20, s_btctrl_tx_buf, sizeof(s_btctrl_tx_buf), &out_len) == ESP_OK) {
            (void)ble_send_notify(s_btctrl_tx_buf, out_len);
        }
        break;
    }
    case BTCTRL_MSG_GET_MESH_STATUS: {
        bt_mesh_status_t st = {0};
        bt_management_mesh_get_status(&st);
        uint16_t peer = 0x0002;
        uint8_t item = 1;
        bt_management_testbench_get_mesh_ui(&peer, &item);
        uint8_t body[14] = {0};
        body[0] = st.provisioned ? 1 : 0;
        body[1] = st.prov_adv_enabled ? 1 : 0;
        body[2] = st.send_ready ? 1 : 0;
        body[3] = item;
        memcpy(&body[4], &st.unicast_addr, sizeof(uint16_t));
        memcpy(&body[6], &peer, sizeof(uint16_t));
        memcpy(&body[8], &st.net_idx, sizeof(uint16_t));
        memcpy(&body[10], &st.app_idx, sizeof(uint16_t));
        if (btctrl_build(BTCTRL_MSG_MESH_STATUS, f.hdr.seq, 0, body, sizeof(body), s_btctrl_tx_buf, sizeof(s_btctrl_tx_buf), &out_len) == ESP_OK) {
            (void)ble_send_notify(s_btctrl_tx_buf, out_len);
        }
        break;
    }
    case BTCTRL_MSG_GET_STATUS: {
        /* For now, return same payload as HELLO_RSP but using HELLO_RSP (simple tooling). */
        uint8_t body[24] = {0};
        uint32_t cap = bt_management_caps_mask();
        uint16_t pver = 0x0100;
        uint16_t mtu = s_mtu;
        uint32_t up = (uint32_t)(esp_timer_get_time() / 1000ULL);
        uint32_t freeh = (uint32_t)esp_get_free_heap_size();
        uint32_t minfree = (uint32_t)esp_get_minimum_free_heap_size();
        memcpy(&body[0], &cap, sizeof(cap));
        memcpy(&body[4], &pver, sizeof(pver));
        memcpy(&body[6], &mtu, sizeof(mtu));
        memcpy(&body[8], &up, sizeof(up));
        memcpy(&body[12], &freeh, sizeof(freeh));
        memcpy(&body[16], &minfree, sizeof(minfree));
        if (btctrl_build(BTCTRL_MSG_HELLO_RSP, f.hdr.seq, 0, body, 20, s_btctrl_tx_buf, sizeof(s_btctrl_tx_buf), &out_len) == ESP_OK) {
            (void)ble_send_notify(s_btctrl_tx_buf, out_len);
        }
        break;
    }
    case BTCTRL_MSG_SET_STATE: {
        /* v1 body: [item_id:u8][value:u8] */
        uint8_t status = 0x02; /* INVALID */
        if (f.hdr.len >= 2) {
            uint8_t item_id = f.body[0];
            uint8_t value = f.body[1];
            esp_err_t aerr = bt_management_apply_set_state(item_id, value);
            status = (aerr == ESP_OK) ? 0x00 : 0x01; /* 0=OK, 1=FAIL */
        }

        uint8_t body[1] = { status };
        if (btctrl_build(BTCTRL_MSG_ACK, f.hdr.seq, 0, body, sizeof(body), s_btctrl_tx_buf, sizeof(s_btctrl_tx_buf), &out_len) == ESP_OK) {
            (void)ble_send_notify(s_btctrl_tx_buf, out_len);
        }
        break;
    }
    case BTCTRL_MSG_GET_LINK: {
        ble_notify_link_status(f.hdr.seq);
        break;
    }
    case BTCTRL_MSG_DATA: {
        uint8_t status = 0x02;
        if (f.hdr.len > 0 && f.body) {
            bt_gatt_star_log_rx(f.body, f.hdr.len);
            status = 0x00;
            ESP_LOGI(TAG, "DATA from central len=%u", (unsigned)f.hdr.len);
        }
        ble_reply_data_ind(f.hdr.seq, status, f.body, f.hdr.len);
        break;
    }
    case BTCTRL_MSG_MESH_SEND: {
        /* v1 body: [dst:u16 LE][item_id:u8][value:u8] — 经已连接节点转发 Mesh SET_STATE */
        uint8_t status = 0x02;
        if (f.hdr.len >= 4) {
            uint16_t dst;
            memcpy(&dst, f.body, sizeof(dst));
            uint8_t item_id = f.body[2];
            uint8_t value = f.body[3];
            esp_err_t merr = bt_management_mesh_send_set_state(dst, item_id, value);
            status = (merr == ESP_OK) ? 0x00 : 0x01;
            ESP_LOGI(TAG, "MESH_SEND dst=0x%04x item=%u val=%u -> %s",
                     (unsigned)dst, (unsigned)item_id, (unsigned)value,
                     esp_err_to_name(merr));
        }
        uint8_t body[1] = { status };
        if (btctrl_build(BTCTRL_MSG_ACK, f.hdr.seq, 0, body, sizeof(body), s_btctrl_tx_buf, sizeof(s_btctrl_tx_buf), &out_len) == ESP_OK) {
            (void)ble_send_notify(s_btctrl_tx_buf, out_len);
        }
        break;
    }
    default: {
        uint16_t code = 0x0002; /* NOT_SUPPORTED */
        uint8_t body[2];
        memcpy(body, &code, sizeof(code));
        if (btctrl_build(BTCTRL_MSG_ERROR_RSP, f.hdr.seq, 0, body, sizeof(body), s_btctrl_tx_buf, sizeof(s_btctrl_tx_buf), &out_len) == ESP_OK) {
            (void)ble_send_notify(s_btctrl_tx_buf, out_len);
        }
        break;
    }
    }

        /* Consume this frame */
        if (s_rx_accum_len > total) {
            memmove(s_rx_accum, s_rx_accum + total, s_rx_accum_len - total);
        }
        s_rx_accum_len -= total;
    }
}

static void gatts_cb(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT: {
        s_gatts_if = gatts_if;
        ESP_LOGI(TAG, "GATTS_REG_EVT: if=%d app_id=%d", (int)gatts_if, (int)param->reg.app_id);

        esp_ble_gap_register_callback(ble_gap_cb);

        esp_ble_adv_data_t adv = {
            .set_scan_rsp = false,
            .include_name = true,
            .include_txpower = false,
            .min_interval = 0x0006,
            .max_interval = 0x0010,
            .appearance = 0x00,
            .manufacturer_len = 0,
            .p_manufacturer_data = NULL,
            .service_data_len = 0,
            .p_service_data = NULL,
            .service_uuid_len = sizeof(UUID_SVC),
            .p_service_uuid = (uint8_t *)UUID_SVC,
            .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
        };
        (void)esp_ble_gap_config_adv_data(&adv);

        (void)esp_ble_gatts_create_attr_tab(s_gatt_db, gatts_if, IDX_NB, 0);
        break;
    }
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status == ESP_GATT_OK) {
            memcpy(s_gatt_handle_table, param->add_attr_tab.handles, sizeof(s_gatt_handle_table));
            (void)esp_ble_gatts_start_service(s_gatt_handle_table[IDX_SVC]);
            s_service_started = 1;
            ESP_LOGI(TAG, "attr tab ok: svc=%u rx=%u tx=%u ccc=%u",
                     (unsigned)s_gatt_handle_table[IDX_SVC],
                     (unsigned)s_gatt_handle_table[IDX_RX_VAL],
                     (unsigned)s_gatt_handle_table[IDX_TX_VAL],
                     (unsigned)s_gatt_handle_table[IDX_TX_CCC]);
            bt_management_on_ble_gatt_ready();
        } else {
            ESP_LOGE(TAG, "create attr tab failed: %d", param->add_attr_tab.status);
        }
        break;
    case ESP_GATTS_CONNECT_EVT:
        s_connected = true;
        s_conn_id = param->connect.conn_id;
        s_notify_enabled = false;
        s_mtu = 23;
        s_rx_accum_len = 0;
        memcpy(s_remote_bda, param->connect.remote_bda, sizeof(s_remote_bda));
        ble_sync_link_state();
        ESP_LOGI(TAG, "connect: conn_id=%u", (unsigned)s_conn_id);
#if CONFIG_BLE_MESH
        bt_mesh_node_suspend_for_ble_link();
#endif
        (void)esp_ble_set_encryption(s_remote_bda, ESP_BLE_SEC_ENCRYPT_NO_MITM);
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        s_connected = false;
        s_notify_enabled = false;
        s_rx_accum_len = 0;
        ble_sync_link_state();
        ESP_LOGI(TAG, "disconnect; restart adv");
#if CONFIG_BLE_MESH
        bt_mesh_node_resume_after_ble_link();
#else
        ble_start_advertising();
#endif
        break;
    case ESP_GATTS_MTU_EVT:
        s_mtu = param->mtu.mtu;
        ESP_LOGI(TAG, "mtu=%u", (unsigned)s_mtu);
        break;
    case ESP_GATTS_WRITE_EVT:
        if (!param->write.is_prep) {
            ESP_LOGI(TAG, "write: handle=%u len=%u need_rsp=%d",
                     (unsigned)param->write.handle, (unsigned)param->write.len, (int)param->write.need_rsp);
            if (param->write.handle == s_gatt_handle_table[IDX_TX_CCC] && param->write.len == 2) {
                uint16_t v = (uint16_t)(param->write.value[1] << 8) | param->write.value[0];
                s_notify_enabled = (v == 0x0001);
                ble_sync_link_state();
                ESP_LOGI(TAG, "cccd=%04x notify=%d", (unsigned)v, (int)s_notify_enabled);
                if (s_notify_enabled) {
                    ble_notify_link_status(0);
                }
            } else if (param->write.handle == s_gatt_handle_table[IDX_RX_VAL]) {
                if (param->write.len >= 2) {
                    ESP_LOGI(TAG, "rx first bytes: %02x %02x", param->write.value[0], param->write.value[1]);
                }
                handle_rx_write(param->write.value, param->write.len);
                /* respond OK if client requested response */
                if (param->write.need_rsp) {
                    esp_gatt_rsp_t rsp = {0};
                    rsp.attr_value.handle = param->write.handle;
                    rsp.attr_value.len = 0;
                    (void)esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id,
                                                      ESP_GATT_OK, &rsp);
                }
            } else {
                ESP_LOGW(TAG, "write to unknown handle=%u", (unsigned)param->write.handle);
            }
        }
        break;
    default:
        break;
    }
}

esp_err_t bt_transport_ble_start(uint16_t mtu)
{
    s_mtu = mtu ? mtu : 23;

    /* Request local MTU (best-effort) */
    (void)esp_ble_gatt_set_local_mtu(s_mtu);

    esp_err_t ret = esp_ble_gatts_register_callback(gatts_cb);
    ESP_RETURN_ON_ERROR(ret, TAG, "gatts cb");
    ret = esp_ble_gatts_app_register(GATTS_APP_ID);
    ESP_RETURN_ON_ERROR(ret, TAG, "app reg");
    return ESP_OK;
}

esp_err_t bt_transport_ble_stop(void)
{
    if (s_service_started && s_gatts_if != ESP_GATT_IF_NONE) {
        (void)esp_ble_gatts_stop_service(s_gatt_handle_table[IDX_SVC]);
    }
    return ESP_OK;
}

#else

void bt_transport_ble_refresh_advertising(void)
{
}

esp_err_t bt_transport_ble_scan_start(uint32_t duration_sec)
{
    (void)duration_sec;
    return ESP_ERR_NOT_SUPPORTED;
}

bool bt_transport_ble_scan_done(void)
{
    return true;
}

bool bt_transport_ble_scan_failed(void)
{
    return false;
}

void bt_transport_ble_scan_abort_if_active(void)
{
}

int bt_transport_ble_scan_count(void)
{
    return 0;
}

void bt_transport_ble_scan_summary(char *buf, size_t buf_len)
{
    if (buf && buf_len) {
        snprintf(buf, buf_len, "(未启用 BT)");
    }
}

esp_err_t bt_transport_ble_start(uint16_t mtu)
{
    (void)mtu;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bt_transport_ble_stop(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

#endif

