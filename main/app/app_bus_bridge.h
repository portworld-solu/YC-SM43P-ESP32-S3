#pragma once
/**
 * @file app_bus_bridge.h
 * @brief UI ↔ Modbus bridge: queue writes, poll reads, refresh hub_model.
 *
 * LVGL callbacks must only call app_bus_bridge_notify_widget() (non-blocking).
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start RS485 worker + optional status poll. Idempotent. */
esp_err_t app_bus_bridge_start(void);

/**
 * After hub_model_* mutation for (room,slot): enqueue Modbus write from map.
 * Safe in LVGL thread (queue send only).
 */
bool app_bus_bridge_notify_widget(int room, int slot);

/** Manual FC06 write (bypass map). Still non-blocking. */
bool app_bus_bridge_post_write(uint8_t slave, uint16_t addr, uint16_t value);

/** Enable/disable background poll (default on after start if map has poll rows). */
void app_bus_bridge_set_poll_enable(bool on);

/** Poll interval ms (default 2000). Call before or after start. */
void app_bus_bridge_set_poll_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif
