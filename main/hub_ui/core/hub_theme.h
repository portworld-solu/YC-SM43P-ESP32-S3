#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_color_t bg_deep;
    lv_color_t bg_base;
    lv_color_t bg_card;
    lv_color_t bg_card2;
    lv_color_t bg_elev;
    lv_color_t line;
    lv_color_t t1;
    lv_color_t t2;
    lv_color_t t3;
    lv_color_t t4;
    lv_color_t accent;
    lv_color_t accent2;
    lv_color_t on;
    lv_color_t warm;
    lv_color_t heat;
    lv_color_t cool;
    lv_color_t violet;
    lv_color_t alert;
    lv_color_t ink_on;
    lv_coord_t radius;
    lv_coord_t radius_sm;
    const char *name;
} hub_palette_t;

/** Provided by ui/themes/<id>/palette.c */
const hub_palette_t *hub_palette(void);

/**
 * Per-theme card chrome (shadow / bevel / square / ink border).
 * Implemented in ui/themes/<id>/theme_local.c — required for every hub pack.
 */
void theme_card_chrome(lv_obj_t *obj, bool active);

void hub_apply_screen_bg(lv_obj_t *scr);
void hub_apply_card(lv_obj_t *obj, bool active);
/** hub_apply_card + theme_card_chrome */
void hub_apply_card_themed(lv_obj_t *obj, bool active);
void hub_style_label(lv_obj_t *label, lv_color_t color, const lv_font_t *font);

#ifdef __cplusplus
}
#endif
