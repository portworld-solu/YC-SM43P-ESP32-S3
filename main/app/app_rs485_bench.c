#include "app_rs485_bench.h"

#if defined(CONFIG_APP_RS485_BENCH_MODE) && CONFIG_APP_RS485_BENCH_MODE

#include "app_bus_bridge.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "rs485_bench";

void app_rs485_bench_apply(void)
{
    /* Default: hide INFO noise from stack/cloud */
    esp_log_level_set("*", ESP_LOG_WARN);

    /* Keep bus path visible */
    esp_log_level_set("rs485_bench", ESP_LOG_INFO);
    esp_log_level_set("rs485_hex", ESP_LOG_INFO);
    esp_log_level_set("app_rs485", ESP_LOG_INFO);
    esp_log_level_set("app_modbus", ESP_LOG_INFO);
    esp_log_level_set("bus_bridge", ESP_LOG_INFO);
    esp_log_level_set("mb_map", ESP_LOG_INFO);
    esp_log_level_set("app", ESP_LOG_INFO);

    /* Explicit quiet for known polluters (even if someone raises *) */
    static const char *quiet[] = {
        "wifi",
        "wifi_init",
        "wifi_mgmt",
        "phy_init",
        "bemfa_mqtt",
        "mqtt_client",
        "transport_base",
        "esp_netif_handlers",
        "app_health",
        "gui_task",
        "bt_mgmt",
        "spiffs",
        "lvgl_rt",
        "board_io_exp",
        "octal_psram",
        "esp_psram",
        "cpu_start",
        "heap_init",
        "spi_flash",
    };
    for (size_t i = 0; i < sizeof(quiet) / sizeof(quiet[0]); i++) {
        /* NONE: drop Wi-Fi disconnect / MQTT transport spam entirely on the bench */
        esp_log_level_set(quiet[i], ESP_LOG_NONE);
    }

    /* Match the log promise: no background FC03 storm while bringing up the bus */
    app_bus_bridge_set_poll_enable(false);

    ESP_LOGI(TAG, "=== RS485 BENCH MODE ===");
    ESP_LOGI(TAG, "Logs focused on rs485_hex / app_rs485 / app_modbus / bus_bridge");
    ESP_LOGI(TAG, "Background Modbus poll OFF — tap UI to see TX/RX only");
    ESP_LOGI(TAG, "Prefer console=USB-JTAG so UART0 TX43/RX44 is RS485-only");
}

void app_rs485_bench_hex(const char *dir, const uint8_t *data, size_t len)
{
    if (!dir) {
        dir = "?";
    }
    if (data == NULL || len == 0) {
        ESP_LOGI("rs485_hex", "%s 0 bytes", dir);
        return;
    }

    /* One line up to 64 bytes; longer frames get a second truncated notice */
    char line[3 * 64 + 8];
    size_t n = len > 64 ? 64 : len;
    size_t pos = 0;
    for (size_t i = 0; i < n && pos + 3 < sizeof(line); i++) {
        int w = snprintf(line + pos, sizeof(line) - pos, "%02X%s", data[i],
                         (i + 1 < n) ? " " : "");
        if (w <= 0) {
            break;
        }
        pos += (size_t)w;
    }
    if (len > 64) {
        ESP_LOGI("rs485_hex", "%s (%u): %s ...", dir, (unsigned)len, line);
    } else {
        ESP_LOGI("rs485_hex", "%s (%u): %s", dir, (unsigned)len, line);
    }
}

#endif /* CONFIG_APP_RS485_BENCH_MODE */
