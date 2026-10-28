/** Ink — home */
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
    hub_room_dev_t *d = &m->rooms[0];
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 480, 480);
    hub_apply_screen_bg(root);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *rail = lv_obj_create(root);
    lv_obj_remove_style_all(rail);
    lv_obj_set_size(rail, 112, 480);
    lv_obj_set_style_pad_all(rail, 12, 0);
    lv_obj_set_style_pad_row(rail, 8, 0);
    lv_obj_set_style_border_width(rail, 1, 0);
    lv_obj_set_style_border_side(rail, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(rail, p->line, 0);
    lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_COLUMN);
    lbl(rail, "INK", p->accent, hub_font());
    hub_clock_label(rail, p->t1, hub_font_clock());
    {
        const bool clim_ok = hub_climate_ui_available();
        char clim[28];
        if (clim_ok) {
            snprintf(clim, sizeof(clim), "%.1f° / %d%%", (double)hub_climate_display_temp_c(),
                     hub_climate_display_rh());
        } else {
            snprintf(clim, sizeof(clim), "--° / --%%");
        }
        lv_obj_t *clim_btn = lv_btn_create(rail);
        lv_obj_remove_style_all(clim_btn);
        lv_obj_set_width(clim_btn, LV_PCT(100));
        lv_obj_add_event_cb(clim_btn, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_SENSOR_CAL);
        lbl(clim_btn, clim, clim_ok ? p->t1 : p->t3, hub_font());
    }
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = lv_btn_create(rail);
        lv_obj_remove_style_all(b);
        lv_obj_add_event_cb(b, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_GATEWAY);
        lbl(b, m->protos[i].name, m->protos[i].ok ? p->on : p->alert, hub_font());
    }
    lv_obj_t *wifi = btn_go(rail, "WiFi", HUB_ROUTE_NETWORK, true);
    lv_obj_set_width(wifi, LV_PCT(100));
    lv_obj_set_height(wifi, 36);

    lv_obj_t *mainw = lv_obj_create(root);
    lv_obj_remove_style_all(mainw);
    lv_obj_set_flex_grow(mainw, 1);
    lv_obj_set_height(mainw, 480);
    lv_obj_set_style_pad_all(mainw, 12, 0);
    lv_obj_set_style_pad_row(mainw, 8, 0);
    lv_obj_set_flex_flow(mainw, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *mt = lv_obj_create(mainw);
    lv_obj_remove_style_all(mt);
    lv_obj_set_width(mt, LV_PCT(100));
    lv_obj_set_height(mt, 28);
    lv_obj_set_flex_flow(mt, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mt, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lbl(mt, "ZONE MATRIX", p->t1, hub_font());
    lv_obj_t *go = lv_btn_create(mt);
    lv_obj_remove_style_all(go);
    lv_obj_add_event_cb(go, room_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)0);
    char go_txt[32];
    snprintf(go_txt, sizeof(go_txt), "%s →", hub_model_room_name(0));
    lbl(go, go_txt, p->accent, hub_font());

    lv_obj_t *grid = lv_obj_create(mainw);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_row(grid, 6, 0);
    lv_obj_set_style_pad_column(grid, 6, 0);
    static lv_coord_t cols[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t rows[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(grid, cols, rows);
    const char *cells[] = {"L1", "L2", "AC", "HEAT", "CUR", "SHUT", "FAN", "PUMP", "VALVE"};
    bool ons[] = {d->ceiling, d->strip > 0, d->ac_on, d->heat_on, d->curtain > 50,
                  d->shutter > 50, d->fan_on, d->pump_ok, d->valve_ok};
    for (int i = 0; i < 9; i++) {
        lv_obj_t *b = lv_btn_create(grid);
        lv_obj_remove_style_all(b);
        hub_apply_card(b, ons[i]);
        theme_card_chrome(b, ons[i]);
        if (ons[i]) {
            lv_obj_set_style_bg_color(b, p->accent, 0);
        }
        lv_obj_set_style_radius(b, p->radius, 0);
        lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, i % 3, 1, LV_GRID_ALIGN_STRETCH, i / 3, 1);
        lv_obj_add_event_cb(b, zone_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, cells[i]);
        hub_style_label(l, ons[i] ? p->ink_on : p->t1, hub_font());
        lv_obj_center(l);
    }
    quickbar(mainw);
}
