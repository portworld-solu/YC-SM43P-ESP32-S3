/** Slate — home */
#include "theme_local.h"
#include "hub_ui.h"
#include "hub_theme.h"
#include "hub_font.h"
#include "hub_model.h"
#include "hub_icons.h"
#include "hub_i18n.h"
#include "hub_climate_offset.h"
#include "app_ui.h"

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
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, 14, 0);
    lv_obj_set_style_pad_row(root, 10, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *head = lv_obj_create(root);
    lv_obj_remove_style_all(head);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_height(head, 40);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    hub_clock_label(head, p->t1, hub_font_clock());
    lbl(head, "Slate Hub", p->t1, hub_font());
    lv_obj_t *wifi = lv_btn_create(head);
    lv_obj_remove_style_all(wifi);
    lv_obj_add_event_cb(wifi, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_NETWORK);
    hub_ico_add(wifi, m->protos[3].ok ? HUB_ICO_WIFI : HUB_ICO_WIFI_OFF, p->accent, 20);

    lv_obj_t *pills = lv_obj_create(root);
    lv_obj_remove_style_all(pills);
    lv_obj_set_width(pills, LV_PCT(100));
    lv_obj_set_height(pills, 28);
    lv_obj_set_flex_flow(pills, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(pills, 6, 0);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *pill = lv_btn_create(pills);
        lv_obj_remove_style_all(pill);
        lv_obj_set_height(pill, LV_PCT(100));
        lv_obj_set_style_pad_hor(pill, 10, 0);
        lv_obj_set_style_radius(pill, 999, 0);
        lv_obj_set_style_bg_color(pill, p->bg_card2, 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(pill, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_GATEWAY);
        lv_obj_t *l = lv_label_create(pill);
        lv_label_set_text(l, m->protos[i].name);
        hub_style_label(l, m->protos[i].ok ? p->on : p->alert, hub_font());
        lv_obj_center(l);
    }

    /* Full-width climate → sensor calibration (power removed) */
    const bool clim_ok = hub_climate_ui_available();
    char tbuf[24], rbuf[16];
    if (clim_ok) {
        snprintf(tbuf, sizeof(tbuf), "%.1f%s", (double)hub_climate_display_temp_c(), HUB_UNIT_C);
        snprintf(rbuf, sizeof(rbuf), "%d%s", hub_climate_display_rh(), HUB_UNIT_RH_FULL);
    } else {
        snprintf(tbuf, sizeof(tbuf), "--%s", HUB_UNIT_C);
        snprintf(rbuf, sizeof(rbuf), "--%s", HUB_UNIT_RH_FULL);
    }

    lv_obj_t *clim = lv_btn_create(root);
    lv_obj_remove_style_all(clim);
    hub_apply_card(clim, false);
    theme_card_chrome(clim, false);
    lv_obj_set_width(clim, LV_PCT(100));
    lv_obj_set_height(clim, 112);
    lv_obj_add_event_cb(clim, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_SENSOR_CAL);

    lv_obj_t *tl = lv_label_create(clim);
    lv_label_set_text(tl, hub_tr("室内温湿度 · 点此校准", "Indoor climate · tap to calibrate"));
    hub_style_label(tl, p->t3, hub_font());
    lv_obj_align(tl, LV_ALIGN_TOP_LEFT, 12, 10);

    lv_obj_t *tv = lv_label_create(clim);
    lv_label_set_text(tv, tbuf);
    hub_style_label(tv, clim_ok ? p->accent : p->t3, hub_font_clock());
    lv_obj_align(tv, LV_ALIGN_BOTTOM_LEFT, 12, -14);

    lv_obj_t *rv = lv_label_create(clim);
    lv_label_set_text(rv, rbuf);
    hub_style_label(rv, clim_ok ? p->t1 : p->t3, hub_font_clock());
    lv_obj_align(rv, LV_ALIGN_BOTTOM_RIGHT, -14, -14);

    lv_obj_t *grid = lv_obj_create(root);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_row(grid, 8, 0);
    lv_obj_set_style_pad_column(grid, 8, 0);
    static lv_coord_t cols[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t rows[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(grid, cols, rows);
    for (int i = 0; i < 4; i++) {
        bool on = hub_model_room_active(i);
        lv_obj_t *b = lv_btn_create(grid);
        lv_obj_remove_style_all(b);
        hub_apply_card(b, on);
        theme_card_chrome(b, on);
        lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, i % 2, 1, LV_GRID_ALIGN_STRETCH, i / 2, 1);
        lv_obj_set_style_border_side(b, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_width(b, 3, 0);
        lv_obj_set_style_border_color(b, on ? p->accent : p->line, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(b, room_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        hub_ico_add(b, hub_ico_for_room(i), p->accent, 24);
        lbl(b, hub_model_room_name(i), p->t1, hub_font());
        char pts[24];
        snprintf(pts, sizeof(pts), "%d pts · %s", hub_model_enabled_widget_count(i), on ? "Active" : "Idle");
        lbl(b, pts, on ? p->on : p->t3, hub_font());
    }

    quickbar(root);
}
