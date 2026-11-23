#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Portable, local-only HTTP firmware loader for ESP-IDF OTA layouts.
 * The service intentionally does not authenticate users. Only run it on a
 * trusted, isolated LAN and never forward its TCP port to the Internet.
 */
typedef struct {
    /** NVS namespace used by the host application's MQTT client. */
    const char *mqtt_nvs_namespace;
    /** HTTP TCP port. Set to 0 for port 80. */
    uint16_t http_port;
} panel_loader_config_t;

/** Start the HTTP loader once after esp_netif/NVS are initialized. */
esp_err_t panel_loader_start(const panel_loader_config_t *config);

#ifdef __cplusplus
}
#endif
