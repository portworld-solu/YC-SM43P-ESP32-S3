#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Temperature / humidity unit strings (literal text, not snprintf format).
 * Use ASCII "°C" (U+00B0 + C) — present in hub font extras.
 * Avoid U+2103 "℃" which is missing from the packed CJK sets.
 *
 * IMPORTANT: never concatenate HUB_UNIT_RH into a snprintf format string
 * (e.g. "%d" HUB_UNIT_RH → "%d%") — the trailing '%' is a format introducer
 * and the RH unit disappears. Always use "%d%s" / "%.1f%s" + unit arg, or
 * HUB_UNIT_RH_FMT ("%%") inside a format string.
 */
#define HUB_UNIT_C "°C"
#define HUB_UNIT_RH "%"
#define HUB_UNIT_RH_FMT "%%" /* literal % inside snprintf format */
#define HUB_UNIT_RH_FULL "%RH" /* preferred humidity label */

/**
 * Unified UI fonts:
 * - hub_font() / hub_font_small(): hub CJK extras → Source Han 14 → Montserrat 16
 * - hub_font_symbol(): Montserrat 16 (LV_SYMBOL_* and ASCII +/-)
 * - hub_font_clock*(): Montserrat digits for time only
 */
const lv_font_t *hub_font(void);
const lv_font_t *hub_font_small(void);
const lv_font_t *hub_font_symbol(void);

const lv_font_t *hub_font_clock(void);
const lv_font_t *hub_font_clock_lg(void);
/** XL digits for Zen / minimal homes (~100px Source Han time subset). */
const lv_font_t *hub_font_clock_xl(void);

void hub_font_init(void);

/**
 * Apply hub CJK font to dropdown button + open list (MAIN/SELECTED).
 * Indicator stays Montserrat so the ▼ glyph remains visible.
 */
void hub_style_dropdown(lv_obj_t *dd);

#ifdef __cplusplus
}
#endif
