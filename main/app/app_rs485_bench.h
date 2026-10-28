#pragma once
/**
 * @file app_rs485_bench.h
 * @brief RS485 bring-up helpers: log focus + hex dump (CONFIG_APP_RS485_BENCH_MODE).
 */

#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_APP_RS485_BENCH_MODE) && CONFIG_APP_RS485_BENCH_MODE

/** Quiet Wi-Fi/MQTT/etc.; keep app_rs485 / app_modbus / bus_bridge / rs485_hex loud. */
void app_rs485_bench_apply(void);

/** Dump up to @p len bytes as hex under tag rs485_hex. */
void app_rs485_bench_hex(const char *dir, const uint8_t *data, size_t len);

#else

static inline void app_rs485_bench_apply(void) {}
static inline void app_rs485_bench_hex(const char *dir, const uint8_t *data, size_t len)
{
    (void)dir;
    (void)data;
    (void)len;
}

#endif

#ifdef __cplusplus
}
#endif
