/** Metro — home */
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
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, 8, 0);
    lv_obj_set_style_pad_row(root, 6, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *head = lv_obj_create(root);
    lv_obj_remove_style_all(head);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_height(head, 36);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    hub_clock_label(head, p->t1, hub_font());
    lbl(head, "Metro Hub", p->t1, hub_font());
    lv_obj_t *wifi = lv_btn_create(head);
    lv_obj_remove_style_all(wifi);
    lv_obj_add_event_cb(wifi, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_NETWORK);
    hub_ico_add(wifi, m->protos[3].ok ? HUB_ICO_WIFI : HUB_ICO_WIFI_OFF, p->accent, 18);

    lv_obj_t *grid = lv_obj_create(root);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_row(grid, 6, 0);
    lv_obj_set_style_pad_column(grid, 6, 0);
    static lv_coord_t cols[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t rows[] = { LV_GRID_FR(125), LV_GRID_FR(100), LV_GRID_FR(100), LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(grid, cols, rows);
    char sub0[16], sub1[16], sub_net[16], sub_b[16];
    snprintf(sub0, sizeof(sub0), "%s",
             hub_model_room_active(0) ? hub_tr("运行", "Active") : hub_tr("待机", "Idle"));
    snprintf(sub1, sizeof(sub1), "%s",
             hub_model_room_active(1) ? hub_tr("运行", "Active") : hub_tr("待机", "Idle"));
    char sub_clim[24];
    snprintf(sub_net, sizeof(sub_net), "%s", m->protos[3].ok ? "ON" : "OFF");
    snprintf(sub_b, sizeof(sub_b), "%d/%d", hub_model_ok_count(), HUB_PROTO_COUNT);
    const bool clim_ok = hub_climate_ui_available();
    if (clim_ok) {
        snprintf(sub_clim, sizeof(sub_clim), "%.0f°/%d%%", (double)hub_climate_display_temp_c(),
                 hub_climate_display_rh());
    } else {
        snprintf(sub_clim, sizeof(sub_clim), "--°/--%%");
    }
    const char *labels[] = {
        hub_model_room_name(0), hub_model_room_name(1), hub_model_room_name(2),
        hub_tr("情景", "Scenes"), hub_tr("总线", "Gateway"), hub_tr("网络", "Network"),
        hub_tr("气候", "Climate"),
    };
    const char *subs[] = {sub0, sub1, "4 pts", hub_model_scene_label(m->active_scene), sub_b, sub_net,
                          sub_clim};
    lv_color_t colors[] = {
        LV_COLOR_MAKE(0x00, 0xbc, 0xf2), LV_COLOR_MAKE(0x87, 0x64, 0xb8), LV_COLOR_MAKE(0x10, 0x7c, 0x10),
        LV_COLOR_MAKE(0xff, 0x8c, 0x00), LV_COLOR_MAKE(0xe7, 0x48, 0x56), LV_COLOR_MAKE(0x00, 0x78, 0xd4),
        LV_COLOR_MAKE(0x5d, 0x5a, 0x58),
    };
    uint8_t cs[] = {2, 1, 1, 1, 1, 1, 1};
    uint8_t col[] = {0, 2, 0, 1, 2, 0, 1};
    uint8_t row[] = {0, 0, 1, 1, 1, 2, 2};
    int rooms[] = {0, 1, 2, -1, -1, -1, -1};
    hub_route_t routes[] = {
        HUB_ROUTE_ROOM, HUB_ROUTE_ROOM, HUB_ROUTE_ROOM, HUB_ROUTE_SCENES,
        HUB_ROUTE_GATEWAY, HUB_ROUTE_NETWORK, HUB_ROUTE_SENSOR_CAL,
    };
    for (int i = 0; i < 7; i++) {
        lv_obj_t *b = lv_btn_create(grid);
        lv_obj_remove_style_all(b);
        lv_obj_set_style_bg_color(b, colors[i], 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, col[i], cs[i], LV_GRID_ALIGN_STRETCH, row[i], 1);
        if (rooms[i] >= 0) {
            lv_obj_add_event_cb(b, room_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)rooms[i]);
        } else {
            lv_obj_add_event_cb(b, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)routes[i]);
        }
        lv_obj_t *n = lv_label_create(b);
        lv_label_set_text(n, labels[i]);
        hub_style_label(n, lv_color_hex(0xffffff), cs[i] == 2 ? hub_font_clock() : hub_font());
        lv_obj_align(n, LV_ALIGN_BOTTOM_LEFT, 10, -22);
        lv_obj_t *s = lv_label_create(b);
        lv_label_set_text(s, subs[i]);
        hub_style_label(s, lv_color_hex(0xffffff), hub_font());
        lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 10, -4);
    }
    quickbar(root);
}
