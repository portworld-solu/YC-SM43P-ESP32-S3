/** Ocean — home */
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
    lv_obj_set_style_pad_all(root, 12, 0);
    lv_obj_set_style_pad_row(root, 8, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *head = lv_obj_create(root);
    lv_obj_remove_style_all(head);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_height(head, 36);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    hub_clock_label(head, p->t1, hub_font());
    lbl(head, "Ocean Hub", p->t1, hub_font());
    lv_obj_t *lan = lv_btn_create(head);
    lv_obj_remove_style_all(lan);
    hub_apply_card(lan, m->protos[3].ok);
    theme_card_chrome(lan, m->protos[3].ok);
    if (m->protos[3].ok) {
        lv_obj_set_style_bg_color(lan, p->accent, 0);
    }
    lv_obj_set_size(lan, 52, 26);
    lv_obj_set_style_radius(lan, 999, 0);
    lv_obj_add_event_cb(lan, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_NETWORK);
    lv_obj_t *ll = lv_label_create(lan);
    lv_label_set_text(ll, m->protos[3].ok ? "LAN" : "OFF");
    hub_style_label(ll, m->protos[3].ok ? p->ink_on : p->alert, hub_font());
    lv_obj_center(ll);

    /* Full-width climate → sensor calibration (power removed) */
    lv_obj_t *hero = lv_btn_create(root);
    lv_obj_remove_style_all(hero);
    hub_apply_card(hero, false);
    theme_card_chrome(hero, false);
    lv_obj_set_width(hero, LV_PCT(100));
    lv_obj_set_height(hero, 88);
    lv_obj_add_event_cb(hero, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_SENSOR_CAL);
    {
        const bool clim_ok = hub_climate_ui_available();
        char tb[24], rb[16];
        if (clim_ok) {
            snprintf(tb, sizeof(tb), "%.1f%s", (double)hub_climate_display_temp_c(), HUB_UNIT_C);
            snprintf(rb, sizeof(rb), "%d%s", hub_climate_display_rh(), HUB_UNIT_RH_FULL);
        } else {
            snprintf(tb, sizeof(tb), "--%s", HUB_UNIT_C);
            snprintf(rb, sizeof(rb), "--%s", HUB_UNIT_RH_FULL);
        }
        lv_obj_t *l2 = lv_label_create(hero);
        lv_label_set_text(l2, hub_tr("室内温湿度 · 点此校准", "Indoor climate · tap to calibrate"));
        hub_style_label(l2, p->t3, hub_font());
        lv_obj_align(l2, LV_ALIGN_TOP_LEFT, 12, 10);
        lv_obj_t *v1 = lv_label_create(hero);
        lv_label_set_text(v1, tb);
        hub_style_label(v1, clim_ok ? p->accent : p->t3, hub_font_clock());
        lv_obj_align(v1, LV_ALIGN_BOTTOM_LEFT, 12, -12);
        lv_obj_t *v2 = lv_label_create(hero);
        lv_label_set_text(v2, rb);
        hub_style_label(v2, clim_ok ? p->t1 : p->t3, hub_font_clock());
        lv_obj_align(v2, LV_ALIGN_BOTTOM_RIGHT, -12, -12);
    }

    lbl(root, hub_tr("协议健康度 · 点击切换", "Protocol health · tap"), p->t2, hub_font());
    for (int i = 0; i < 3; i++) {
        lv_obj_t *row = lv_btn_create(root);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 40);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_add_event_cb(row, proto_cb, LV_EVENT_CLICKED, (void *)m->protos[i].id);
        lv_obj_t *n = lv_label_create(row);
        lv_label_set_text(n, m->protos[i].name);
        hub_style_label(n, p->t1, hub_font());
        lv_obj_align(n, LV_ALIGN_TOP_LEFT, 0, 0);
        char pct[16];
        snprintf(pct, sizeof(pct), m->protos[i].ok ? "%d%%" : "FAULT", m->protos[i].health);
        lv_obj_t *st = lv_label_create(row);
        lv_label_set_text(st, pct);
        hub_style_label(st, m->protos[i].ok ? p->on : p->alert, hub_font());
        lv_obj_align(st, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_t *bar = lv_bar_create(row);
        lv_obj_set_size(bar, LV_PCT(100), 8);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, m->protos[i].health, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, m->protos[i].ok ? p->accent : p->alert, LV_PART_INDICATOR);
        lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    }

    lv_obj_t *nav = lv_obj_create(root);
    lv_obj_remove_style_all(nav);
    lv_obj_set_width(nav, LV_PCT(100));
    lv_obj_set_height(nav, 56);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(nav, 8, 0);
    struct { hub_ico_t ico; const char *t; hub_route_t r; } navs[] = {
        { HUB_ICO_HOME, hub_tr("房间", "Rooms"), HUB_ROUTE_ROOM },
        { HUB_ICO_LAYERS, hub_tr("点表", "Points"), HUB_ROUTE_POINTS },
        { HUB_ICO_COG, hub_tr("设置", "Settings"), HUB_ROUTE_SETTINGS },
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = lv_btn_create(nav);
        lv_obj_remove_style_all(b);
        hub_apply_card(b, false);
        theme_card_chrome(b, false);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_height(b, LV_PCT(100));
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(b, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)navs[i].r);
        hub_ico_add(b, navs[i].ico, p->accent, 20);
        lbl(b, navs[i].t, p->t1, hub_font());
    }
    quickbar(root);
}
