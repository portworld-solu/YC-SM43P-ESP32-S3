/** Ocean �?helpers / chrome / callbacks */
#include "theme_local.h"
#include "hub_ui.h"
#include "hub_theme.h"
#include "hub_font.h"
#include "hub_model.h"
#include "hub_icons.h"
#include "app_ui.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

void go_cb(lv_event_t *e)
{
    hub_ui_go((hub_route_t)(uintptr_t)lv_event_get_user_data(e));
}

void scene_cb(lv_event_t *e)
{
    const char *id = (const char *)lv_event_get_user_data(e);
    hub_model_apply_scene(id);
    hub_ui_refresh();
}

void proto_cb(lv_event_t *e)
{
    const char *id = (const char *)lv_event_get_user_data(e);
    if (id && strcmp(id, "wifi") == 0) {
        hub_ui_go(HUB_ROUTE_NETWORK);
        return;
    }
    hub_model_toggle_proto(id);
    hub_ui_refresh();
}

void room_cb(lv_event_t *e)
{
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    hub_model_set_room(idx);
    hub_ui_go(HUB_ROUTE_ROOM);
}

void arm_cb(lv_event_t *e)
{
    bool on = (bool)(uintptr_t)lv_event_get_user_data(e);
    hub_model_set_armed(on);
    hub_ui_refresh();
}

void zone_cb(lv_event_t *e)
{
    int cell = (int)(uintptr_t)lv_event_get_user_data(e);
    /* Ink PUMP/VALVE cells open appliances sheet (FE parity) */
    if (cell == 7 || cell == 8) {
        hub_ui_go(HUB_ROUTE_APPLIANCES);
        return;
    }
    hub_model_toggle_zone(cell);
    hub_ui_refresh();
}

lv_obj_t *lbl(lv_obj_t *par, const char *txt, lv_color_t c, const lv_font_t *f)
{
    lv_obj_t *l = lv_label_create(par);
    lv_label_set_text(l, txt ? txt : "");
    hub_style_label(l, c, f);
    return l;
}

lv_obj_t *btn_go(lv_obj_t *par, const char *txt, hub_route_t r, bool accent)
{
    const hub_palette_t *p = hub_palette();
    lv_obj_t *b = lv_btn_create(par);
    lv_obj_remove_style_all(b);
    hub_apply_card(b, accent);
    if (accent) {
        lv_obj_set_style_bg_color(b, p->accent, 0);
    }
    lv_obj_add_event_cb(b, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)r);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    hub_style_label(l, accent ? p->ink_on : p->t1, hub_font());
    lv_obj_center(l);
    return b;
}

void quickbar(lv_obj_t *par)
{
    const hub_palette_t *p = hub_palette();
    lv_obj_t *bar = lv_obj_create(par);
    lv_obj_remove_style_all(bar);
    lv_obj_set_width(bar, LV_PCT(100));
    lv_obj_set_height(bar, 58);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bar, 6, 0);

    struct {
        hub_ico_t ico;
        const char *zh;
        const char *en;
        hub_route_t r;
    } items[] = {
        { HUB_ICO_GRID, "情景", "Scenes", HUB_ROUTE_SCENES },
        { HUB_ICO_SHIELD, "安防", "Alarm", HUB_ROUTE_SECURITY },
        { HUB_ICO_CLOCK, "日程", "Sched", HUB_ROUTE_SCHEDULE },
        { HUB_ICO_COG, "设置", "Setup", HUB_ROUTE_SETTINGS },
    };
    bool zh = hub_model()->settings.lang_zh;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = lv_btn_create(bar);
        lv_obj_remove_style_all(b);
        hub_apply_card(b, false);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_height(b, LV_PCT(100));
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(b, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)items[i].r);
        hub_ico_add(b, items[i].ico, p->accent, 20);
        lbl(b, zh ? items[i].zh : items[i].en, p->t1, hub_font());
    }
}


void toggle_dev_cb(lv_event_t *e)
{
    int kind = (int)(uintptr_t)lv_event_get_user_data(e);
    hub_model_t *m = hub_model();
    hub_room_dev_t *d = &m->rooms[m->room_idx];
    if (kind == 0) {
        d->ceiling = !d->ceiling;
    } else if (kind == 1) {
        d->strip = d->strip > 0 ? 0 : 55;
    } else if (kind == 3) {
        d->ac_on = !d->ac_on;
    } else if (kind == 4) {
        d->heat_on = !d->heat_on;
    } else if (kind == 6) {
        d->fan_on = !d->fan_on;
    }
    hub_ui_refresh();
}

void step_ac_cb(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    hub_model_step_ac(delta);
    hub_ui_refresh();
}

void step_curtain_cb(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    hub_model_step_curtain(delta);
    hub_ui_refresh();
}

void strip_step_cb(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    hub_model_step_strip(delta);
    hub_ui_refresh();
}

void curtain_set_cb(lv_event_t *e)
{
    int level = (int)(intptr_t)lv_event_get_user_data(e);
    hub_model_t *m = hub_model();
    m->rooms[m->room_idx].curtain = level;
    hub_ui_refresh();
}

void theme_card_chrome(lv_obj_t *obj, bool active)
{
    const hub_palette_t *p = hub_palette();
    lv_obj_set_style_radius(obj, p->radius_sm, 0);
    if (active) {
        lv_obj_set_style_outline_width(obj, 1, 0);
        lv_obj_set_style_outline_color(obj, p->cool, 0);
        lv_obj_set_style_outline_opa(obj, LV_OPA_COVER, 0);
    }
}
