/** Zen — home (ThemeHubZen parity: focus room, hairlines, footer strip) */
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
    bool on = hub_model_room_active(0);
    bool zh = hub_lang_zh();

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 480, 480);
    lv_obj_set_style_bg_color(root, p->bg_deep, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /* Header: ZEN + XL clock (~FE 72) | ARMED/HOME */
    lv_obj_t *head = lv_obj_create(root);
    lv_obj_remove_style_all(head);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_height(head, 148);
    lv_obj_set_style_pad_top(head, 28, 0);
    lv_obj_set_style_pad_hor(head, 32, 0);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *hl = lv_obj_create(head);
    lv_obj_remove_style_all(hl);
    lv_obj_set_flex_flow(hl, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(hl, 4, 0);
    lv_obj_t *eye = lbl(hl, "ZEN", p->t3, hub_font());
    lv_obj_set_style_text_letter_space(eye, 4, 0);
    /* FE ~72; use 48 Montserrat so HH:MM fits (100px XL clipped minutes) */
    lv_obj_t *clk = hub_clock_label(hl, p->t1, hub_font_clock_lg());
    lv_obj_set_style_text_letter_space(clk, -2, 0);
    lv_obj_add_flag(hl, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_flag(head, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_flag(clk, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    lv_obj_t *hr = lv_obj_create(head);
    lv_obj_remove_style_all(hr);
    lv_obj_set_flex_flow(hr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_row(hr, 8, 0);
    lv_obj_t *wifi = lv_btn_create(hr);
    lv_obj_remove_style_all(wifi);
    lv_obj_add_event_cb(wifi, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_NETWORK);
    hub_ico_add(wifi, m->protos[3].ok ? HUB_ICO_WIFI : HUB_ICO_WIFI_OFF, p->t2, 18);
    lv_obj_t *arm = lv_btn_create(hr);
    lv_obj_remove_style_all(arm);
    lv_obj_add_event_cb(arm, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_SECURITY);
    lv_obj_t *al = lv_label_create(arm);
    lv_label_set_text(al, m->armed ? "ARMED" : "HOME");
    hub_style_label(al, p->t3, hub_font());
    lv_obj_set_style_text_letter_space(al, 2, 0);

    lv_obj_t *body = lv_obj_create(root);
    lv_obj_remove_style_all(body);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_hor(body, 32, 0);
    lv_obj_set_style_pad_ver(body, 20, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *rule1 = lv_obj_create(body);
    lv_obj_remove_style_all(rule1);
    lv_obj_set_size(rule1, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(rule1, p->line, 0);
    lv_obj_set_style_bg_opa(rule1, LV_OPA_COVER, 0);

    lv_obj_t *sp1 = lv_obj_create(body);
    lv_obj_remove_style_all(sp1);
    lv_obj_set_size(sp1, LV_PCT(100), 22);

    lv_obj_t *focus = lv_btn_create(body);
    lv_obj_remove_style_all(focus);
    lv_obj_set_width(focus, LV_PCT(100));
    lv_obj_set_height(focus, 110);
    lv_obj_add_event_cb(focus, room_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)0);

    lv_obj_t *fl = lv_label_create(focus);
    lv_label_set_text(fl, "FOCUS ROOM");
    hub_style_label(fl, p->t3, hub_font());
    lv_obj_set_style_text_letter_space(fl, 2, 0);
    lv_obj_align(fl, LV_ALIGN_TOP_LEFT, 0, 0);

    /* CJK via hub_font(); no transform_zoom (clips out of parent) */
    lv_obj_t *rn = lv_label_create(focus);
    lv_label_set_text(rn, hub_model_room_name(0));
    hub_style_label(rn, p->t1, hub_font());
    lv_obj_set_style_text_letter_space(rn, zh ? 2 : 1, 0);
    lv_obj_align(rn, LV_ALIGN_LEFT_MID, 0, 4);

    char sub[96];
    const bool clim_ok = hub_climate_ui_available();
    if (d->ac_on && clim_ok) {
        snprintf(sub, sizeof(sub),
                 hub_tr("%s · 设定%d° · 室内%.1f°/%d%%", "%s · Set %d° · Indoor %.1f°/%d%%"),
                 on ? hub_tr("运行中", "Active") : hub_tr("静音", "Quiet"), d->ac_sp,
                 (double)hub_climate_display_temp_c(), hub_climate_display_rh());
    } else if (d->ac_on) {
        snprintf(sub, sizeof(sub), hub_tr("%s · 设定%d° · 室内--°/--%%", "%s · Set %d° · Indoor --/--"),
                 on ? hub_tr("运行中", "Active") : hub_tr("静音", "Quiet"), d->ac_sp);
    } else if (clim_ok) {
        snprintf(sub, sizeof(sub),
                 hub_tr("%s · 室内%.1f°/%d%% · 点此校准", "%s · Indoor %.1f°/%d%% · tap to cal"),
                 on ? hub_tr("运行中", "Active") : hub_tr("静音", "Quiet"),
                 (double)hub_climate_display_temp_c(), hub_climate_display_rh());
    } else {
        snprintf(sub, sizeof(sub), hub_tr("%s · 室内--°/--%% · 点此校准", "%s · Indoor --/-- · tap to cal"),
                 on ? hub_tr("运行中", "Active") : hub_tr("静音", "Quiet"));
    }
    lv_obj_t *clim_hit = lv_btn_create(focus);
    lv_obj_remove_style_all(clim_hit);
    lv_obj_set_width(clim_hit, LV_PCT(100));
    lv_obj_set_height(clim_hit, 28);
    lv_obj_align(clim_hit, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(clim_hit, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_SENSOR_CAL);
    lv_obj_t *sl = lv_label_create(clim_hit);
    lv_label_set_text(sl, sub);
    hub_style_label(sl, p->t2, hub_font());
    lv_obj_align(sl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *rule2 = lv_obj_create(body);
    lv_obj_remove_style_all(rule2);
    lv_obj_set_size(rule2, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(rule2, p->line, 0);
    lv_obj_set_style_bg_opa(rule2, LV_OPA_COVER, 0);

    lv_obj_t *sp2 = lv_obj_create(body);
    lv_obj_remove_style_all(sp2);
    lv_obj_set_size(sp2, LV_PCT(100), 16);

    quickbar(body);

    lv_obj_t *foot = lv_obj_create(root);
    lv_obj_remove_style_all(foot);
    lv_obj_set_width(foot, LV_PCT(100));
    lv_obj_set_height(foot, 56);
    lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_border_width(foot, 1, 0);
    lv_obj_set_style_border_side(foot, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(foot, p->line, 0);

    lv_obj_t *fs = lv_btn_create(foot);
    lv_obj_remove_style_all(fs);
    lv_obj_set_flex_grow(fs, 1);
    lv_obj_set_height(fs, LV_PCT(100));
    lv_obj_add_event_cb(fs, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_SETTINGS);
    lv_obj_t *fsl = lv_label_create(fs);
    lv_label_set_text(fsl, hub_tr("设置", "Settings"));
    hub_style_label(fsl, p->t1, hub_font());
    lv_obj_set_style_text_letter_space(fsl, 1, 0);
    lv_obj_center(fsl);

    lv_obj_t *div1 = lv_obj_create(foot);
    lv_obj_remove_style_all(div1);
    lv_obj_set_size(div1, 1, LV_PCT(100));
    lv_obj_set_style_bg_color(div1, p->line, 0);
    lv_obj_set_style_bg_opa(div1, LV_OPA_COVER, 0);

    lv_obj_t *fc = lv_btn_create(foot);
    lv_obj_remove_style_all(fc);
    lv_obj_set_flex_grow(fc, 1);
    lv_obj_set_height(fc, LV_PCT(100));
    lv_obj_add_event_cb(fc, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_SCENES);
    lv_obj_t *fcl = lv_label_create(fc);
    lv_label_set_text(fcl, hub_tr("情景", "Scenes"));
    hub_style_label(fcl, p->t1, hub_font());
    lv_obj_center(fcl);

    lv_obj_t *div2 = lv_obj_create(foot);
    lv_obj_remove_style_all(div2);
    lv_obj_set_size(div2, 1, LV_PCT(100));
    lv_obj_set_style_bg_color(div2, p->line, 0);
    lv_obj_set_style_bg_opa(div2, LV_OPA_COVER, 0);

    lv_obj_t *fa = lv_btn_create(foot);
    lv_obj_remove_style_all(fa);
    lv_obj_set_style_bg_color(fa, p->t1, 0);
    lv_obj_set_style_bg_opa(fa, LV_OPA_COVER, 0);
    lv_obj_set_flex_grow(fa, 1);
    lv_obj_set_height(fa, LV_PCT(100));
    lv_obj_add_event_cb(fa, scene_cb, LV_EVENT_CLICKED, (void *)"alloff");
    lv_obj_t *fal = lv_label_create(fa);
    lv_label_set_text(fal, hub_tr("全部关闭", "All off"));
    hub_style_label(fal, p->ink_on, hub_font());
    lv_obj_center(fal);
}
