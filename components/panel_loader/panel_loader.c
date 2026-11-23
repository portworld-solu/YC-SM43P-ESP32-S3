/*
 * Panel OTA Loader - reusable ESP-IDF 5.5+ component.
 * Local LAN administration with deliberately NO authentication.
 * Never publish TCP port 80 to the Internet or an untrusted guest VLAN.
 */
#include "panel_loader.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#define UPLOAD_CHUNK 4096U
#define JSON_BODY_LIMIT 1024U
#define MQTT_HOST_MAX 128U
#define MQTT_USER_MAX 64U
#define MQTT_PASS_MAX 128U
#define MQTT_PANEL_MAX 48U

static const char *TAG = "panel_loader";
static httpd_handle_t s_httpd;
static char s_mqtt_ns[16] = "panel_mqtt";
static bool s_ota_busy;

extern const unsigned char _binary_index_html_start[];
extern const unsigned char _binary_index_html_end[];

static void no_cache(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
}

static esp_err_t send_json(httpd_req_t *req, cJSON *value)
{
    if (!value) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    char *body = cJSON_PrintUnformatted(value);
    cJSON_Delete(value);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    no_cache(req);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    esp_err_t err = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    return err;
}

static esp_err_t send_message(httpd_req_t *req, const char *status, const char *message)
{
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(obj, "status", status);
    cJSON_AddStringToObject(obj, "message", message);
    return send_json(req, obj);
}

static esp_err_t bad_request(httpd_req_t *req, const char *message)
{
    httpd_resp_set_status(req, "400 Bad Request");
    return send_message(req, "error", message);
}

static esp_err_t ui_get(httpd_req_t *req)
{
    size_t length = (size_t)(_binary_index_html_end - _binary_index_html_start);
    if (length == 0) return ESP_FAIL;
    /* EMBED_TXTFILES adds one trailing null byte. */
    no_cache(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)_binary_index_html_start, length - 1);
}

static esp_err_t info_get(httpd_req_t *req)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    const esp_app_desc_t *app = esp_app_get_description();
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(obj, "device", "ESP32 OTA Panel");
    cJSON_AddStringToObject(obj, "project", app ? app->project_name : "unknown");
    cJSON_AddStringToObject(obj, "version", app ? app->version : "unknown");
    cJSON_AddStringToObject(obj, "running", running ? running->label : "unknown");
    cJSON_AddStringToObject(obj, "next", target ? target->label : "unavailable");
    cJSON_AddNumberToObject(obj, "max_binary_bytes", target ? target->size : 0);
    cJSON_AddNumberToObject(obj, "free_heap", esp_get_free_heap_size());
    cJSON_AddBoolToObject(obj, "ota_ready", target != NULL && !s_ota_busy);
    cJSON_AddBoolToObject(obj, "authenticated", false);
    return send_json(req, obj);
}

static esp_err_t mqtt_get(httpd_req_t *req)
{
    char host[MQTT_HOST_MAX] = "";
    char user[MQTT_USER_MAX] = "";
    char panel[MQTT_PANEL_MAX] = "panel";
    char pass[MQTT_PASS_MAX] = "";
    uint16_t port = 1883;
    nvs_handle_t nvs;
    if (nvs_open(s_mqtt_ns, NVS_READONLY, &nvs) == ESP_OK) {
        size_t n = sizeof(host);
        (void)nvs_get_str(nvs, "host", host, &n);
        n = sizeof(user);
        (void)nvs_get_str(nvs, "user", user, &n);
        n = sizeof(panel);
        (void)nvs_get_str(nvs, "panel", panel, &n);
        n = sizeof(pass);
        (void)nvs_get_str(nvs, "pass", pass, &n);
        uint16_t saved_port;
        if (nvs_get_u16(nvs, "port", &saved_port) == ESP_OK && saved_port)
            port = saved_port;
        nvs_close(nvs);
    }
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(obj, "host", host);
    cJSON_AddNumberToObject(obj, "port", port);
    cJSON_AddStringToObject(obj, "user", user);
    cJSON_AddStringToObject(obj, "panel", panel);
    cJSON_AddBoolToObject(obj, "has_password", pass[0] != '\0');
    /* Never send stored passwords over HTTP. */
    return send_json(req, obj);
}

static bool json_string(cJSON *obj, const char *key, char *dest, size_t max)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(v) || !v->valuestring) return false;
    size_t len = strlen(v->valuestring);
    if (len >= max) return false;
    memcpy(dest, v->valuestring, len + 1);
    return true;
}

static esp_err_t read_json_body(httpd_req_t *req, cJSON **root)
{
    if (req->content_len == 0 || req->content_len > JSON_BODY_LIMIT)
        return ESP_ERR_INVALID_SIZE;
    char body[JSON_BODY_LIMIT + 1];
    size_t received = 0;
    unsigned timeouts = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 4) continue;
        if (n <= 0) return ESP_FAIL;
        received += (size_t)n;
        timeouts = 0;
    }
    body[received] = '\0';
    *root = cJSON_ParseWithLength(body, received);
    return *root ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static esp_err_t mqtt_post(httpd_req_t *req)
{
    cJSON *json = NULL;
    if (read_json_body(req, &json) != ESP_OK)
        return bad_request(req, "Invalid JSON configuration");

    char host[MQTT_HOST_MAX], user[MQTT_USER_MAX];
    char pass[MQTT_PASS_MAX], panel[MQTT_PANEL_MAX];
    const cJSON *port_obj = cJSON_GetObjectItemCaseSensitive(json, "port");
    const cJSON *clear_obj = cJSON_GetObjectItemCaseSensitive(json, "clear_password");
    bool clear_password = cJSON_IsTrue(clear_obj);
    bool valid = json_string(json, "host", host, sizeof(host)) &&
                 json_string(json, "user", user, sizeof(user)) &&
                 json_string(json, "panel", panel, sizeof(panel)) &&
                 json_string(json, "pass", pass, sizeof(pass)) &&
                 cJSON_IsNumber(port_obj) &&
                 port_obj->valuedouble >= 1 && port_obj->valuedouble <= 65535 &&
                 port_obj->valuedouble == (double)port_obj->valueint &&
                 host[0] && panel[0];
    if (valid) {
        for (const unsigned char *p = (const unsigned char *)host; *p; ++p) {
            if (!( (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                   (*p >= '0' && *p <= '9') || *p == '.' || *p == '-' || *p == '_' )) {
                valid = false;
                break;
            }
        }
    }
    if (!valid) {
        cJSON_Delete(json);
        return bad_request(req, "Invalid host, port, username, panel ID or password length");
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(s_mqtt_ns, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "host", host);
        if (err == ESP_OK) err = nvs_set_u16(nvs, "port", (uint16_t)port_obj->valueint);
        if (err == ESP_OK) err = nvs_set_str(nvs, "user", user);
        if (err == ESP_OK) err = nvs_set_str(nvs, "panel", panel);
        /* Empty password means keep the current secret, unless explicitly cleared. */
        if (err == ESP_OK && (pass[0] || clear_password))
            err = nvs_set_str(nvs, "pass", clear_password ? "" : pass);
        if (err == ESP_OK) err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    cJSON_Delete(json);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT configuration save failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_message(req, "error", "Could not save MQTT settings");
    }
    ESP_LOGI(TAG, "MQTT settings saved to NVS namespace '%s'", s_mqtt_ns);
    return send_message(req, "ok", "MQTT settings saved. Reboot to reconnect.");
}

static void reboot_task(void *arg)
{
    (void)arg;
    /* Allow time for the HTTP success response to leave the device. */
    vTaskDelay(pdMS_TO_TICKS(4000));
    esp_restart();
    vTaskDelete(NULL);
}

static void schedule_reboot(void)
{
    if (xTaskCreate(reboot_task, "panel_reboot", 2048, NULL, 4, NULL) != pdPASS) {
        ESP_LOGW(TAG, "Reboot task allocation failed; rebooting directly");
        esp_restart();
    }
}

static esp_err_t reboot_post(httpd_req_t *req)
{
    esp_err_t err = send_message(req, "ok", "Rebooting");
    schedule_reboot();
    return err;
}

static esp_err_t firmware_post(httpd_req_t *req)
{
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target)
        return bad_request(req, "No inactive OTA partition found");
    if (s_ota_busy) {
        httpd_resp_set_status(req, "409 Conflict");
        return send_message(req, "error", "Firmware update already in progress");
    }
    size_t length = req->content_len;
    if (length < 256 || length > target->size)
        return bad_request(req, "Binary size is invalid or exceeds OTA partition");

    s_ota_busy = true;
    uint8_t *buffer = malloc(UPLOAD_CHUNK);
    if (!buffer) {
        s_ota_busy = false;
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_message(req, "error", "Cannot allocate upload buffer");
    }

    size_t total = 0;
    esp_ota_handle_t ota = 0;
    bool ota_open = false;
    unsigned timeouts = 0;
    esp_err_t err = ESP_OK;
    ESP_LOGI(TAG, "Receiving %u-byte firmware for %s", (unsigned)length, target->label);

    while (total < length) {
        size_t want = (length - total < UPLOAD_CHUNK) ? length - total : UPLOAD_CHUNK;
        int n = httpd_req_recv(req, (char *)buffer, want);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) continue;
        if (n <= 0) { err = ESP_ERR_TIMEOUT; break; }
        timeouts = 0;
        if (total == 0) {
            if (buffer[0] != 0xE9) { err = ESP_ERR_INVALID_ARG; break; }
            err = esp_ota_begin(target, OTA_SIZE_UNKNOWN, &ota);
            if (err != ESP_OK) break;
            ota_open = true;
        }
        err = esp_ota_write(ota, buffer, (size_t)n);
        if (err != ESP_OK) break;
        total += (size_t)n;
    }
    free(buffer);

    if (err == ESP_OK && total == length) {
        err = esp_ota_end(ota);
        ota_open = false;
        if (err == ESP_OK)
            err = esp_ota_set_boot_partition(target);
    }
    if (ota_open) (void)esp_ota_abort(ota);
    s_ota_busy = false;

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Firmware upload failed (%u/%u): %s", (unsigned)total,
                 (unsigned)length, esp_err_to_name(err));
        httpd_resp_set_status(req, "400 Bad Request");
        return send_message(req, "error", "Firmware invalid, transfer failed, or flash write rejected");
    }

    ESP_LOGI(TAG, "Firmware verified; next boot partition is %s", target->label);
    /* Start the restart timer before replying. A dead HTTP connection must
     * never prevent reboot after the OTA boot partition has been committed.
     * The current firmware remains in place on every unsuccessful upload.
     */
    schedule_reboot();
    return send_message(req, "ok", "Firmware installed and verified; rebooting");
}

esp_err_t panel_loader_start(const panel_loader_config_t *config)
{
    if (s_httpd) return ESP_OK;
    if (config && config->mqtt_nvs_namespace && config->mqtt_nvs_namespace[0]) {
        size_t length = strlen(config->mqtt_nvs_namespace);
        /* ESP-IDF NVS namespace limit is 15 characters. */
        if (length > 15) return ESP_ERR_INVALID_ARG;
        memcpy(s_mqtt_ns, config->mqtt_nvs_namespace, length + 1);
    }

    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.server_port = (config && config->http_port) ? config->http_port : 80;
    http.stack_size = 8192;
    http.max_uri_handlers = 8;
    http.lru_purge_enable = true;
    http.recv_wait_timeout = 20;
    http.send_wait_timeout = 20;

    esp_err_t err = httpd_start(&s_httpd, &http);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP loader could not start: %s", esp_err_to_name(err));
        return err;
    }
    const httpd_uri_t endpoints[] = {
        {.uri = "/",             .method = HTTP_GET,  .handler = ui_get},
        {.uri = "/api/info",     .method = HTTP_GET,  .handler = info_get},
        {.uri = "/api/mqtt",     .method = HTTP_GET,  .handler = mqtt_get},
        {.uri = "/api/mqtt",     .method = HTTP_POST, .handler = mqtt_post},
        {.uri = "/api/firmware", .method = HTTP_POST, .handler = firmware_post},
        {.uri = "/api/reboot",   .method = HTTP_POST, .handler = reboot_post},
    };
    for (size_t i = 0; i < sizeof(endpoints) / sizeof(endpoints[0]); ++i) {
        err = httpd_register_uri_handler(s_httpd, &endpoints[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register HTTP handler %s", endpoints[i].uri);
            httpd_stop(s_httpd);
            s_httpd = NULL;
            return err;
        }
    }
    ESP_LOGW(TAG, "HTTP OTA loader active on port %u WITHOUT authentication. TRUSTED LAN ONLY.",
             (unsigned)http.server_port);
    return ESP_OK;
}
