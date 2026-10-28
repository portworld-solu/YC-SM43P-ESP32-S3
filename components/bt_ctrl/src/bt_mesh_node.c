#include "bt_management.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>

#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"

#if defined(CONFIG_BT_ENABLED)

static const char *TAG = "bt_mesh";

static bool s_started;
static bool s_prov_enabled = false;
static bool s_ble_link_suspended;
static bool s_prov_before_suspend;
static bool s_proxy_gatt_was_enabled;

#if CONFIG_BLE_MESH && CONFIG_BLE_MESH_NODE

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "esp_ble_mesh_proxy_api.h"

#ifndef CID_ESP
#define CID_ESP 0x02E5
#endif

#define VND_MODEL_ID_SERVER 0x0001
#define VND_MODEL_ID_CLIENT 0x0002

#define VND_OP_SET_STATE ESP_BLE_MESH_MODEL_OP_3(0x01, CID_ESP)
#define VND_OP_STATUS    ESP_BLE_MESH_MODEL_OP_3(0x02, CID_ESP)

typedef struct {
    bool provisioned;
    uint16_t net_idx;
    uint16_t unicast_addr;
    uint16_t app_idx;
    bool app_key_ready;
} mesh_node_state_t;

static mesh_node_state_t s_mesh;

static esp_ble_mesh_cfg_srv_t s_cfg_srv = {
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
    .default_ttl = 7,
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
};

static esp_ble_mesh_model_op_t s_vnd_srv_op[] = {
    ESP_BLE_MESH_MODEL_OP(VND_OP_SET_STATE, 2),
    ESP_BLE_MESH_MODEL_OP_END,
};

static const esp_ble_mesh_client_op_pair_t s_vnd_cli_op_pair[] = {
    { VND_OP_SET_STATE, VND_OP_STATUS },
};

static esp_ble_mesh_client_t s_vnd_client = {
    .op_pair_size = ARRAY_SIZE(s_vnd_cli_op_pair),
    .op_pair = (esp_ble_mesh_client_op_pair_t *)s_vnd_cli_op_pair,
};

static esp_ble_mesh_model_op_t s_vnd_cli_op[] = {
    ESP_BLE_MESH_MODEL_OP(VND_OP_STATUS, 2),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t s_sig_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&s_cfg_srv),
};

static esp_ble_mesh_model_t s_vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, VND_MODEL_ID_SERVER, s_vnd_srv_op, NULL, NULL),
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, VND_MODEL_ID_CLIENT, s_vnd_cli_op, NULL, &s_vnd_client),
};

static esp_ble_mesh_elem_t s_elements[] = {
    ESP_BLE_MESH_ELEMENT(0, s_sig_models, s_vnd_models),
};

static esp_ble_mesh_comp_t s_comp = {
    .cid = CID_ESP,
    .pid = 0x0000,
    .vid = 0x0001,
    .element_count = ARRAY_SIZE(s_elements),
    .elements = s_elements,
};

static uint8_t s_dev_uuid[16] =
    { 0x4C, 0x56, 0x47, 0x4C, 0x46, 0x52, 0x41, 0x4D, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };

static esp_ble_mesh_prov_t s_prov = {
    .uuid = s_dev_uuid,
    .oob_info = 0,
};

static esp_ble_mesh_prov_bearer_t mesh_prov_bearers(void)
{
    esp_ble_mesh_prov_bearer_t bearers = ESP_BLE_MESH_PROV_ADV;
#if defined(CONFIG_BLE_MESH_PB_GATT) && CONFIG_BLE_MESH_PB_GATT
    bearers |= ESP_BLE_MESH_PROV_GATT;
#endif
    return bearers;
}

static void mesh_refresh_runtime_state(void)
{
    s_mesh.provisioned = esp_ble_mesh_node_is_provisioned();
    if (s_mesh.provisioned) {
        s_mesh.unicast_addr = esp_ble_mesh_get_primary_element_address();
        if (esp_ble_mesh_node_get_local_app_key(s_mesh.app_idx) != NULL) {
            s_mesh.app_key_ready = true;
        }
    }
}

static void mesh_uuid_to_hex(char *out, size_t out_len)
{
    if (out == NULL || out_len < 33) {
        return;
    }
    for (size_t i = 0; i < 16; i++) {
        snprintf(out + i * 2, 3, "%02X", s_dev_uuid[i]);
    }
}

static void mesh_bind_client_app_key(uint16_t elem_addr, uint16_t app_idx)
{
    esp_err_t err = esp_ble_mesh_node_bind_app_key_to_local_model(
        elem_addr, CID_ESP, VND_MODEL_ID_CLIENT, app_idx);
    if (err == ESP_OK) {
        s_mesh.app_idx = app_idx;
        s_mesh.app_key_ready = true;
        ESP_LOGI(TAG, "client model bound app_idx=0x%04x", app_idx);
    } else {
        ESP_LOGW(TAG, "client bind failed: %s", esp_err_to_name(err));
    }
}

static void mesh_bind_vendor_models(uint16_t elem_addr, uint16_t app_idx)
{
    esp_err_t err = esp_ble_mesh_node_bind_app_key_to_local_model(
        elem_addr, CID_ESP, VND_MODEL_ID_SERVER, app_idx);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "server model bound app_idx=0x%04x", app_idx);
    }
    mesh_bind_client_app_key(elem_addr, app_idx);
}

static void mesh_on_provisioned(uint16_t net_idx, uint16_t addr)
{
    s_mesh.provisioned = true;
    s_mesh.net_idx = net_idx;
    s_mesh.unicast_addr = addr;
    s_prov_enabled = false;
    (void)esp_ble_mesh_node_prov_disable(mesh_prov_bearers());
    ESP_LOGI(TAG, "prov done: addr=0x%04x — configure AppKey + bind vendor models in provisioner", addr);
}

static void mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                  esp_ble_mesh_cfg_server_cb_param_t *param)
{
    if (event != ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT || param == NULL) {
        return;
    }

    switch (param->ctx.recv_op) {
    case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
        s_mesh.net_idx = param->value.state_change.appkey_add.net_idx;
        s_mesh.app_idx = param->value.state_change.appkey_add.app_idx;
        ESP_LOGI(TAG, "app key add net=0x%04x app=0x%04x",
                 s_mesh.net_idx, s_mesh.app_idx);
        if (s_mesh.provisioned && s_mesh.unicast_addr != 0) {
            mesh_bind_vendor_models(s_mesh.unicast_addr, s_mesh.app_idx);
        }
        break;
    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND: {
        const esp_ble_mesh_state_change_cfg_model_app_bind_t *bind =
            &param->value.state_change.mod_app_bind;
        ESP_LOGI(TAG, "model app bind elem=0x%04x cid=0x%04x mod=0x%04x app=0x%04x",
                 bind->element_addr, bind->company_id, bind->model_id, bind->app_idx);
        if (bind->company_id != CID_ESP) {
            break;
        }
        s_mesh.app_idx = bind->app_idx;
        if (bind->model_id == VND_MODEL_ID_SERVER || bind->model_id == VND_MODEL_ID_CLIENT) {
            s_mesh.app_key_ready = true;
            mesh_bind_client_app_key(bind->element_addr, bind->app_idx);
        }
        break;
    }
    default:
        break;
    }
}

static void mesh_prov_cb(esp_ble_mesh_prov_cb_event_t event, esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "prov register: status=%d", param->prov_register_comp.err_code);
        if (param->prov_register_comp.err_code == 0) {
            (void)esp_ble_mesh_set_unprovisioned_device_name("esp32_s3_frame");
        }
        break;
    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "prov enable: status=%d", param->node_prov_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_DISABLE_COMP_EVT:
        ESP_LOGI(TAG, "prov disable: status=%d", param->node_prov_disable_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        mesh_on_provisioned(param->node_prov_complete.net_idx, param->node_prov_complete.addr);
        ESP_LOGI(TAG, "prov complete: net_idx=0x%x addr=0x%04x iv=0x%08" PRIx32,
                 param->node_prov_complete.net_idx, param->node_prov_complete.addr,
                 param->node_prov_complete.iv_index);
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGW(TAG, "prov reset");
        memset(&s_mesh, 0, sizeof(s_mesh));
        break;
    default:
        break;
    }
}

static void mesh_model_cb(esp_ble_mesh_model_cb_event_t event, esp_ble_mesh_model_cb_param_t *param)
{
    if (param == NULL) {
        return;
    }

    switch (event) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT: {
        const struct ble_mesh_model_operation_evt_param *op = &param->model_operation;
        if (op->opcode == VND_OP_SET_STATE && op->length >= 2 && op->msg) {
            uint8_t item_id = op->msg[0];
            uint8_t value = op->msg[1];
            ESP_LOGI(TAG, "VND SET_STATE item=%u val=%u from 0x%04x",
                     (unsigned)item_id, (unsigned)value, op->ctx ? op->ctx->addr : 0);
            (void)bt_management_apply_set_state(item_id, value);

            uint8_t status[2] = { item_id, value };
            (void)esp_ble_mesh_server_model_send_msg(op->model, op->ctx, VND_OP_STATUS,
                                                     sizeof(status), status);
        }
        break;
    }
    case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
        if (param->model_send_comp.err_code == 0) {
            ESP_LOGI(TAG, "send complete opcode=0x%06" PRIx32, param->model_send_comp.opcode);
        } else {
            ESP_LOGW(TAG, "send failed opcode=0x%06" PRIx32 " err=%d",
                     param->model_send_comp.opcode, param->model_send_comp.err_code);
        }
        break;
    case ESP_BLE_MESH_CLIENT_MODEL_RECV_PUBLISH_MSG_EVT:
        ESP_LOGI(TAG, "client recv opcode=0x%06" PRIx32, param->client_recv_publish_msg.opcode);
        break;
    default:
        break;
    }
}

#endif /* CONFIG_BLE_MESH && CONFIG_BLE_MESH_NODE */

esp_err_t bt_mesh_node_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

#if !CONFIG_BLE_MESH || !CONFIG_BLE_MESH_NODE
    ESP_LOGW(TAG, "BLE Mesh node not enabled in sdkconfig; skip");
    return ESP_ERR_NOT_SUPPORTED;
#else
    esp_err_t err;
    err = esp_ble_mesh_register_prov_callback(mesh_prov_cb);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_ble_mesh_register_config_server_callback(mesh_config_server_cb);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_ble_mesh_register_custom_model_callback(mesh_model_cb);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_ble_mesh_init(&s_prov, &s_comp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ble_mesh_init failed: %s", esp_err_to_name(err));
        return err;
    }

    mesh_refresh_runtime_state();
    if (s_mesh.provisioned) {
        ESP_LOGI(TAG, "restored mesh node addr=0x%04x", s_mesh.unicast_addr);
    }

    if (s_prov_enabled && !s_mesh.provisioned) {
        err = esp_ble_mesh_node_prov_enable(mesh_prov_bearers());
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "prov_enable failed: %s", esp_err_to_name(err));
        }
    }

    s_started = true;
    return ESP_OK;
#endif
}

esp_err_t bt_mesh_node_stop(void)
{
    if (!s_started) {
        return ESP_OK;
    }
#if defined(CONFIG_BLE_MESH_DEINIT)
    esp_ble_mesh_deinit_param_t p = {0};
    (void)esp_ble_mesh_deinit(&p);
#endif
    s_started = false;
    return ESP_OK;
}

esp_err_t bt_mesh_node_set_prov_enable(bool enable)
{
    s_prov_enabled = enable;
#if CONFIG_BLE_MESH && CONFIG_BLE_MESH_NODE
    esp_err_t err = ESP_OK;
    if (!s_started || s_ble_link_suspended) {
        return ESP_OK;
    }
    if (esp_ble_mesh_node_is_provisioned()) {
        return ESP_OK;
    }
    if (enable) {
        err = esp_ble_mesh_node_prov_enable(mesh_prov_bearers());
    } else {
        err = esp_ble_mesh_node_prov_disable(mesh_prov_bearers());
        bt_management_ble_refresh_advertising();
    }
    return err;
#else
    (void)enable;
    return ESP_OK;
#endif
}

void bt_mesh_node_suspend_for_ble_link(void)
{
#if CONFIG_BLE_MESH && CONFIG_BLE_MESH_NODE
    if (!s_started || s_ble_link_suspended) {
        return;
    }
    s_ble_link_suspended = true;
    s_prov_before_suspend = s_prov_enabled;
    s_proxy_gatt_was_enabled = false;

    if (!esp_ble_mesh_node_is_provisioned()) {
        esp_err_t err = esp_ble_mesh_node_prov_disable(mesh_prov_bearers());
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "prov disable for BLE scan: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "mesh prov paused for BLE scan");
        }
    } else {
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER) && CONFIG_BLE_MESH_GATT_PROXY_SERVER
        esp_err_t err = esp_ble_mesh_proxy_gatt_disable();
        if (err == ESP_OK) {
            s_proxy_gatt_was_enabled = true;
            ESP_LOGI(TAG, "mesh GATT proxy paused for BLE scan");
        } else if (err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "proxy_gatt_disable: %s", esp_err_to_name(err));
        }
#endif
    }
#endif
}

void bt_mesh_node_resume_after_ble_link(void)
{
#if CONFIG_BLE_MESH && CONFIG_BLE_MESH_NODE
    if (!s_started || !s_ble_link_suspended) {
        return;
    }
    s_ble_link_suspended = false;

    if (esp_ble_mesh_node_is_provisioned()) {
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER) && CONFIG_BLE_MESH_GATT_PROXY_SERVER
        if (s_proxy_gatt_was_enabled) {
            esp_err_t err = esp_ble_mesh_proxy_gatt_enable();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "proxy_gatt_enable restore: %s", esp_err_to_name(err));
            } else {
                ESP_LOGI(TAG, "mesh GATT proxy restored");
            }
        }
#endif
        s_proxy_gatt_was_enabled = false;
    } else if (s_prov_enabled) {
        esp_err_t err = esp_ble_mesh_node_prov_enable(mesh_prov_bearers());
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "prov restore after BLE scan: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "mesh prov restored after BLE scan");
        }
    } else {
        bt_management_ble_refresh_advertising();
    }
    s_prov_before_suspend = false;
#endif
}

bool bt_mesh_node_is_unprov_beacon_active(void)
{
#if CONFIG_BLE_MESH && CONFIG_BLE_MESH_NODE
    return s_started && s_prov_enabled && !esp_ble_mesh_node_is_provisioned();
#else
    return false;
#endif
}

esp_err_t bt_mesh_node_factory_reset(void)
{
#if CONFIG_BLE_MESH && CONFIG_BLE_MESH_NODE
    if (s_started) {
        esp_ble_mesh_node_local_reset();
    }
    memset(&s_mesh, 0, sizeof(s_mesh));
#endif
    return ESP_OK;
}

bool bt_mesh_node_get_prov_enable_flag(void)
{
#if CONFIG_BLE_MESH && CONFIG_BLE_MESH_NODE
    return s_prov_enabled;
#else
    return false;
#endif
}

void bt_mesh_node_get_status(bt_mesh_status_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
#if CONFIG_BLE_MESH && CONFIG_BLE_MESH_NODE
    mesh_refresh_runtime_state();
    out->provisioned = s_mesh.provisioned;
    out->prov_adv_enabled = s_prov_enabled;
    out->net_idx = s_mesh.net_idx;
    out->unicast_addr = s_mesh.unicast_addr;
    out->app_idx = s_mesh.app_idx;
    out->send_ready = s_mesh.provisioned && s_mesh.app_key_ready;
    mesh_uuid_to_hex(out->device_uuid, sizeof(out->device_uuid));
#endif
}

esp_err_t bt_mesh_node_send_set_state(uint16_t dst_addr, uint8_t item_id, uint8_t value)
{
#if !CONFIG_BLE_MESH || !CONFIG_BLE_MESH_NODE
    (void)dst_addr;
    (void)item_id;
    (void)value;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!esp_ble_mesh_node_is_provisioned()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (dst_addr == 0 || dst_addr == ESP_BLE_MESH_ADDR_UNASSIGNED) {
        return ESP_ERR_INVALID_ARG;
    }

    mesh_refresh_runtime_state();
    if (!s_mesh.app_key_ready && esp_ble_mesh_node_get_local_app_key(s_mesh.app_idx) == NULL) {
        ESP_LOGW(TAG, "app key not ready; bind vendor client in provisioner first");
        return ESP_ERR_INVALID_STATE;
    }

    esp_ble_mesh_msg_ctx_t ctx = {0};
    ctx.net_idx = s_mesh.net_idx;
    ctx.app_idx = s_mesh.app_idx;
    ctx.addr = dst_addr;
    ctx.send_ttl = 5;

    uint8_t payload[2] = { item_id, value };
    esp_ble_mesh_model_t *cli_model = &s_vnd_models[1];
    esp_err_t err = esp_ble_mesh_client_model_send_msg(cli_model, &ctx, VND_OP_SET_STATE,
                                                       sizeof(payload), payload, 0, false,
                                                       ROLE_NODE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "send SET_STATE failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "send SET_STATE dst=0x%04x item=%u val=%u", dst_addr,
                 (unsigned)item_id, (unsigned)value);
    }
    return err;
#endif
}

#else

bool bt_mesh_node_is_unprov_beacon_active(void)
{
    return false;
}

esp_err_t bt_mesh_node_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bt_mesh_node_stop(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bt_mesh_node_set_prov_enable(bool enable)
{
    (void)enable;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bt_mesh_node_factory_reset(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

bool bt_mesh_node_get_prov_enable_flag(void)
{
    return false;
}

void bt_mesh_node_get_status(bt_mesh_status_t *out)
{
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
}

esp_err_t bt_mesh_node_send_set_state(uint16_t dst_addr, uint8_t item_id, uint8_t value)
{
    (void)dst_addr;
    (void)item_id;
    (void)value;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
