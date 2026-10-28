#include "hub_font.h"

#include "lv_font_source_han_14_cjk3500.h"
#include "lv_font_hub_cjk_extra.h"
#include "lv_font_source_han_screen1_time_100.h"

/*
 * Unified UI typeface (one CJK face + one Latin face):
 *
 *   hub CJK extras (站/-/°/…)  ──►  Source Han Sans 14 (≈3500)  ──►  Montserrat 16
 *
 * Extras first so Hub UI strings (站、−、ASCII '-') always hit a known glyph;
 * the large Source Han subset fills the rest. Clocks stay Montserrat digits-only.
 */
static lv_font_t s_hub_ui;     /* body + captions (extras primary) */
static lv_font_t s_hub_han;    /* Source Han bridge */
static bool s_inited;

void hub_font_init(void)
{
    if (s_inited) {
        return;
    }
    s_inited = true;

    s_hub_han = lv_font_source_han_14_cjk3500;
#if LV_FONT_MONTSERRAT_16
    s_hub_han.fallback = &lv_font_montserrat_16;
#else
    s_hub_han.fallback = LV_FONT_DEFAULT;
#endif

    s_hub_ui = lv_font_hub_cjk_extra;
    s_hub_ui.fallback = &s_hub_han;
    /* Match Source Han metrics so mixed runs share one rhythm */
    s_hub_ui.line_height = (uint16_t)(s_hub_han.line_height + 4);
    s_hub_ui.base_line = s_hub_han.base_line;
}

const lv_font_t *hub_font(void)
{
    if (!s_inited) {
        hub_font_init();
    }
    return &s_hub_ui;
}

const lv_font_t *hub_font_small(void)
{
    /* Same face as body — unified captions */
    return hub_font();
}

const lv_font_t *hub_font_clock(void)
{
#if LV_FONT_MONTSERRAT_32
    return &lv_font_montserrat_32;
#else
    return &lv_font_montserrat_28;
#endif
}

const lv_font_t *hub_font_clock_lg(void)
{
#if LV_FONT_MONTSERRAT_48
    return &lv_font_montserrat_48;
#elif LV_FONT_MONTSERRAT_40
    return &lv_font_montserrat_40;
#else
    return &lv_font_montserrat_36;
#endif
}

const lv_font_t *hub_font_clock_xl(void)
{
    return &lv_font_source_han_screen1_time_100;
}

const lv_font_t *hub_font_symbol(void)
{
#if LV_FONT_MONTSERRAT_16
    return &lv_font_montserrat_16;
#else
    return &lv_font_montserrat_14;
#endif
}

static void dropdown_list_font_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    lv_obj_t *list = lv_dropdown_get_list(dd);
    if (!list) {
        return;
    }
    const lv_font_t *f = hub_font();
    lv_obj_set_style_text_font(list, f, LV_PART_MAIN);
    lv_obj_set_style_text_font(list, f, LV_PART_SELECTED);
}

void hub_style_dropdown(lv_obj_t *dd)
{
    if (!dd) {
        return;
    }
    const lv_font_t *f = hub_font();
    lv_obj_set_style_text_font(dd, f, LV_PART_MAIN);
    lv_obj_set_style_text_font(dd, f, LV_PART_SELECTED);
    lv_obj_set_style_text_font(dd, hub_font_symbol(), LV_PART_INDICATOR);
    lv_obj_t *list = lv_dropdown_get_list(dd);
    if (list) {
        lv_obj_set_style_text_font(list, f, LV_PART_MAIN);
        lv_obj_set_style_text_font(list, f, LV_PART_SELECTED);
    }
    lv_obj_add_event_cb(dd, dropdown_list_font_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(dd, dropdown_list_font_cb, LV_EVENT_CLICKED, NULL);
}
