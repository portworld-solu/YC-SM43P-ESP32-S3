#include "app_rs485.h"

#include <string.h>

#include "app_coexist.h"
#include "app_gpio_out.h"
#include "app_rs485_bench.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "gui_task.h"
#include "hub_model.h"
#include "hub_ui.h"
#include "sdkconfig.h"

static const char *TAG = "app_rs485";

#define APP_RS485_DE_SETUP_MS_DEFAULT 5u
#define APP_RS485_TX_QUIET_MS_DEFAULT 12u

static SemaphoreHandle_t s_mutex;
static bool s_ready;
static app_rs485_config_t s_cfg;

void app_rs485_get_default_config(app_rs485_config_t *out)
{
    if (out == NULL) {
        return;
    }
    *out = (app_rs485_config_t){
        .uart_num = UART_NUM_0,
        .tx_gpio = 43,
        .rx_gpio = 44,
        .baud = 115200,
        .use_expander_de = true,
        .de_setup_ms = APP_RS485_DE_SETUP_MS_DEFAULT,
        .tx_quiet_ms = APP_RS485_TX_QUIET_MS_DEFAULT,
    };
}

static esp_err_t app_rs485_de(bool tx)
{
    if (!s_cfg.use_expander_de) {
        return ESP_OK;
    }
    return app_gpio_out_set(tx);
}

static uint32_t app_rs485_turnaround_ms(size_t len)
{
    uint32_t ms = s_cfg.tx_quiet_ms;
    if (len > 32) {
        ms += (uint32_t)(len / 16);
    }
    return ms < 20 ? 20 : ms;
}

esp_err_t app_rs485_init(const app_rs485_config_t *cfg)
{
    if (s_ready) {
        return ESP_OK;
    }
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (cfg != NULL) {
        s_cfg = *cfg;
    } else {
        app_rs485_get_default_config(&s_cfg);
    }

#if CONFIG_ESP_CONSOLE_UART && (CONFIG_ESP_CONSOLE_UART_NUM == 0)
    if (s_cfg.uart_num == UART_NUM_0) {
        ESP_LOGW(TAG, "console shares UART0 — use USB-JTAG console for RS485 (see sdkconfig)");
    }
#endif

    esp_err_t err = uart_driver_install(s_cfg.uart_num, 2048, 2048, 0, NULL, 0);
    if (err == ESP_ERR_INVALID_STATE) {
        err = ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "uart_driver_install");

    uart_config_t ucfg = {
        .baud_rate = s_cfg.baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(uart_param_config(s_cfg.uart_num, &ucfg), TAG, "uart_param_config");

    gpio_reset_pin(s_cfg.tx_gpio);
    gpio_reset_pin(s_cfg.rx_gpio);
    gpio_set_pull_mode(s_cfg.rx_gpio, GPIO_PULLUP_ONLY);
    ESP_RETURN_ON_ERROR(
        uart_set_pin(s_cfg.uart_num, s_cfg.tx_gpio, s_cfg.rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
        TAG, "uart_set_pin");

    (void)app_rs485_de(false);
    s_ready = true;
    ESP_LOGI(TAG, "ready UART%d TX=%d RX=%d baud=%d de=%d",
             (int)s_cfg.uart_num, s_cfg.tx_gpio, s_cfg.rx_gpio, s_cfg.baud, (int)s_cfg.use_expander_de);
    return ESP_OK;
}

esp_err_t app_rs485_deinit(void)
{
    if (!s_ready) {
        return ESP_OK;
    }
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    (void)app_rs485_de(false);
    (void)uart_driver_delete(s_cfg.uart_num);
    s_ready = false;
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
    return ESP_OK;
}

bool app_rs485_is_ready(void)
{
    return s_ready;
}

esp_err_t app_rs485_write(const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if (!s_ready || data == NULL || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(timeout_ms ? timeout_ms : 3000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    app_coexist_before_rs485_burst();

    /* Drop stale RX (console echo / bus noise) before a clean half-duplex burst */
    (void)uart_flush_input(s_cfg.uart_num);

    esp_err_t err = app_rs485_de(true);
    if (err != ESP_OK) {
        app_coexist_after_rs485_burst();
        if (s_mutex) {
            xSemaphoreGive(s_mutex);
        }
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(s_cfg.de_setup_ms));

    app_rs485_bench_hex("TX", data, len);
    int n = uart_write_bytes(s_cfg.uart_num, data, (int)len);
    if (n != (int)len) {
        (void)app_rs485_de(false);
        app_coexist_after_rs485_burst();
        if (s_mutex) {
            xSemaphoreGive(s_mutex);
        }
        return ESP_FAIL;
    }

    err = uart_wait_tx_done(s_cfg.uart_num, pdMS_TO_TICKS(timeout_ms ? timeout_ms : 800));
    (void)uart_wait_tx_idle_polling(s_cfg.uart_num);
    /* Flush local TX echo while still driving the bus; then turnaround + DE low */
    (void)uart_flush_input(s_cfg.uart_num);
    vTaskDelay(pdMS_TO_TICKS(app_rs485_turnaround_ms(len)));
    (void)app_rs485_de(false);

    app_coexist_after_rs485_burst();
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
    return err;
}

int app_rs485_read(uint8_t *buf, size_t cap, uint32_t wait_ms)
{
    if (!s_ready || buf == NULL || cap == 0) {
        return -ESP_ERR_INVALID_STATE;
    }
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(wait_ms ? wait_ms : 1000)) != pdTRUE) {
        return -ESP_ERR_TIMEOUT;
    }

    size_t total = 0;
    const int64_t deadline = esp_timer_get_time() + (int64_t)wait_ms * 1000;
    while (total < cap && esp_timer_get_time() < deadline) {
        int remain_ms = (int)((deadline - esp_timer_get_time()) / 1000);
        if (remain_ms <= 0) {
            break;
        }
        int chunk = remain_ms > 60 ? 60 : remain_ms;
        int n = uart_read_bytes(s_cfg.uart_num, buf + total, (int)(cap - total), pdMS_TO_TICKS(chunk));
        if (n > 0) {
            total += (size_t)n;
        }
    }

    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
    return (int)total;
}

esp_err_t app_rs485_transact(const uint8_t *tx, size_t tx_len,
                             uint8_t *rx, size_t rx_cap, int *rx_len,
                             uint32_t turnaround_ms, uint32_t rx_wait_ms)
{
    if (rx_len != NULL) {
        *rx_len = 0;
    }
    esp_err_t err = app_rs485_write(tx, tx_len, 1000);
    if (err != ESP_OK) {
        return err;
    }
    if (turnaround_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(turnaround_ms));
    }
    if (rx == NULL || rx_cap == 0) {
        return ESP_OK;
    }
    int n = app_rs485_read(rx, rx_cap, rx_wait_ms ? rx_wait_ms : 700);
    if (n < 0) {
        return (esp_err_t)(-n);
    }
    if (rx_len != NULL) {
        *rx_len = n;
    }
    if (n > 0) {
        app_rs485_bench_hex("RX", rx, (size_t)n);
    } else {
        app_rs485_bench_hex("RX", NULL, 0);
    }
    return ESP_OK;
}

static void app_rs485_hub_refresh_cb(void *arg)
{
    (void)arg;
    hub_ui_refresh();
}

void app_rs485_update_hub_health(bool link_ok, int health, bool post_ui_refresh)
{
    hub_model_t *m = hub_model();
    if (m == NULL) {
        return;
    }
    int idx = 2; /* docs default: Wi-Fi often last; RS485 commonly slot 2 */
    for (int i = 0; i < HUB_PROTO_COUNT; i++) {
        if (m->protos[i].id != NULL && strcmp(m->protos[i].id, "rs485") == 0) {
            idx = i;
            break;
        }
    }
    m->protos[idx].ok = link_ok;
    m->protos[idx].health = health;
    if (post_ui_refresh) {
        (void)gui_task_post_lvgl(app_rs485_hub_refresh_cb, NULL);
    }
}
