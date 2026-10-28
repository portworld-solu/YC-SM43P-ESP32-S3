/** Pulse — home */
#include "theme_local.h"
#include "hub_ui.h"
#include "hub_theme.h"
#include "hub_font.h"
#include "hub_model.h"
#include "hub_icons.h"
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
    lv_obj_set_style_pad_all(root, 12, 0);
    lv_obj_set_style_pad_row(root, 8, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *head = lv_obj_create(root);
    lv_obj_remove_style_all(head);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_height(head, 36);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    hub_clock_label(head, p->accent, hub_font());
    lbl(head, "PULSE // NODE", p->t2, hub_font());
    lv_obj_t *wifi = lv_btn_create(head);
    lv_obj_remove_style_all(wifi);
    lv_obj_add_event_cb(wifi, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_NETWORK);
    hub_ico_add(wifi, m->protos[3].ok ? HUB_ICO_WIFI : HUB_ICO_WIFI_OFF, p->accent, 18);

    lv_obj_t *hud = lv_obj_create(root);
    lv_obj_remove_style_all(hud);
    lv_obj_set_width(hud, LV_PCT(100));
    lv_obj_set_height(hud, 100);
    lv_obj_set_flex_flow(hud, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(hud, 8, 0);

    lv_obj_t *clim = lv_btn_create(hud);
    lv_obj_remove_style_all(clim);
    hub_apply_card(clim, false);
    theme_card_chrome(clim, false);
    hub_hud_bevel(clim, p->accent);
    lv_obj_set_flex_grow(clim, 12);
    lv_obj_set_height(clim, LV_PCT(100));
    lv_obj_add_event_cb(clim, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_SENSOR_CAL);
    lbl(clim, "CLIMATE", p->accent, hub_font());
    lv_obj_align(lv_obj_get_child(clim, 0), LV_ALIGN_TOP_LEFT, 8, 8);
    const bool clim_ok = hub_climate_ui_available();
    char pb[24];
    if (clim_ok) {
        snprintf(pb, sizeof(pb), "%.1f°  %d%%", (double)hub_climate_display_temp_c(),
                 hub_climate_display_rh());
    } else {
        snprintf(pb, sizeof(pb), "--°  --%%");
    }
    lv_obj_t *pv = lv_label_create(clim);
    lv_label_set_text(pv, pb);
    hub_style_label(pv, clim_ok ? p->accent : p->t3, hub_font_clock());
    lv_obj_align(pv, LV_ALIGN_LEFT_MID, 8, 4);
    lbl(clim, "T/RH · CAL", p->t3, hub_font());
    lv_obj_align(lv_obj_get_child(clim, 2), LV_ALIGN_BOTTOM_LEFT, 8, -10);

    lv_obj_t *bus = lv_btn_create(hud);
    lv_obj_remove_style_all(bus);
    hub_apply_card(bus, false);
    theme_card_chrome(bus, false);
    hub_hud_bevel(bus, p->accent);
    lv_obj_set_flex_grow(bus, 10);
    lv_obj_set_height(bus, LV_PCT(100));
    lv_obj_add_event_cb(bus, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_GATEWAY);
    lbl(bus, "BUS_HEALTH", p->violet, hub_font());
    lv_obj_align(lv_obj_get_child(bus, 0), LV_ALIGN_TOP_LEFT, 8, 6);
    for (int i = 0; i < 3; i++) {
        char line[24];
        snprintf(line, sizeof(line), "%s %s", m->protos[i].name, m->protos[i].ok ? "OK" : "ERR");
        lv_obj_t *l = lv_label_create(bus);
        lv_label_set_text(l, line);
        hub_style_label(l, m->protos[i].ok ? p->on : p->alert, hub_font());
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 8, 24 + i * 18);
    }

    lbl(root, "ZONE_SELECT", p->t3, hub_font());
    lv_obj_t *zones = lv_obj_create(root);
    lv_obj_remove_style_all(zones);
    lv_obj_set_width(zones, LV_PCT(100));
    lv_obj_set_flex_grow(zones, 1);
    lv_obj_set_layout(zones, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_row(zones, 8, 0);
    lv_obj_set_style_pad_column(zones, 8, 0);
    static lv_coord_t cols[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t rows[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(zones, cols, rows);
    for (int i = 0; i < 4; i++) {
        bool active = hub_model_room_active(i);
        lv_obj_t *b = lv_btn_create(zones);
        lv_obj_remove_style_all(b);
        hub_apply_card(b, active);
        theme_card_chrome(b, active);
        hub_hud_bevel(b, p->accent);
        if (active) {
            lv_obj_set_style_border_color(b, p->accent, 0);
            lv_obj_set_style_border_width(b, 2, 0);
        }
        lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, i % 2, 1, LV_GRID_ALIGN_STRETCH, i / 2, 1);
        lv_obj_add_event_cb(b, room_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        char zid[8];
        snprintf(zid, sizeof(zid), "Z%d", i + 1);
        lbl(b, zid, p->accent, hub_font());
        lv_obj_align(lv_obj_get_child(b, 0), LV_ALIGN_TOP_LEFT, 8, 8);
        lbl(b, hub_model_room_name(i), p->t1, hub_font());
        lv_obj_align(lv_obj_get_child(b, 1), LV_ALIGN_LEFT_MID, 8, 0);
        lbl(b, active ? "ACTIVE" : "IDLE", active ? p->on : p->t4, hub_font());
        lv_obj_align(lv_obj_get_child(b, 2), LV_ALIGN_BOTTOM_LEFT, 8, -8);
    }

    quickbar(root);
    lv_obj_t *sc = lv_obj_create(root);
    lv_obj_remove_style_all(sc);
    lv_obj_set_width(sc, LV_PCT(100));
    lv_obj_set_height(sc, 36);
    lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(sc, 8, 0);
    lv_obj_t *sh = lv_btn_create(sc);
    lv_obj_remove_style_all(sh);
    hub_apply_card(sh, false);
    theme_card_chrome(sh, false);
    lv_obj_set_flex_grow(sh, 1);
    lv_obj_set_height(sh, LV_PCT(100));
    lv_obj_add_event_cb(sh, scene_cb, LV_EVENT_CLICKED, (void *)"home");
    lbl(sh, "SCENE_HOME", p->accent, hub_font());
    lv_obj_center(lv_obj_get_child(sh, 0));
    lv_obj_t *sa = lv_btn_create(sc);
    lv_obj_remove_style_all(sa);
    hub_apply_card(sa, false);
    theme_card_chrome(sa, false);
    lv_obj_set_flex_grow(sa, 1);
    lv_obj_set_height(sa, LV_PCT(100));
    lv_obj_add_event_cb(sa, scene_cb, LV_EVENT_CLICKED, (void *)"away");
    lbl(sa, "SCENE_AWAY", p->alert, hub_font());
    lv_obj_center(lv_obj_get_child(sa, 0));
    lv_obj_t *cf = btn_go(sc, "CFG", HUB_ROUTE_SETTINGS, false);
    lv_obj_set_flex_grow(cf, 1);
    lv_obj_set_height(cf, LV_PCT(100));
}
