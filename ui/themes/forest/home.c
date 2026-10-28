/** Forest — home */
#include "theme_local.h"
#include "hub_ui.h"
#include "hub_theme.h"
#include "hub_font.h"
#include "hub_model.h"
#include "hub_icons.h"
#include "hub_i18n.h"
#include "app_ui.h"
#include "hub_climate_offset.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

void build_home(lv_obj_t *parent)
{
    const hub_palette_t *p = hub_palette();
    hub_model_t *m = hub_model();
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 480, 480);
    hub_apply_screen_bg(root);
    hub_theme_wash(root, p->accent, LV_OPA_20);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, 14, 0);
    lv_obj_set_style_pad_row(root, 8, 0);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *head = lv_obj_create(root);
    lv_obj_remove_style_all(head);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_height(head, 36);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    hub_clock_label(head, p->t1, hub_font());
    lbl(head, "Forest", p->t1, hub_font());
    lv_obj_t *wifi = lv_btn_create(head);
    lv_obj_remove_style_all(wifi);
    lv_obj_set_size(wifi, 40, 28);
    lv_obj_add_event_cb(wifi, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_NETWORK);
    hub_ico_add(wifi, m->protos[3].ok ? HUB_ICO_WIFI : HUB_ICO_WIFI_OFF, p->accent, 18);
    lv_obj_center(lv_obj_get_child(wifi, 0));

    /* Climate hero (replaces energy ring) → sensor calibration */
    const bool clim_ok = hub_climate_ui_available();
    lv_obj_t *clim = lv_btn_create(root);
    lv_obj_remove_style_all(clim);
    hub_apply_card(clim, false);
    theme_card_chrome(clim, false);
    lv_obj_set_width(clim, LV_PCT(100));
    lv_obj_set_height(clim, 120);
    lv_obj_add_event_cb(clim, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_SENSOR_CAL);
    lbl(clim, hub_tr("室内温湿度 · 点此校准", "Indoor climate · tap to calibrate"), p->t3, hub_font());
    lv_obj_align(lv_obj_get_child(clim, 0), LV_ALIGN_TOP_MID, 0, 14);
    char tb[24], rb[16];
    if (clim_ok) {
        snprintf(tb, sizeof(tb), "%.1f°", (double)hub_climate_display_temp_c());
        snprintf(rb, sizeof(rb), "%d%%", hub_climate_display_rh());
    } else {
        snprintf(tb, sizeof(tb), "--°");
        snprintf(rb, sizeof(rb), "--%%");
    }
    lv_obj_t *tv = lv_label_create(clim);
    lv_label_set_text(tv, tb);
    hub_style_label(tv, clim_ok ? p->accent : p->t3, hub_font_clock());
    lv_obj_align(tv, LV_ALIGN_BOTTOM_LEFT, 28, -18);
    lv_obj_t *rv = lv_label_create(clim);
    lv_label_set_text(rv, rb);
    hub_style_label(rv, clim_ok ? p->t1 : p->t3, hub_font_clock());
    lv_obj_align(rv, LV_ALIGN_BOTTOM_RIGHT, -28, -18);

    lv_obj_t *stats = lv_obj_create(root);
    lv_obj_remove_style_all(stats);
    lv_obj_set_width(stats, LV_PCT(100));
    lv_obj_set_height(stats, 48);
    lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    char ib[16], hb[16];
    if (clim_ok) {
        snprintf(ib, sizeof(ib), "%.0f°", (double)hub_climate_display_temp_c());
        snprintf(hb, sizeof(hb), "%d%%", hub_climate_display_rh());
    } else {
        snprintf(ib, sizeof(ib), "--°");
        snprintf(hb, sizeof(hb), "--%%");
    }
    const char *busv = hub_model_ok_count() == HUB_PROTO_COUNT ? "OK" : "CHK";
    const char *vals[] = { ib, hb, busv };
    const char *labs[] = {
        hub_tr("室内", "Indoor"),
        hub_tr("湿度", "Humidity"),
        hub_tr("总线", "Bus"),
    };
    hub_route_t rts[] = { HUB_ROUTE_SENSOR_CAL, HUB_ROUTE_SENSOR_CAL, HUB_ROUTE_GATEWAY };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = lv_btn_create(stats);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 90, 48);
        lv_obj_add_event_cb(b, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)rts[i]);
        lv_obj_t *v = lv_label_create(b);
        lv_label_set_text(v, vals[i]);
        const lv_color_t vc = (i < 2 && !clim_ok) ? p->t3
                              : ((i == 2 && hub_model_ok_count() == HUB_PROTO_COUNT) ? p->on : p->t1);
        hub_style_label(v, vc, hub_font());
        lv_obj_align(v, LV_ALIGN_TOP_MID, 0, 2);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, labs[i]);
        hub_style_label(l, p->t3, hub_font());
        lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -2);
    }

    lv_obj_t *scenes = lv_obj_create(root);
    lv_obj_remove_style_all(scenes);
    lv_obj_set_width(scenes, LV_PCT(100));
    lv_obj_set_flex_grow(scenes, 1);
    lv_obj_set_flex_flow(scenes, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(scenes, 8, 0);
    lv_obj_set_style_pad_column(scenes, 8, 0);
    const char *ids[] = {"home", "guest", "movie", "sleep", "eco", "alloff"};
    for (int i = 0; i < 6; i++) {
        bool on = strcmp(m->active_scene, ids[i]) == 0
                  || (strcmp(ids[i], "alloff") == 0 && strcmp(m->active_scene, "away") == 0);
        lv_obj_t *b = lv_btn_create(scenes);
        lv_obj_remove_style_all(b);
        hub_apply_card(b, on);
        theme_card_chrome(b, on);
        if (on) {
            lv_obj_set_style_bg_color(b, p->accent, 0);
        }
        lv_obj_set_style_radius(b, 999, 0);
        lv_obj_set_height(b, 36);
        lv_obj_set_style_pad_hor(b, 12, 0);
        lv_obj_add_event_cb(b, scene_cb, LV_EVENT_CLICKED, (void *)ids[i]);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, hub_model_scene_label(ids[i]));
        hub_style_label(l, on ? p->ink_on : p->t1, hub_font());
        lv_obj_center(l);
    }
    quickbar(root);
}
