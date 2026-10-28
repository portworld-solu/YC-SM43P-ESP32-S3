#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Hub icons — Smart-LVGL nt-style white PNG masks on SPIFFS
 * (spiffs_image/icons/nt/), recolored via lv_img.
 * Source: lvgl-front/lvgl-icons.jsx → tools/gen_hub_icons.py
 */
typedef enum {
    HUB_ICO_HOME = 0,
    HUB_ICO_BULB,
    HUB_ICO_CURTAIN,
    HUB_ICO_SNOW,
    HUB_ICO_SHIELD,
    HUB_ICO_GAUGE,
    HUB_ICO_LAYERS,
    HUB_ICO_WIFI,
    HUB_ICO_WIFI_OFF,
    HUB_ICO_COG,
    HUB_ICO_GRID,
    HUB_ICO_CLOCK,
    HUB_ICO_POWER,
    HUB_ICO_MOON,
    HUB_ICO_MOON_SLEEP,
    HUB_ICO_AWAY,
    HUB_ICO_BUS,
    HUB_ICO_DROP,
    HUB_ICO_HEAT,
    HUB_ICO_PLUS,
    HUB_ICO_MINUS,
    HUB_ICO_UP,
    HUB_ICO_DOWN,
    HUB_ICO_STOP,
    HUB_ICO_BACK,
    HUB_ICO_LEFT,
    HUB_ICO_RIGHT,
    HUB_ICO_CHEVRON,
    HUB_ICO_LINK,
    HUB_ICO_BRIGHT,
    HUB_ICO_INFO,
    HUB_ICO_LOCK,
    HUB_ICO_PLUG,
    HUB_ICO_FAN,
    HUB_ICO_SHUTTER,
    HUB_ICO_LANG,
    HUB_ICO_ROOM_LIVING,
    HUB_ICO_ROOM_BED,
    HUB_ICO_ROOM_KITCHEN,
    HUB_ICO_ROOM_STUDY,
    HUB_ICO_RS485,
    HUB_ICO_MQTT,
    HUB_ICO_COUNT
} hub_ico_t;

/** Mask PNG base name (no path / extension), or NULL if unknown. */
const char *hub_ico_name(hub_ico_t ico);

/** Room index 0..3 → living / bed / kitchen / study icon. */
hub_ico_t hub_ico_for_room(int room_idx);

/** Proto id ("rs485" / "mqtt" / "wifi" / …) → SPIFFS icon. */
hub_ico_t hub_ico_for_proto(const char *proto_id);

/**
 * Decode all hub icons from SPIFFS into PSRAM once.
 * Call after SPIFFS mount / before first hub_ico_add.
 */
void hub_ico_cache_init(void);

/** Legacy Montserrat symbol (debug only — hub_ico_add does not use it). */
const char *hub_ico_sym(hub_ico_t ico);

lv_obj_t *hub_ico_label(lv_obj_t *parent, hub_ico_t ico, lv_color_t color);

/**
 * Create icon sized to `size` pixels from SPIFFS PNG only (no built-in glyphs).
 * Cache miss → empty transparent wrap of `size`.
 */
lv_obj_t *hub_ico_add(lv_obj_t *parent, hub_ico_t ico, lv_color_t color, lv_coord_t size);

/**
 * Rebind an existing hub_ico_add() result (wrap+img or symbol label) in place.
 * Used by edit-list object pools — never destroy/recreate the icon child.
 */
void hub_ico_rebind(lv_obj_t *ico_obj, hub_ico_t ico, lv_color_t color, lv_coord_t size);

/**
 * FE badge: plate + centered icon (SettingRow / SceneBtn).
 * Returns the badge container; icon is its first child.
 */
lv_obj_t *hub_ico_badge(lv_obj_t *parent, hub_ico_t ico, lv_color_t color, lv_coord_t badge_sz,
                        lv_coord_t ico_sz);

lv_obj_t *hub_clock_label(lv_obj_t *parent, lv_color_t color, const lv_font_t *font);

void hub_theme_wash(lv_obj_t *parent, lv_color_t accent, lv_opa_t opa);
void hub_hud_bevel(lv_obj_t *card, lv_color_t tick_color);
lv_obj_t *hub_energy_ring(lv_obj_t *parent, lv_color_t accent, lv_color_t cool);

#ifdef __cplusplus
}
#endif
