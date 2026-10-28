#pragma once
/**
 * @file app_modbus_map.h
 * @brief Per-room / per-widget Modbus binding.
 *
 * Primary config: app_modbus_map_config.h (compile-time seed).
 * Optional: HUB_UI_ENGINEER_EDIT + APP_MODBUS_MAP_PERSIST_NVS for on-screen edits.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "hub_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/** How widget state maps to a holding-register value. */
typedef enum {
    APP_MB_ENC_ONOFF = 0, /**< w->on  ↔  on_val / off_val */
    APP_MB_ENC_LEVEL,     /**< w->level (0..100) ↔ reg (scale + offset) */
} app_mb_encode_t;

/**
 * One command row for a Hub widget.
 * Match key: (room, slot).
 */
typedef struct {
    bool enabled;           /**< false = ignore this row on bus */
    int8_t room;            /**< 0 .. HUB_ROOM_COUNT-1 */
    int8_t slot;            /**< widget slot in that room */
    hub_wtype_t type;       /**< expected widget type */

    uint8_t slave;          /**< Modbus slave address 1..247 */
    uint16_t wr_addr;       /**< FC06 write address */
    uint16_t rd_addr;       /**< FC03 read address (often == wr_addr) */
    bool poll;              /**< include in background status poll */

    app_mb_encode_t encode;
    uint16_t on_val;        /**< ENC_ONOFF: register when ON */
    uint16_t off_val;       /**< ENC_ONOFF: register when OFF */
    int16_t scale_num;      /**< ENC_LEVEL: reg = level * num / den + offset */
    int16_t scale_den;      /**< use 1 if unused */
    int16_t offset;
} app_modbus_binding_t;

/** Load seed + NVS, fill gaps from current hub_model widgets. Idempotent. */
esp_err_t app_modbus_map_init(void);

/** Persist runtime table to NVS. */
esp_err_t app_modbus_map_save(void);

/** Flat view of enabled+disabled slots that have been touched / seeded (for poll). */
const app_modbus_binding_t *app_modbus_map_table(size_t *out_count);

/** Find enabled binding for room+slot (NULL if none / disabled). */
const app_modbus_binding_t *app_modbus_map_find(int room, int slot);

/** Mutable row (creates empty slot entry if needed). NULL if OOB. */
app_modbus_binding_t *app_modbus_map_get(int room, int slot);

/**
 * Suggest defaults from room/slot/type (slave=room+1, addr=room*0x10+slot, …).
 * Does not write the live table.
 */
void app_modbus_map_suggest(int room, int slot, hub_wtype_t type, app_modbus_binding_t *out);

/** Ensure row exists; if missing or type mismatch empty → apply suggest. */
void app_modbus_map_ensure(int room, int slot, hub_wtype_t type);

/**
 * After hub_model_add_widget — append defaults for new slot (RAM only).
 * Caller must persist with app_modbus_map_save() off the LVGL thread.
 */
void app_modbus_map_on_widget_added(int room, int slot, hub_wtype_t type);

/**
 * After hub_model_remove_widget — compact map slots for that room (RAM only).
 * Caller must persist with app_modbus_map_save() off the LVGL thread.
 */
void app_modbus_map_on_widget_removed(int room, int slot);

/** Ensure every current model widget has a map row (seed/suggest). */
void app_modbus_map_sync_model(void);

bool app_modbus_map_encode(const app_modbus_binding_t *b, const hub_widget_t *w, uint16_t *out_reg);
bool app_modbus_map_apply_reg(const app_modbus_binding_t *b, uint16_t reg);

/** Encode helper: pick ONOFF vs LEVEL from widget type. */
app_mb_encode_t app_modbus_map_encode_for_type(hub_wtype_t type);

#ifdef __cplusplus
}
#endif
