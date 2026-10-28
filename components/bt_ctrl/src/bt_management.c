#include "bt_management.h"

#include <string.h>

#include <stdio.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

/* Forward decls for submodules (kept internal for now). */
esp_err_t bt_transport_ble_start(uint16_t mtu);
esp_err_t bt_transport_ble_stop(void);
void bt_transport_ble_refresh_advertising(void);
esp_err_t bt_transport_ble_scan_start(uint32_t duration_sec);
bool bt_transport_ble_scan_done(void);
bool bt_transport_ble_scan_failed(void);
int bt_transport_ble_scan_count(void);
void bt_transport_ble_scan_summary(char *buf, size_t buf_len);
void bt_transport_ble_scan_abort_if_active(void);
esp_err_t bt_transport_spp_start(void);
esp_err_t bt_transport_spp_stop(void);
esp_err_t bt_mesh_node_start(void);
esp_err_t bt_mesh_node_stop(void);
esp_err_t bt_mesh_node_set_prov_enable(bool enable);
void bt_mesh_node_suspend_for_ble_link(void);
void bt_mesh_node_resume_after_ble_link(void);
esp_err_t bt_mesh_node_factory_reset(void);
bool bt_mesh_node_get_prov_enable_flag(void);
bool bt_mesh_node_is_unprov_beacon_active(void);
void bt_mesh_node_get_status(bt_mesh_status_t *out);
esp_err_t bt_mesh_node_send_set_state(uint16_t dst_addr, uint8_t item_id, uint8_t value);

static const char *TAG = "bt_mgmt";

static bt_mgmt_config_t s_cfg;
static bool s_inited;
static bool s_started;
static bool s_mesh_defer_pending;

/* From main/bt_switch_control.c (no-op). Ensures strong set_state handler is linked. */
void bt_switch_control_linker_keep(void);

#if defined(CONFIG_BT_ENABLED)

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "soc/soc_caps.h"

static esp_err_t bt_stack_start(void)
{
    esp_err_t ret;

    /* Controller */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    ESP_RETURN_ON_ERROR(ret, TAG, "controller init");

    /* ESP32-S3: BLE only (no Classic BR/EDR). */
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    ESP_RETURN_ON_ERROR(ret, TAG, "controller enable");

    /* Bluedroid host */
    ret = esp_bluedroid_init();
    ESP_RETURN_ON_ERROR(ret, TAG, "bluedroid init");
    ret = esp_bluedroid_enable();
    ESP_RETURN_ON_ERROR(ret, TAG, "bluedroid enable");

    /* Device name */
    const char *name = (s_cfg.device_name && s_cfg.device_name[0]) ? s_cfg.device_name : "lvglframe";
    (void)esp_bt_dev_set_device_name(name);

    /* BLE security: Secure Connections + bonding, Just Works (no MITM). */
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key  = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;

    (void)esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(auth_req));
    (void)esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap));
    (void)esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size));
    (void)esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(init_key));
    (void)esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(rsp_key));

    return ESP_OK;
}

static esp_err_t bt_stack_stop(void)
{
    /* Best-effort teardown; many products never call stop(). */
    (void)esp_bluedroid_disable();
    (void)esp_bluedroid_deinit();
    (void)esp_bt_controller_disable();
    (void)esp_bt_controller_deinit();
    return ESP_OK;
}

#else

static esp_err_t bt_stack_start(void) { return ESP_ERR_NOT_SUPPORTED; }
static esp_err_t bt_stack_stop(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif

esp_err_t bt_management_init(const bt_mgmt_config_t *cfg)
{
    if (s_inited) {
        return ESP_OK;
    }
    if (cfg) {
        s_cfg = *cfg;
    } else {
        memset(&s_cfg, 0, sizeof(s_cfg));
        s_cfg.enabled_ble = true;
        s_cfg.enabled_bredr = false;
        s_cfg.enabled_mesh = true;
        s_cfg.enable_pairing_ui = false;
        s_cfg.single_controller_lock = true;
        s_cfg.ble_mtu = 185;
        s_cfg.device_name = NULL;
    }
    s_inited = true;
    return ESP_OK;
}

esp_err_t bt_management_start(void)
{
    ESP_RETURN_ON_FALSE(s_inited, ESP_ERR_INVALID_STATE, TAG, "not inited");
    if (s_started) {
        return ESP_OK;
    }

    /* Pull in optional app-side handlers (e.g. switch control) */
    bt_switch_control_linker_keep();

#if defined(CONFIG_BT_ENABLED) && CONFIG_BT_ENABLED
    {
        size_t free_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t largest_int = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "heap before BT stack: int=%u largest_int=%u",
                 (unsigned)free_int, (unsigned)largest_int);
    }
#endif

    esp_err_t ret = bt_stack_start();
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Bluetooth disabled in sdkconfig; skipping start");
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "stack");

    if (s_cfg.enabled_ble) {
        ret = bt_transport_ble_start(s_cfg.ble_mtu);
        if (ret != ESP_OK) ESP_LOGW(TAG, "BLE start skipped: %s", esp_err_to_name(ret));
    }

    if (s_cfg.enabled_bredr) {
#if SOC_BT_CLASSIC_SUPPORTED
        ret = bt_transport_spp_start();
        if (ret != ESP_OK) ESP_LOGW(TAG, "SPP start skipped: %s", esp_err_to_name(ret));
#else
        ESP_LOGW(TAG, "Classic BT/SPP not supported on this target; skipping");
#endif
    }

    if (s_cfg.enabled_mesh) {
        /* Mesh init 与 GATT 注册同在 BTC_TASK，延后到服务启动完成后再执行 */
        s_mesh_defer_pending = true;
    } else if (s_cfg.enabled_ble) {
        bt_transport_ble_refresh_advertising();
    }

    s_started = true;
    return ESP_OK;
}

void bt_management_on_ble_gatt_ready(void)
{
    if (!s_started) {
        return;
    }
    if (s_mesh_defer_pending && s_cfg.enabled_mesh) {
        s_mesh_defer_pending = false;
        esp_err_t ret = bt_mesh_node_start();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Mesh start skipped: %s", esp_err_to_name(ret));
        }
    }
    if (s_cfg.enabled_ble) {
        bt_transport_ble_refresh_advertising();
    }
}

esp_err_t bt_management_stop(void)
{
    if (!s_started) {
        return ESP_OK;
    }

    (void)bt_mesh_node_stop();
    (void)bt_transport_spp_stop();
    (void)bt_transport_ble_stop();
    (void)bt_stack_stop();

    s_started = false;
    return ESP_OK;
}

esp_err_t bt_management_local_submit_command(const uint8_t *buf, size_t len)
{
    (void)buf;
    (void)len;
    /* Next step: route into bt_proto + apply device control + respond to active link. */
    return ESP_OK;
}

esp_err_t bt_management_mesh_set_prov_enable(bool enable)
{
    return bt_mesh_node_set_prov_enable(enable);
}

bool bt_management_mesh_prov_is_enabled(void)
{
    return bt_mesh_node_get_prov_enable_flag();
}

void bt_management_mesh_get_status(bt_mesh_status_t *out)
{
    bt_mesh_node_get_status(out);
}

esp_err_t bt_management_mesh_send_set_state(uint16_t dst_addr, uint8_t item_id, uint8_t value)
{
    return bt_mesh_node_send_set_state(dst_addr, item_id, value);
}

void bt_management_ble_refresh_advertising(void)
{
    bt_transport_ble_refresh_advertising();
}

esp_err_t bt_management_factory_reset(void)
{
    /* Next step: clear bonds + app auth material + mesh state. */
    return bt_mesh_node_factory_reset();
}

uint32_t bt_management_caps_mask(void)
{
    uint32_t cap = 0;
    if (s_cfg.enabled_ble) cap |= 1u << 0;
#if SOC_BT_CLASSIC_SUPPORTED
    if (s_cfg.enabled_bredr) cap |= 1u << 1;
#endif
    if (s_cfg.enabled_mesh) cap |= 1u << 2;
    return cap;
}

__attribute__((weak)) esp_err_t bt_management_apply_set_state(uint8_t item_id, uint8_t value)
{
    ESP_LOGI(TAG, "SET_STATE item=%u value=%u (weak handler)", (unsigned)item_id, (unsigned)value);
    return ESP_OK;
}

__attribute__((weak)) void bt_management_testbench_get_mesh_ui(uint16_t *peer_addr, uint8_t *item_id)
{
    if (peer_addr) {
        *peer_addr = 0x0002;
    }
    if (item_id) {
        *item_id = 1;
    }
}

#if defined(CONFIG_BT_ENABLED)
esp_err_t bt_transport_ble_send_notify_payload(const uint8_t *data, size_t len);
#endif

void bt_management_gatt_get_link(bt_gatt_star_link_t *out)
{
    bt_gatt_star_get_link(out);
}

esp_err_t bt_management_gatt_send_data(const uint8_t *data, size_t len)
{
#if defined(CONFIG_BT_ENABLED)
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    bt_gatt_star_log_tx(data, len);
    return bt_transport_ble_send_notify_payload(data, len);
#else
    (void)data;
    (void)len;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

unsigned bt_management_gatt_log_count(void)
{
    return bt_gatt_star_log_count();
}

void bt_management_gatt_format_log(char *buf, size_t buf_len, unsigned max_lines)
{
    bt_gatt_star_format_log(buf, buf_len, max_lines);
}

void bt_management_gatt_log_clear(void)
{
    bt_gatt_star_log_clear();
}

esp_err_t bt_management_ble_scan_start(uint32_t duration_sec)
{
#if defined(CONFIG_BT_ENABLED)
    return bt_transport_ble_scan_start(duration_sec);
#else
    (void)duration_sec;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool bt_management_ble_scan_done(void)
{
#if defined(CONFIG_BT_ENABLED)
    return bt_transport_ble_scan_done();
#else
    return true;
#endif
}

bool bt_management_ble_scan_failed(void)
{
#if defined(CONFIG_BT_ENABLED)
    return bt_transport_ble_scan_failed();
#else
    return false;
#endif
}

int bt_management_ble_scan_count(void)
{
#if defined(CONFIG_BT_ENABLED)
    return bt_transport_ble_scan_count();
#else
    return 0;
#endif
}

void bt_management_ble_scan_summary(char *buf, size_t buf_len)
{
#if defined(CONFIG_BT_ENABLED)
    bt_transport_ble_scan_summary(buf, buf_len);
#else
    if (buf && buf_len) {
        snprintf(buf, buf_len, "(未启用 BT)");
    }
#endif
}

void bt_management_ble_scan_abort_if_active(void)
{
#if defined(CONFIG_BT_ENABLED)
    bt_transport_ble_scan_abort_if_active();
#else
#endif
}

