/** Dusk — home */
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
    hub_room_dev_t *d = &m->rooms[0];
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 480, 480);
    hub_apply_screen_bg(root);
    hub_theme_wash(root, p->violet, LV_OPA_30);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, 16, 0);
    lv_obj_set_style_pad_row(root, 10, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *head = lv_obj_create(root);
    lv_obj_remove_style_all(head);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_height(head, 72);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *hl = lv_obj_create(head);
    lv_obj_remove_style_all(hl);
    lv_obj_set_flex_flow(hl, LV_FLEX_FLOW_COLUMN);
    lbl(hl, "DUSK", p->violet, hub_font());
    hub_clock_label(hl, p->t1, hub_font_clock_lg());
    {
        const bool clim_ok = hub_climate_ui_available();
        char clim[40];
        if (clim_ok) {
            snprintf(clim, sizeof(clim), hub_tr("室内 %.1f° · RH %d%%", "Indoor %.1f° · RH %d%%"),
                     (double)hub_climate_display_temp_c(), hub_climate_display_rh());
        } else {
            snprintf(clim, sizeof(clim), "%s", hub_tr("室内 --° · RH --%", "Indoor --° · RH --%"));
        }
        lv_obj_t *clim_btn = lv_btn_create(hl);
        lv_obj_remove_style_all(clim_btn);
        lv_obj_set_width(clim_btn, LV_SIZE_CONTENT);
        lv_obj_add_event_cb(clim_btn, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_SENSOR_CAL);
        lbl(clim_btn, clim, clim_ok ? p->t1 : p->t3, hub_font());
    }
    lv_obj_t *right = lv_obj_create(head);
    lv_obj_remove_style_all(right);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_PCT(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right, 8, 0);
    lv_obj_t *wifi = lv_btn_create(right);
    lv_obj_remove_style_all(wifi);
    lv_obj_set_size(wifi, 40, 40);
    lv_obj_add_event_cb(wifi, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_NETWORK);
    hub_ico_add(wifi, m->protos[3].ok ? HUB_ICO_WIFI : HUB_ICO_WIFI_OFF, p->violet, 18);
    lv_obj_center(lv_obj_get_child(wifi, 0));
    lv_obj_t *sec = lv_btn_create(right);
    lv_obj_remove_style_all(sec);
    hub_apply_card(sec, m->armed);
    theme_card_chrome(sec, m->armed);
    lv_obj_set_size(sec, 100, 56);
    lv_obj_add_event_cb(sec, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_SECURITY);
    hub_ico_add(sec, HUB_ICO_SHIELD, m->armed ? p->on : p->t3, 18);
    lv_obj_align(lv_obj_get_child(sec, 0), LV_ALIGN_TOP_RIGHT, -8, 6);
    lbl(sec, hub_tr("安防", "Security"), p->t3, hub_font());
    lv_obj_align(lv_obj_get_child(sec, 1), LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lbl(sec, m->armed ? hub_tr("已布防", "Armed") : hub_tr("撤防", "Disarmed"),
        m->armed ? p->on : p->t3, hub_font());
    lv_obj_align(lv_obj_get_child(sec, 2), LV_ALIGN_BOTTOM_RIGHT, -8, -8);

    lv_obj_t *grid = lv_obj_create(root);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_row(grid, 8, 0);
    lv_obj_set_style_pad_column(grid, 8, 0);
    static lv_coord_t cols[] = { LV_GRID_FR(12), LV_GRID_FR(10), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t rows[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(grid, cols, rows);

    lv_obj_t *ac = lv_btn_create(grid);
    lv_obj_remove_style_all(ac);
    hub_apply_card(ac, d->ac_on);
    theme_card_chrome(ac, d->ac_on);
    lv_obj_set_grid_cell(ac, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 2);
    lv_obj_add_event_cb(ac, room_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)0);
    hub_ico_add(ac, HUB_ICO_SNOW, p->cool, 28);
    lv_obj_align(lv_obj_get_child(ac, 0), LV_ALIGN_TOP_LEFT, 12, 12);
    char ac_title[40];
    snprintf(ac_title, sizeof(ac_title), hub_tr("%s空调", "%s AC"), hub_model_room_name(0));
    lbl(ac, ac_title, p->t3, hub_font());
    lv_obj_align(lv_obj_get_child(ac, 1), LV_ALIGN_BOTTOM_LEFT, 12, -48);
    char acb[16];
    snprintf(acb, sizeof(acb), d->ac_on ? "%d°" : "--°", d->ac_sp);
    lv_obj_t *av = lv_label_create(ac);
    lv_label_set_text(av, acb);
    hub_style_label(av, p->t1, hub_font_clock_lg());
    lv_obj_align(av, LV_ALIGN_BOTTOM_LEFT, 12, -8);

    int lights = 0;
    for (int i = 0; i < HUB_ROOM_COUNT; i++) {
        if (m->rooms[i].ceiling || m->rooms[i].strip > 0) {
            lights++;
        }
    }
    lv_obj_t *li = lv_btn_create(grid);
    lv_obj_remove_style_all(li);
    hub_apply_card(li, lights > 0);
    theme_card_chrome(li, lights > 0);
    lv_obj_set_grid_cell(li, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_add_event_cb(li, room_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)0);
    hub_ico_add(li, HUB_ICO_BULB, p->warm, 22);
    lv_obj_align(lv_obj_get_child(li, 0), LV_ALIGN_TOP_LEFT, 10, 10);
    lbl(li, hub_tr("灯光", "Lights"), p->t1, hub_font());
    lv_obj_align(lv_obj_get_child(li, 1), LV_ALIGN_BOTTOM_LEFT, 10, -22);
    char lb[24];
    snprintf(lb, sizeof(lb), hub_tr("%d / 4 房间", "%d / 4 rooms"), lights);
    lbl(li, lb, p->t3, hub_font());
    lv_obj_align(lv_obj_get_child(li, 2), LV_ALIGN_BOTTOM_LEFT, 10, -6);

    lv_obj_t *cu = lv_btn_create(grid);
    lv_obj_remove_style_all(cu);
    hub_apply_card(cu, d->curtain > 50);
    theme_card_chrome(cu, d->curtain > 50);
    lv_obj_set_grid_cell(cu, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_add_event_cb(cu, room_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)0);
    hub_ico_add(cu, HUB_ICO_CURTAIN, p->violet, 22);
    lv_obj_align(lv_obj_get_child(cu, 0), LV_ALIGN_TOP_LEFT, 10, 10);
    lbl(cu, hub_tr("窗帘", "Curtain"), p->t1, hub_font());
    lv_obj_align(lv_obj_get_child(cu, 1), LV_ALIGN_BOTTOM_LEFT, 10, -22);
    char cb[24];
    snprintf(cb, sizeof(cb), hub_tr("%d%% 开", "%d%% open"), d->curtain);
    lbl(cu, cb, p->t3, hub_font());
    lv_obj_align(lv_obj_get_child(cu, 2), LV_ALIGN_BOTTOM_LEFT, 10, -6);

    lv_obj_t *strip = lv_obj_create(root);
    lv_obj_remove_style_all(strip);
    lv_obj_set_width(strip, LV_PCT(100));
    lv_obj_set_height(strip, 40);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(strip, 8, 0);
    const char *ids[] = {"home", "movie", "sleep"};
    for (int i = 0; i < 3; i++) {
        bool on = strcmp(m->active_scene, ids[i]) == 0;
        lv_obj_t *b = lv_btn_create(strip);
        lv_obj_remove_style_all(b);
        hub_apply_card(b, on);
        theme_card_chrome(b, on);
        if (on) {
            lv_obj_set_style_bg_color(b, p->accent, 0);
        }
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_height(b, LV_PCT(100));
        lv_obj_add_event_cb(b, scene_cb, LV_EVENT_CLICKED, (void *)ids[i]);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, hub_model_scene_label(ids[i]));
        hub_style_label(l, on ? p->ink_on : p->t1, hub_font());
        lv_obj_center(l);
    }
    quickbar(root);
}
