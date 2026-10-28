/**
 * @file app_bus_bridge.c
 * @brief Modbus bridge with per-widget map + status poll → hub_model.
 */
#include "app_bus_bridge.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "app_beep.h"
#include "app_modbus_map.h"
#include "app_modbus_rtu.h"
#include "app_rs485.h"
#include "gui_task.h"
#include "hub_model.h"
#include "hub_ui.h"

#include "sdkconfig.h"

static const char *TAG = "bus_bridge";

typedef enum {
    BUS_CMD_WRITE = 1,
    BUS_CMD_POLL_ONE,
    BUS_CMD_POLL_ALL,
} bus_cmd_t;

typedef struct {
    bus_cmd_t cmd;
    uint8_t slave;
    uint16_t addr;
    uint16_t value;
    int8_t room;
    int8_t slot;
} bus_job_t;

static QueueHandle_t s_q;
static TaskHandle_t s_task;
static bool s_poll_en = true;
static uint32_t s_poll_ms = 2000;

static void refresh_cb(void *arg)
{
    (void)arg;
    /* Soft: never tear Modbus/room editors with a full rebuild */
    hub_ui_notify_soft();
}

static void apply_and_refresh(const app_modbus_binding_t *b, uint16_t reg)
{
    /* Apply on this (bus) task is OK for hub_model; refresh must be LVGL. */
    if (app_modbus_map_apply_reg(b, reg)) {
        (void)gui_task_post_lvgl(refresh_cb, NULL);
    }
}

static esp_err_t ensure_rs485(void)
{
    if (app_rs485_is_ready()) {
        return ESP_OK;
    }
    return app_rs485_init(NULL);
}

static void do_write(const bus_job_t *job)
{
    esp_err_t err = ensure_rs485();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "rs485 init: %s", esp_err_to_name(err));
        hub_model_toast("RS485 未就绪");
        (void)gui_task_post_lvgl(refresh_cb, NULL);
        return;
    }
    err = app_modbus_write_single(job->slave, job->addr, job->value, 800);
    /* Health only — do not force full UI rebuild (tears MB edit page). */
    app_rs485_update_hub_health(err == ESP_OK, err == ESP_OK ? 95 : 15, false);
    if (err == ESP_OK) {
        app_beep_pulse(40);
        ESP_LOGI(TAG, "WR slave=%u addr=0x%04X val=%u", job->slave, job->addr, job->value);
    } else {
        ESP_LOGW(TAG, "write fail: %s", esp_err_to_name(err));
        hub_model_toast("总线写入失败");
        (void)gui_task_post_lvgl(refresh_cb, NULL);
    }
}

static void do_poll_one(int room, int slot)
{
    const app_modbus_binding_t *b = app_modbus_map_find(room, slot);
    if (!b || !b->poll) {
        return;
    }
    if (ensure_rs485() != ESP_OK) {
        return;
    }
    uint16_t reg = 0;
    esp_err_t err = app_modbus_read_holding(b->slave, b->rd_addr, 1, &reg, 1, 600);
    app_rs485_update_hub_health(err == ESP_OK, err == ESP_OK ? 90 : 20, false);
    if (err == ESP_OK) {
        apply_and_refresh(b, reg);
    }
}

static void do_poll_all(void)
{
    if (!s_poll_en) {
        return;
    }
    if (ensure_rs485() != ESP_OK) {
        return;
    }
    size_t n = 0;
    const app_modbus_binding_t *t = app_modbus_map_table(&n);
    bool any_ok = false;
    bool any_fail = false;
    bool model_changed = false;

    for (size_t i = 0; i < n; i++) {
        if (!t[i].enabled || !t[i].poll) {
            continue;
        }
        uint16_t reg = 0;
        esp_err_t err = app_modbus_read_holding(t[i].slave, t[i].rd_addr, 1, &reg, 1, 500);
        if (err == ESP_OK) {
            any_ok = true;
            if (app_modbus_map_apply_reg(&t[i], reg)) {
                model_changed = true;
            }
        } else {
            any_fail = true;
            ESP_LOGD(TAG, "poll fail room=%d slot=%d: %s",
                     (int)t[i].room, (int)t[i].slot, esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(20)); /* breathe between slaves */
    }

    if (any_ok || any_fail) {
        app_rs485_update_hub_health(any_ok && !any_fail, any_ok ? 88 : 25, false);
    }
    if (model_changed) {
        (void)gui_task_post_lvgl(refresh_cb, NULL);
    }
}

static void bus_worker(void *arg)
{
    (void)arg;
    TickType_t next_poll = xTaskGetTickCount() + pdMS_TO_TICKS(s_poll_ms);
    bus_job_t job;

    for (;;) {
        TickType_t now = xTaskGetTickCount();
        TickType_t wait = (next_poll > now) ? (next_poll - now) : 0;
        if (xQueueReceive(s_q, &job, wait) == pdTRUE) {
            if (job.cmd == BUS_CMD_WRITE) {
                do_write(&job);
            } else if (job.cmd == BUS_CMD_POLL_ONE) {
                do_poll_one(job.room, job.slot);
            } else if (job.cmd == BUS_CMD_POLL_ALL) {
                do_poll_all();
                next_poll = xTaskGetTickCount() + pdMS_TO_TICKS(s_poll_ms);
            }
            continue;
        }
        /* timeout → scheduled poll */
        do_poll_all();
        next_poll = xTaskGetTickCount() + pdMS_TO_TICKS(s_poll_ms);
    }
}

esp_err_t app_bus_bridge_start(void)
{
#if !(defined(CONFIG_APP_ENABLE_RS485) && CONFIG_APP_ENABLE_RS485)
    ESP_LOGW(TAG, "APP_ENABLE_RS485 off — bridge not started");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_q != NULL) {
        return ESP_OK;
    }
    (void)app_modbus_map_init();
    s_q = xQueueCreate(12, sizeof(bus_job_t));
    if (s_q == NULL) {
        return ESP_ERR_NO_MEM;
    }
    BaseType_t ok = xTaskCreate(bus_worker, "bus_bridge", 4096, NULL, 5, &s_task);
    if (ok != pdPASS) {
        vQueueDelete(s_q);
        s_q = NULL;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "started (poll=%s every %lu ms)", s_poll_en ? "on" : "off",
             (unsigned long)s_poll_ms);
    return ESP_OK;
#endif
}

bool app_bus_bridge_post_write(uint8_t slave, uint16_t addr, uint16_t value)
{
    if (s_q == NULL) {
        return false;
    }
    bus_job_t job = {
        .cmd = BUS_CMD_WRITE,
        .slave = slave,
        .addr = addr,
        .value = value,
        .room = -1,
        .slot = -1,
    };
    return xQueueSend(s_q, &job, 0) == pdTRUE;
}

bool app_bus_bridge_notify_widget(int room, int slot)
{
    if (s_q == NULL) {
        return false;
    }
    const app_modbus_binding_t *b = app_modbus_map_find(room, slot);
    if (!b) {
        return false;
    }
    hub_widget_t *w = hub_model_widget_by_slot(room, slot);
    if (!w) {
        return false;
    }
    uint16_t reg = 0;
    if (!app_modbus_map_encode(b, w, &reg)) {
        return false;
    }
    bus_job_t job = {
        .cmd = BUS_CMD_WRITE,
        .slave = b->slave,
        .addr = b->wr_addr,
        .value = reg,
        .room = (int8_t)room,
        .slot = (int8_t)slot,
    };
    return xQueueSend(s_q, &job, 0) == pdTRUE;
}

void app_bus_bridge_set_poll_enable(bool on)
{
    s_poll_en = on;
}

void app_bus_bridge_set_poll_ms(uint32_t ms)
{
    if (ms < 500) {
        ms = 500;
    }
    s_poll_ms = ms;
}
