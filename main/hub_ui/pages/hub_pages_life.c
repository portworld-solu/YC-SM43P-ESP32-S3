#include "hub_pages_life.h"

#include "hub_ui.h"
#include "hub_theme.h"
#include "hub_font.h"
#include "hub_model.h"
#include "hub_icons.h"
#include "hub_i18n.h"
#include "hub_device_ui.h"
#include "hub_standby.h"
#include "hub_pages_common.h"
#include "hub_climate_offset.h"
#include "ui_bg_task.h"

#include "sdkconfig.h"

#ifndef CONFIG_HUB_UI_ENGINEER_EDIT
#define CONFIG_HUB_UI_ENGINEER_EDIT 0
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static void go_cb(lv_event_t *e)
{
    hub_ui_go((hub_route_t)(uintptr_t)lv_event_get_user_data(e));
}

static lv_obj_t *lbl(lv_obj_t *par, const char *txt, lv_color_t c, const lv_font_t *f)
{
    lv_obj_t *l = lv_label_create(par);
    lv_label_set_text(l, txt ? txt : "");
    hub_style_label(l, c, f);
    return l;
}

static void night_cb(lv_event_t *e)
{
    (void)e;
    hub_model_t *m = hub_model();
    hub_model_set_night_mode(!m->settings.night_mode);
    hub_device_brightness_apply_effective();
    hub_ui_refresh();
}

static void standby_en_cb(lv_event_t *e)
{
    (void)e;
    hub_standby_set_enabled(!hub_standby_enabled());
    hub_ui_refresh();
}

static void standby_tmo_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    hub_standby_set_idle_index((int)lv_dropdown_get_selected(dd));
    hub_model_toast(hub_standby_idle_label());
}

static void standby_preview_cb(lv_event_t *e)
{
    (void)e;
    hub_ui_go(HUB_ROUTE_STANDBY);
}

static void sound_cb(lv_event_t *e)
{
    (void)e;
    hub_model_t *m = hub_model();
    hub_model_set_click_sound(!m->settings.click_sound);
    hub_ui_refresh();
}

static void lang_cb(lv_event_t *e)
{
    (void)e;
    hub_model_toggle_lang();
    hub_ui_refresh();
}

static void sched_toggle_cb(lv_event_t *e)
{
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    hub_model_toggle_schedule(idx);
    hub_ui_refresh();
}

static void sched_run_cb(lv_event_t *e)
{
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    hub_model_run_schedule(idx);
    hub_ui_refresh();
}

/* ── Schedule time / days editor ─────────────────────────────── */
static lv_obj_t *s_sched_sheet;
static lv_obj_t *s_sched_hh;
static lv_obj_t *s_sched_mm;
static lv_obj_t *s_sched_day_btns[7];
static int s_sched_edit_idx = -1;
static uint8_t s_sched_day_mask;

static int sched_parse_hh(const char *t)
{
    int h = 0, m = 0;
    if (t && sscanf(t, "%d:%d", &h, &m) >= 1) {
        if (h < 0) {
            h = 0;
        }
        if (h > 23) {
            h = 23;
        }
        return h;
    }
    return 7;
}

static int sched_parse_mm(const char *t)
{
    int h = 0, m = 0;
    if (t && sscanf(t, "%d:%d", &h, &m) >= 2) {
        if (m < 0) {
            m = 0;
        }
        if (m > 59) {
            m = 59;
        }
        return m;
    }
    return 0;
}

static uint8_t sched_parse_days(const char *days)
{
    if (!days || !days[0]) {
        return 0x1F; /* weekdays default */
    }
    if (strstr(days, "每天") || strstr(days, "Daily") || strstr(days, "daily")) {
        return 0x7F;
    }
    if (strstr(days, "工作日") || strstr(days, "Weekday") || strstr(days, "weekday")) {
        return 0x1F;
    }
    if (strstr(days, "周末") || strstr(days, "Weekend") || strstr(days, "weekend")) {
        return 0x60;
    }
    uint8_t mask = 0;
    static const char *zh[] = { "一", "二", "三", "四", "五", "六", "日" };
    static const char *en[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
    for (int i = 0; i < 7; i++) {
        if (strstr(days, zh[i]) || strstr(days, en[i])) {
            mask |= (uint8_t)(1u << i);
        }
    }
    return mask ? mask : 0x1F;
}

static void sched_format_days(uint8_t mask, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) {
        return;
    }
    if (mask == 0x7F) {
        snprintf(out, out_sz, "%s", hub_tr("每天", "Daily"));
        return;
    }
    if (mask == 0x1F) {
        snprintf(out, out_sz, "%s", hub_tr("工作日", "Weekdays"));
        return;
    }
    if (mask == 0x60) {
        snprintf(out, out_sz, "%s", hub_tr("周末", "Weekend"));
        return;
    }
    static const char *zh[] = { "一", "二", "三", "四", "五", "六", "日" };
    static const char *en[] = { "M", "T", "W", "T", "F", "S", "S" };
    out[0] = '\0';
    bool zh_lang = hub_lang_zh();
    for (int i = 0; i < 7; i++) {
        if (mask & (1u << i)) {
            size_t n = strlen(out);
            if (n + 4 < out_sz) {
                snprintf(out + n, out_sz - n, "%s", zh_lang ? zh[i] : en[i]);
            }
        }
    }
    if (!out[0]) {
        snprintf(out, out_sz, "%s", hub_tr("工作日", "Weekdays"));
    }
}

static void sched_sheet_close(void)
{
    if (s_sched_sheet && lv_obj_is_valid(s_sched_sheet)) {
        lv_obj_add_flag(s_sched_sheet, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_pos(s_sched_sheet, 0, -520);
    }
    s_sched_edit_idx = -1;
}

static void sched_day_toggle_cb(lv_event_t *e)
{
    int di = (int)(uintptr_t)lv_event_get_user_data(e);
    if (di < 0 || di > 6) {
        return;
    }
    s_sched_day_mask ^= (uint8_t)(1u << di);
    if (s_sched_day_mask == 0) {
        s_sched_day_mask = (uint8_t)(1u << di); /* keep at least one */
    }
    const hub_palette_t *p = hub_palette();
    for (int i = 0; i < 7; i++) {
        lv_obj_t *b = s_sched_day_btns[i];
        if (!b || !lv_obj_is_valid(b)) {
            continue;
        }
        bool on = (s_sched_day_mask & (1u << i)) != 0;
        lv_obj_set_style_bg_color(b, on ? p->accent : p->bg_card2, 0);
        if (lv_obj_get_child_cnt(b) > 0) {
            hub_style_label(lv_obj_get_child(b, 0), on ? p->ink_on : p->t1, hub_font());
        }
    }
}

static void sched_save_cb(lv_event_t *e)
{
    (void)e;
    if (s_sched_edit_idx < 0 || s_sched_edit_idx >= HUB_SCHED_COUNT) {
        sched_sheet_close();
        return;
    }
    hub_sched_t *s = &hub_model()->schedules[s_sched_edit_idx];
    int hh = s_sched_hh ? (int)lv_roller_get_selected(s_sched_hh) : 7;
    int mm = s_sched_mm ? (int)lv_roller_get_selected(s_sched_mm) : 0;
    snprintf(s->time, sizeof(s->time), "%02d:%02d", hh, mm);
    sched_format_days(s_sched_day_mask, s->days, sizeof(s->days));
    sched_sheet_close();
    hub_model_toast(hub_tr("日程已保存", "Schedule saved"));
    hub_ui_refresh();
}

static void sched_sheet_scrim_cb(lv_event_t *e)
{
    if (lv_event_get_target(e) != s_sched_sheet) {
        return;
    }
    sched_sheet_close();
}

static void sched_cancel_cb(lv_event_t *e)
{
    (void)e;
    sched_sheet_close();
}

static void sched_build_editor(lv_obj_t *parent)
{
    const hub_palette_t *p = hub_palette();
    s_sched_sheet = lv_obj_create(parent);
    lv_obj_remove_style_all(s_sched_sheet);
    lv_obj_set_size(s_sched_sheet, 480, 480);
    lv_obj_set_pos(s_sched_sheet, 0, -520);
    lv_obj_set_style_bg_color(s_sched_sheet, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_sched_sheet, LV_OPA_70, 0);
    lv_obj_add_flag(s_sched_sheet,
                    LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN |
                        LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(s_sched_sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_sched_sheet, sched_sheet_scrim_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *panel = lv_obj_create(s_sched_sheet);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 440, 360);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(panel, p->radius ? p->radius : 12, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(panel, p->bg_card, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, p->line, 0);
    lv_obj_set_style_pad_all(panel, 14, 0);
    lv_obj_set_style_pad_row(panel, 10, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);

    lbl(panel, hub_tr("编辑时间与日期", "Edit time & days"), p->t1, hub_font());

    lv_obj_t *rollers = lv_obj_create(panel);
    lv_obj_remove_style_all(rollers);
    lv_obj_set_width(rollers, LV_PCT(100));
    lv_obj_set_height(rollers, 120);
    lv_obj_set_flex_flow(rollers, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rollers, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    static const char *hh_opts =
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n"
        "12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23";
    static const char *mm_opts =
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n"
        "12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n"
        "24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n"
        "36\n37\n38\n39\n40\n41\n42\n43\n44\n45\n46\n47\n"
        "48\n49\n50\n51\n52\n53\n54\n55\n56\n57\n58\n59";

    s_sched_hh = lv_roller_create(rollers);
    lv_roller_set_options(s_sched_hh, hh_opts, LV_ROLLER_MODE_INFINITE);
    lv_obj_set_width(s_sched_hh, 100);
    lv_obj_set_style_text_font(s_sched_hh, hub_font_clock(), 0);

    lbl(rollers, ":", p->t1, hub_font_clock());

    s_sched_mm = lv_roller_create(rollers);
    lv_roller_set_options(s_sched_mm, mm_opts, LV_ROLLER_MODE_INFINITE);
    lv_obj_set_width(s_sched_mm, 100);
    lv_obj_set_style_text_font(s_sched_mm, hub_font_clock(), 0);

    lbl(panel, hub_tr("重复日期", "Repeat on"), p->t3, hub_font());

    lv_obj_t *days = lv_obj_create(panel);
    lv_obj_remove_style_all(days);
    lv_obj_set_width(days, LV_PCT(100));
    lv_obj_set_height(days, 40);
    lv_obj_set_flex_flow(days, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(days, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    static const char *d_zh[] = { "一", "二", "三", "四", "五", "六", "日" };
    static const char *d_en[] = { "M", "T", "W", "T", "F", "S", "S" };
    bool zh = hub_lang_zh();
    for (int i = 0; i < 7; i++) {
        lv_obj_t *b = lv_btn_create(days);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 48, 36);
        lv_obj_set_style_radius(b, p->radius ? p->radius : 8, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, p->bg_card2, 0);
        lv_obj_add_event_cb(b, sched_day_toggle_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, zh ? d_zh[i] : d_en[i]);
        hub_style_label(l, p->t1, hub_font());
        lv_obj_center(l);
        s_sched_day_btns[i] = b;
    }

    lv_obj_t *row = lv_obj_create(panel);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 44);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);

    lv_obj_t *cancel = lv_btn_create(row);
    lv_obj_remove_style_all(cancel);
    lv_obj_set_flex_grow(cancel, 1);
    lv_obj_set_height(cancel, LV_PCT(100));
    lv_obj_set_style_radius(cancel, p->radius, 0);
    lv_obj_set_style_bg_opa(cancel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cancel, p->bg_card2, 0);
    lv_obj_set_style_border_width(cancel, 1, 0);
    lv_obj_set_style_border_color(cancel, p->line, 0);
    lv_obj_add_event_cb(cancel, sched_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_label_set_text(cl, hub_tr("取消", "Cancel"));
    hub_style_label(cl, p->t1, hub_font());
    lv_obj_center(cl);

    lv_obj_t *save = lv_btn_create(row);
    lv_obj_remove_style_all(save);
    lv_obj_set_flex_grow(save, 1);
    lv_obj_set_height(save, LV_PCT(100));
    lv_obj_set_style_radius(save, p->radius, 0);
    lv_obj_set_style_bg_opa(save, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(save, p->accent, 0);
    lv_obj_add_event_cb(save, sched_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(save);
    lv_label_set_text(sl, hub_tr("保存", "Save"));
    hub_style_label(sl, p->ink_on, hub_font());
    lv_obj_center(sl);
}

static void sched_open_editor(int idx)
{
    if (idx < 0 || idx >= HUB_SCHED_COUNT || !s_sched_sheet || !lv_obj_is_valid(s_sched_sheet)) {
        return;
    }
    const hub_palette_t *p = hub_palette();
    hub_sched_t *s = &hub_model()->schedules[idx];
    s_sched_edit_idx = idx;
    s_sched_day_mask = sched_parse_days(s->days);

    if (s_sched_hh && lv_obj_is_valid(s_sched_hh)) {
        lv_roller_set_selected(s_sched_hh, (uint16_t)sched_parse_hh(s->time), LV_ANIM_OFF);
    }
    if (s_sched_mm && lv_obj_is_valid(s_sched_mm)) {
        lv_roller_set_selected(s_sched_mm, (uint16_t)sched_parse_mm(s->time), LV_ANIM_OFF);
    }
    for (int i = 0; i < 7; i++) {
        lv_obj_t *b = s_sched_day_btns[i];
        if (!b || !lv_obj_is_valid(b)) {
            continue;
        }
        bool on = (s_sched_day_mask & (1u << i)) != 0;
        lv_obj_set_style_bg_color(b, on ? p->accent : p->bg_card2, 0);
        if (lv_obj_get_child_cnt(b) > 0) {
            hub_style_label(lv_obj_get_child(b, 0), on ? p->ink_on : p->t1, hub_font());
        }
    }

    lv_obj_clear_flag(s_sched_sheet, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(s_sched_sheet, 0, 0);
    lv_obj_move_foreground(s_sched_sheet);
}

static void sched_edit_cb(lv_event_t *e)
{
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    sched_open_editor(idx);
}

static void heat_step_cb(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    hub_model_t *m = hub_model();
    hub_room_dev_t *d = &m->rooms[m->room_idx];
    int sp = d->heat_sp + delta;
    if (sp < 16) {
        sp = 16;
    }
    if (sp > 30) {
        sp = 30;
    }
    d->heat_sp = sp;
    for (int i = 0; i < m->widget_count[m->room_idx]; i++) {
        hub_widget_t *w = &m->widgets[m->room_idx][i];
        if (w->type == HUB_W_THERMO && w->enabled) {
            w->level = sp;
            break;
        }
    }
    hub_ui_refresh();
}

static void heat_toggle_cb(lv_event_t *e)
{
    (void)e;
    hub_model_t *m = hub_model();
    hub_room_dev_t *d = &m->rooms[m->room_idx];
    d->heat_on = !d->heat_on;
    for (int i = 0; i < m->widget_count[m->room_idx]; i++) {
        hub_widget_t *w = &m->widgets[m->room_idx][i];
        if (w->type == HUB_W_THERMO && w->enabled) {
            w->on = d->heat_on;
            break;
        }
    }
    hub_model_toast(d->heat_on ? hub_tr("地暖开启", "Heat on") : hub_tr("地暖关闭", "Heat off"));
    hub_ui_refresh();
}

static void ac_toggle_cb(lv_event_t *e)
{
    (void)e;
    hub_model_t *m = hub_model();
    hub_room_dev_t *d = &m->rooms[m->room_idx];
    d->ac_on = !d->ac_on;
    for (int i = 0; i < m->widget_count[m->room_idx]; i++) {
        hub_widget_t *w = &m->widgets[m->room_idx][i];
        if (w->type == HUB_W_CLIM && w->enabled) {
            w->on = d->ac_on;
            break;
        }
    }
    hub_model_toast(d->ac_on ? hub_tr("空调开启", "AC on") : hub_tr("空调关闭", "AC off"));
    hub_ui_refresh();
}

static void ac_step_cb(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    hub_model_step_ac(delta);
    hub_ui_refresh();
}

static void clim_temp_step_cb(lv_event_t *e)
{
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    if (!hub_climate_ui_available()) {
        hub_model_toast(hub_tr("无温湿度模块", "No climate module"));
        return;
    }
    hub_climate_step_temp(dir);
    hub_climate_apply_to_model(false);
    hub_model_toast(hub_tr("温度补偿已保存", "Temp offset saved"));
    hub_ui_refresh();
}

static void clim_rh_step_cb(lv_event_t *e)
{
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    if (!hub_climate_ui_available()) {
        hub_model_toast(hub_tr("无温湿度模块", "No climate module"));
        return;
    }
    hub_climate_step_rh(dir);
    hub_climate_apply_to_model(false);
    hub_model_toast(hub_tr("湿度补偿已保存", "Humidity offset saved"));
    hub_ui_refresh();
}

static lv_obj_t *mk_step_btn(lv_obj_t *par, hub_ico_t ico, lv_event_cb_t cb, void *ud)
{
    const hub_palette_t *p = hub_palette();
    lv_obj_t *b = lv_btn_create(par);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 40, 36);
    lv_obj_set_style_radius(b, p->radius, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, p->line, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, p->bg_card2, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    hub_ico_add(b, ico, p->t1, 16);
    lv_obj_center(lv_obj_get_child(b, 0));
    return b;
}

/** Temperature / humidity compensation block for settings. */
static void hub_build_climate_offset_block(lv_obj_t *body)
{
    const hub_palette_t *p = hub_palette();
    hub_climate_offset_init();

    const bool ready = hub_climate_ui_available();
    float off_t = hub_climate_temp_offset_c();
    int off_rh = hub_climate_rh_offset();

    if (!ready) {
        lv_obj_t *hint = lv_obj_create(body);
        lv_obj_remove_style_all(hint);
        lv_obj_set_width(hint, LV_PCT(100));
        lv_obj_set_height(hint, 56);
        lv_obj_set_style_radius(hint, p->radius, 0);
        lv_obj_set_style_bg_opa(hint, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(hint, p->bg_card, 0);
        lv_obj_set_style_border_width(hint, 1, 0);
        lv_obj_set_style_border_color(hint, p->line, 0);
        lv_obj_set_style_pad_all(hint, 12, 0);
        lv_obj_t *hl = lv_label_create(hint);
        lv_label_set_text(hl, hub_tr("未检测到温湿度模块 · 无数据可校准",
                                     "No T/H sensor · nothing to calibrate"));
        hub_style_label(hl, p->t3, hub_font());
        lv_obj_align(hl, LV_ALIGN_LEFT_MID, 4, 0);
        return;
    }

    float raw_t = hub_climate_raw_temp_c();
    int raw_rh = hub_climate_raw_rh();
    float out_t = hub_climate_display_temp_c();
    int out_rh = hub_climate_display_rh();

    /* —— Temperature —— */
    {
        lv_obj_t *card = lv_obj_create(body);
        lv_obj_remove_style_all(card);
        lv_obj_set_width(card, LV_PCT(100));
        lv_obj_set_height(card, 108);
        lv_obj_set_style_radius(card, p->radius, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(card, p->bg_card, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, p->line, 0);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        hub_ico_badge(card, HUB_ICO_GAUGE, p->accent, 32, 16);
        lv_obj_align(lv_obj_get_child(card, 0), LV_ALIGN_TOP_LEFT, 0, 2);

        lv_obj_t *title = lv_label_create(card);
        lv_label_set_text(title, hub_tr("温度补偿", "Temp offset"));
        hub_style_label(title, p->t1, hub_font());
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 40, 4);

        char line[80];
        snprintf(line, sizeof(line),
                 hub_tr("当前 %.1f%s  →  显示 %.1f%s", "Now %.1f%s  →  Show %.1f%s"),
                 (double)raw_t, HUB_UNIT_C, (double)out_t, HUB_UNIT_C);
        lv_obj_t *sub = lv_label_create(card);
        lv_label_set_text(sub, line);
        hub_style_label(sub, p->t3, hub_font());
        lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 40, 28);
        lv_obj_set_width(sub, 280);
        lv_label_set_long_mode(sub, LV_LABEL_LONG_CLIP);

        char offb[24];
        snprintf(offb, sizeof(offb), "%+.1f%s", (double)off_t, HUB_UNIT_C);
        lv_obj_t *ov = lv_label_create(card);
        lv_label_set_text(ov, offb);
        hub_style_label(ov, p->accent, hub_font());
        lv_obj_align(ov, LV_ALIGN_BOTTOM_LEFT, 40, -4);

        lv_obj_t *row = lv_obj_create(card);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 100, 40);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_align(row, LV_ALIGN_BOTTOM_RIGHT, 0, -2);
        mk_step_btn(row, HUB_ICO_MINUS, clim_temp_step_cb, (void *)(intptr_t)-1);
        mk_step_btn(row, HUB_ICO_PLUS, clim_temp_step_cb, (void *)(intptr_t)1);
    }

    /* —— Humidity —— */
    {
        lv_obj_t *card = lv_obj_create(body);
        lv_obj_remove_style_all(card);
        lv_obj_set_width(card, LV_PCT(100));
        lv_obj_set_height(card, 108);
        lv_obj_set_style_radius(card, p->radius, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(card, p->bg_card, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, p->line, 0);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        hub_ico_badge(card, HUB_ICO_DROP, p->accent, 32, 16);
        lv_obj_align(lv_obj_get_child(card, 0), LV_ALIGN_TOP_LEFT, 0, 2);

        lv_obj_t *title = lv_label_create(card);
        lv_label_set_text(title, hub_tr("湿度补偿", "Humidity offset"));
        hub_style_label(title, p->t1, hub_font());
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 40, 4);

        char line[80];
        snprintf(line, sizeof(line),
                 hub_tr("当前 %d%s  →  显示 %d%s", "Now %d%s  →  Show %d%s"),
                 raw_rh, HUB_UNIT_RH_FULL, out_rh, HUB_UNIT_RH_FULL);
        lv_obj_t *sub = lv_label_create(card);
        lv_label_set_text(sub, line);
        hub_style_label(sub, p->t3, hub_font());
        lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 40, 28);
        lv_obj_set_width(sub, 280);
        lv_label_set_long_mode(sub, LV_LABEL_LONG_CLIP);

        char offb[24];
        snprintf(offb, sizeof(offb), "%+d%s", off_rh, HUB_UNIT_RH_FULL);
        lv_obj_t *ov = lv_label_create(card);
        lv_label_set_text(ov, offb);
        hub_style_label(ov, p->accent, hub_font());
        lv_obj_align(ov, LV_ALIGN_BOTTOM_LEFT, 40, -4);

        lv_obj_t *row = lv_obj_create(card);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 100, 40);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_align(row, LV_ALIGN_BOTTOM_RIGHT, 0, -2);
        mk_step_btn(row, HUB_ICO_MINUS, clim_rh_step_cb, (void *)(intptr_t)-1);
        mk_step_btn(row, HUB_ICO_PLUS, clim_rh_step_cb, (void *)(intptr_t)1);
    }
}

/** FE sf-switch — radius follows theme palette. */
static lv_obj_t *mk_switch(lv_obj_t *par, bool on, lv_event_cb_t cb, void *ud)
{
    const hub_palette_t *p = hub_palette();
    lv_coord_t r = p->radius;
    lv_obj_t *sw = lv_btn_create(par);
    lv_obj_remove_style_all(sw);
    lv_obj_set_size(sw, 44, 24);
    lv_obj_set_style_radius(sw, r, 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sw, on ? p->accent : lv_color_mix(p->t1, p->bg_deep, LV_OPA_20), 0);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *kn = lv_obj_create(sw);
    lv_obj_remove_style_all(kn);
    lv_obj_set_size(kn, 18, 18);
    lv_obj_set_style_radius(kn, r > 0 ? (r > 9 ? 9 : r) : 0, 0);
    lv_obj_set_style_bg_opa(kn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(kn, p->ink_on, 0);
    lv_obj_align(kn, on ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, on ? -3 : 3, 0);
    lv_obj_clear_flag(kn, LV_OBJ_FLAG_CLICKABLE);
    return sw;
}

static lv_obj_t *mk_setting_row(lv_obj_t *body, hub_ico_t ico, const char *title, const char *sub)
{
    const hub_palette_t *p = hub_palette();
    lv_obj_t *row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 64);
    lv_obj_set_style_radius(row, p->radius, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, p->line, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    hub_ico_badge(row, ico, p->accent, 36, 18);
    lv_obj_align(lv_obj_get_child(row, 0), LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_t *t = lv_label_create(row);
    lv_label_set_text(t, title);
    hub_style_label(t, p->t1, hub_font());
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 48, -10);
    lv_obj_t *s = lv_label_create(row);
    lv_label_set_text(s, sub);
    hub_style_label(s, p->t3, hub_font());
    lv_obj_align(s, LV_ALIGN_LEFT_MID, 48, 12);
    lv_obj_set_width(s, 280);
    lv_label_set_long_mode(s, LV_LABEL_LONG_CLIP);
    return row;
}

static lv_obj_t *mk_nav_row(lv_obj_t *body, hub_ico_t ico, const char *title, const char *sub,
                            hub_route_t route)
{
    const hub_palette_t *p = hub_palette();
    lv_obj_t *row = lv_btn_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 64);
    lv_obj_set_style_radius(row, p->radius, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, p->line, 0);
    hub_ico_badge(row, ico, p->accent, 36, 18);
    lv_obj_align(lv_obj_get_child(row, 0), LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_t *t = lv_label_create(row);
    lv_label_set_text(t, title);
    hub_style_label(t, p->t1, hub_font());
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 48, -10);
    lv_obj_t *s = lv_label_create(row);
    lv_label_set_text(s, sub);
    hub_style_label(s, p->t3, hub_font());
    lv_obj_align(s, LV_ALIGN_LEFT_MID, 48, 12);
    lv_obj_set_width(s, 300);
    lv_label_set_long_mode(s, LV_LABEL_LONG_CLIP);
    hub_ico_add(row, HUB_ICO_CHEVRON, p->t4, 16);
    lv_obj_align(lv_obj_get_child(row, -1), LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_add_event_cb(row, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)route);
    return row;
}

void hub_build_settings(lv_obj_t *parent)
{
    const hub_palette_t *p = hub_palette();
    hub_model_t *m = hub_model();
    bool zh = m->settings.lang_zh;
    hub_wifi_sync_model();
    lv_obj_t *shell = hub_shell_create(parent, hub_tr("设置", "Settings"), true);
    lv_obj_t *body = hub_shell_body(shell);

    lbl(body, hub_tr("显示", "Display"), p->t3, hub_font());
    hub_build_brightness_block(body);

    {
        char sub[40];
        snprintf(sub, sizeof(sub), "%s",
                 m->settings.night_mode
                     ? hub_tr("已开启 · 降亮", "On · dimmed")
                     : hub_tr("跟随白天", "Follow day"));
        lv_obj_t *row = mk_setting_row(body, HUB_ICO_MOON, hub_tr("夜间模式", "Night mode"), sub);
        mk_switch(row, m->settings.night_mode, night_cb, NULL);
        lv_obj_align(lv_obj_get_child(row, -1), LV_ALIGN_RIGHT_MID, -4, 0);
    }

    {
        char sub[48];
        snprintf(sub, sizeof(sub), "%s · %s",
                 hub_standby_enabled() ? hub_tr("已开启", "On") : hub_tr("已关闭", "Off"),
                 hub_standby_idle_label());
        lv_obj_t *row = mk_setting_row(body, HUB_ICO_CLOCK,
                                       hub_tr("低功耗待机", "Low-power standby"), sub);
        mk_switch(row, hub_standby_enabled(), standby_en_cb, NULL);
        lv_obj_align(lv_obj_get_child(row, -1), LV_ALIGN_RIGHT_MID, -4, 0);
    }

    if (hub_standby_enabled()) {
        lbl(body, hub_tr("进入待机时长", "Idle before standby"), p->t3, hub_font());
        lv_obj_t *dd = lv_dropdown_create(body);
        lv_obj_set_width(dd, LV_PCT(100));
        hub_style_dropdown(dd);
        lv_obj_set_style_radius(dd, p->radius, 0);
        lv_dropdown_set_options(dd, zh
            ? "15 秒\n30 秒\n1 分钟\n2 分钟\n5 分钟\n10 分钟"
            : "15 sec\n30 sec\n1 min\n2 min\n5 min\n10 min");
        lv_dropdown_set_selected(dd, (uint16_t)hub_standby_idle_index());
        lv_obj_add_event_cb(dd, standby_tmo_cb, LV_EVENT_VALUE_CHANGED, NULL);

        lv_obj_t *prev = lv_btn_create(body);
        lv_obj_remove_style_all(prev);
        lv_obj_set_width(prev, LV_PCT(100));
        lv_obj_set_height(prev, 44);
        lv_obj_set_style_radius(prev, p->radius, 0);
        lv_obj_set_style_border_width(prev, 1, 0);
        lv_obj_set_style_border_color(prev, p->line, 0);
        lv_obj_add_event_cb(prev, standby_preview_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *pl = lv_label_create(prev);
        lv_label_set_text(pl, hub_tr("预览待机界面 →", "Preview standby →"));
        hub_style_label(pl, p->accent, hub_font());
        lv_obj_center(pl);
    }

    lbl(body, hub_tr("系统", "System"), p->t3, hub_font());

    {
        lv_obj_t *row = mk_setting_row(body, HUB_ICO_LANG, hub_tr("界面语言", "Language"),
                                       zh ? "简体中文" : "English");
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, lang_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *r = lv_label_create(row);
        lv_label_set_text(r, zh ? "ZH" : "EN");
        hub_style_label(r, p->accent, hub_font());
        lv_obj_align(r, LV_ALIGN_RIGHT_MID, -8, 0);
    }

    {
        const char *cal_sub = hub_climate_ui_available()
                                  ? hub_tr("温度 / 湿度补偿", "Temp / RH offset")
                                  : hub_tr("无模块 · 可查看说明", "No module · info");
        mk_nav_row(body, HUB_ICO_DROP, hub_tr("传感校准", "Sensor calibration"), cal_sub,
                   HUB_ROUTE_SENSOR_CAL);
    }

    {
        const char *net = m->protos[3].ok
                              ? hub_tr("Wi-Fi 已连接", "Wi-Fi connected")
                              : hub_tr("Wi-Fi 关闭", "Wi-Fi off");
        mk_nav_row(body, HUB_ICO_WIFI, hub_tr("网络与配网", "Network"), net, HUB_ROUTE_NETWORK);
    }

    {
        char sub[40];
        snprintf(sub, sizeof(sub), zh ? "%d 条启用" : "%d enabled", hub_model_sched_on_count());
        mk_nav_row(body, HUB_ICO_CLOCK, hub_tr("定时与日程", "Schedule"), sub, HUB_ROUTE_SCHEDULE);
    }

#if CONFIG_HUB_UI_ENGINEER_EDIT
    mk_nav_row(body, HUB_ICO_HOME, hub_tr("房间控件", "Room widgets"),
               hub_tr("类型 / MB / 显隐", "Type / MB / Show"), HUB_ROUTE_ROOM_EDIT);
#else
    mk_nav_row(body, HUB_ICO_HOME, hub_tr("房间控件", "Room widgets"),
               hub_tr("显隐 · 点表改代码", "Show/hide · map in code"), HUB_ROUTE_ROOM_EDIT);
#endif

    {
        char sub[40];
        snprintf(sub, sizeof(sub), zh ? "%d/%d 正常" : "%d/%d OK", hub_model_ok_count(), HUB_PROTO_COUNT);
        mk_nav_row(body, HUB_ICO_BUS, hub_tr("总线与点表", "Bus & points"), sub, HUB_ROUTE_GATEWAY);
    }

    {
        lv_obj_t *row = mk_setting_row(body, HUB_ICO_POWER, hub_tr("触控音", "Click sound"),
                                       m->settings.click_sound ? hub_tr("开启", "On")
                                                               : hub_tr("静音", "Mute"));
        mk_switch(row, m->settings.click_sound, sound_cb, NULL);
        lv_obj_align(lv_obj_get_child(row, -1), LV_ALIGN_RIGHT_MID, -4, 0);
    }

    {
        char foot[48];
        snprintf(foot, sizeof(foot), "Hub UI Kit · %s · V1.0", p->name ? p->name : "hub");
        lbl(body, foot, p->t4, hub_font());
    }
}

void hub_build_sensor_cal(lv_obj_t *parent)
{
    lv_obj_t *shell = hub_shell_create(parent, hub_tr("传感校准", "Sensor calibration"), true);
    lv_obj_t *body = hub_shell_body(shell);
    hub_build_climate_offset_block(body);
}

void hub_build_schedule(lv_obj_t *parent)
{
    const hub_palette_t *p = hub_palette();
    hub_model_t *m = hub_model();
    s_sched_sheet = NULL;
    s_sched_hh = NULL;
    s_sched_mm = NULL;
    memset(s_sched_day_btns, 0, sizeof(s_sched_day_btns));
    s_sched_edit_idx = -1;

    lv_obj_t *shell = hub_shell_create(parent, hub_tr("日程", "Schedule"), true);
    lv_obj_t *body = hub_shell_body(shell);
    sched_build_editor(parent);

    lbl(body,
        hub_tr("点左侧改时间/日期 · 开关启用 · 「执行」跑情景",
               "Tap left to edit time/days · Toggle · Run"),
        p->t3, hub_font());

    for (int i = 0; i < HUB_SCHED_COUNT; i++) {
        hub_sched_t *s = &m->schedules[i];

        lv_obj_t *card = lv_obj_create(body);
        lv_obj_remove_style_all(card);
        lv_obj_set_width(card, LV_PCT(100));
        lv_obj_set_height(card, 96);
        lv_obj_set_style_radius(card, p->radius, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(card, p->bg_card, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, p->line, 0);
        lv_obj_set_style_pad_all(card, 10, 0);
        lv_obj_set_style_pad_column(card, 10, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        theme_card_chrome(card, s->on);

        lv_obj_t *bar = lv_obj_create(card);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, 4, 56);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar, s->on ? p->accent : p->line, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

        /* Mid column is the edit hit target */
        lv_obj_t *mid = lv_btn_create(card);
        lv_obj_remove_style_all(mid);
        lv_obj_set_flex_grow(mid, 1);
        lv_obj_set_height(mid, LV_PCT(100));
        lv_obj_set_flex_flow(mid, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(mid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(mid, 2, 0);
        lv_obj_clear_flag(mid, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(mid, sched_edit_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        lv_obj_t *line1 = lv_obj_create(mid);
        lv_obj_remove_style_all(line1);
        lv_obj_set_width(line1, LV_PCT(100));
        lv_obj_set_height(line1, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(line1, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(line1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(line1, 10, 0);
        lv_obj_clear_flag(line1, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *tm = lv_label_create(line1);
        lv_label_set_text(tm, s->time);
        hub_style_label(tm, p->t1, hub_font());
        lv_obj_t *st = lv_label_create(line1);
        lv_label_set_text(st, s->on ? hub_tr("开", "ON") : hub_tr("关", "OFF"));
        hub_style_label(st, s->on ? p->on : p->t4, hub_font());
        lv_obj_t *hint = lv_label_create(line1);
        lv_label_set_text(hint, hub_tr("改", "Edit"));
        hub_style_label(hint, p->accent, hub_font());

        lv_obj_t *ti = lv_label_create(mid);
        lv_label_set_text(ti, hub_model_sched_title(i));
        hub_style_label(ti, p->t1, hub_font());
        lv_label_set_long_mode(ti, LV_LABEL_LONG_DOT);
        lv_obj_set_width(ti, LV_PCT(100));

        char meta[48];
        snprintf(meta, sizeof(meta), "%s · %s", hub_model_sched_days(i), hub_model_sched_action(i));
        lv_obj_t *me = lv_label_create(mid);
        lv_label_set_text(me, meta);
        hub_style_label(me, p->t3, hub_font());
        lv_label_set_long_mode(me, LV_LABEL_LONG_DOT);
        lv_obj_set_width(me, LV_PCT(100));

        lv_obj_t *acts = lv_obj_create(card);
        lv_obj_remove_style_all(acts);
        lv_obj_set_size(acts, LV_SIZE_CONTENT, LV_PCT(100));
        lv_obj_set_flex_flow(acts, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(acts, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(acts, 8, 0);
        lv_obj_clear_flag(acts, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *run = lv_btn_create(acts);
        lv_obj_remove_style_all(run);
        lv_obj_set_size(run, 52, 36);
        lv_obj_set_style_radius(run, p->radius, 0);
        lv_obj_set_style_bg_opa(run, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(run, s->on ? p->accent : p->bg_card2, 0);
        lv_obj_set_style_border_width(run, 1, 0);
        lv_obj_set_style_border_color(run, p->line, 0);
        if (s->on) {
            lv_obj_add_event_cb(run, sched_run_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        } else {
            lv_obj_set_style_opa(run, LV_OPA_50, 0);
        }
        lv_obj_t *rl = lv_label_create(run);
        lv_label_set_text(rl, hub_tr("执行", "Run"));
        hub_style_label(rl, s->on ? p->ink_on : p->t4, hub_font());
        lv_obj_center(rl);

        mk_switch(acts, s->on, sched_toggle_cb, (void *)(uintptr_t)i);
    }

    lv_obj_t *cta = lv_btn_create(body);
    lv_obj_remove_style_all(cta);
    lv_obj_set_width(cta, LV_PCT(100));
    lv_obj_set_height(cta, 44);
    lv_obj_set_style_radius(cta, p->radius, 0);
    lv_obj_set_style_bg_opa(cta, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cta, p->t1, 0);
    lv_obj_add_event_cb(cta, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_SCENES);
    lv_obj_t *cl = lv_label_create(cta);
    lv_label_set_text(cl, hub_tr("前往情景中心 →", "Scenes →"));
    hub_style_label(cl, p->ink_on, hub_font());
    lv_obj_center(cl);
}

void hub_build_hvac(lv_obj_t *parent)
{
    const hub_palette_t *p = hub_palette();
    hub_model_t *m = hub_model();
    hub_room_dev_t *d = &m->rooms[m->room_idx];

    lv_obj_t *shell = hub_shell_create(parent, hub_tr("温控", "Climate"), true);
    lv_obj_t *body = hub_shell_body(shell);
    hub_shell_set_dots(shell, 3, 0);

    lv_obj_t *ac = lv_obj_create(body);
    lv_obj_remove_style_all(ac);
    hub_apply_card(ac, d->ac_on);
    theme_card_chrome(ac, d->ac_on);
    lv_obj_set_width(ac, LV_PCT(100));
    lv_obj_set_height(ac, 160);
    lv_obj_set_style_radius(ac, p->radius, 0);

    hub_ico_add(ac, HUB_ICO_SNOW, p->cool, 28);
    lv_obj_align(lv_obj_get_child(ac, 0), LV_ALIGN_TOP_LEFT, 12, 12);
    lbl(ac, "AC", p->t1, hub_font());
    lv_obj_align(lv_obj_get_child(ac, 1), LV_ALIGN_TOP_LEFT, 48, 16);
    lbl(ac, hub_model_room_name(m->room_idx), p->t3, hub_font());
    lv_obj_align(lv_obj_get_child(ac, 2), LV_ALIGN_TOP_LEFT, 48, 34);

    lv_obj_t *acpwr = lv_btn_create(ac);
    lv_obj_remove_style_all(acpwr);
    lv_obj_set_size(acpwr, 36, 36);
    lv_obj_set_style_radius(acpwr, p->radius, 0);
    lv_obj_set_style_bg_opa(acpwr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(acpwr, d->ac_on ? p->accent : p->bg_card2, 0);
    lv_obj_align(acpwr, LV_ALIGN_TOP_RIGHT, -12, 12);
    lv_obj_add_event_cb(acpwr, ac_toggle_cb, LV_EVENT_CLICKED, NULL);
    hub_ico_add(acpwr, HUB_ICO_POWER, d->ac_on ? p->ink_on : p->t2, 14);
    lv_obj_center(lv_obj_get_child(acpwr, 0));

    char sp[16];
    snprintf(sp, sizeof(sp), d->ac_on ? "%d°" : "--", d->ac_sp);
    lv_obj_t *sv = lv_label_create(ac);
    lv_label_set_text(sv, sp);
    hub_style_label(sv, d->ac_on ? p->cool : p->t4, hub_font_clock_lg());
    lv_obj_align(sv, LV_ALIGN_LEFT_MID, 12, 10);

    {
        char amb[48];
        if (hub_climate_ui_available()) {
            snprintf(amb, sizeof(amb),
                     hub_tr("室内 %.1f%s / %d%s", "Indoor %.1f%s / %d%s"),
                     (double)hub_climate_display_temp_c(), HUB_UNIT_C,
                     hub_climate_display_rh(), HUB_UNIT_RH_FULL);
        } else {
            snprintf(amb, sizeof(amb), hub_tr("室内 --%s / --%s", "Indoor --%s / --%s"),
                     HUB_UNIT_C, HUB_UNIT_RH_FULL);
        }
        lv_obj_t *al = lbl(ac, amb, hub_climate_ui_available() ? p->t3 : p->t4, hub_font());
        lv_obj_align(al, LV_ALIGN_BOTTOM_LEFT, 12, -14);
    }

    lv_obj_t *am = lv_btn_create(ac);
    lv_obj_remove_style_all(am);
    lv_obj_set_size(am, 48, 40);
    lv_obj_set_style_radius(am, p->radius, 0);
    lv_obj_set_style_border_width(am, 1, 0);
    lv_obj_set_style_border_color(am, p->line, 0);
    lv_obj_align(am, LV_ALIGN_BOTTOM_RIGHT, -68, -12);
    lv_obj_add_event_cb(am, ac_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    hub_ico_add(am, HUB_ICO_MINUS, p->t1, 18);
    lv_obj_center(lv_obj_get_child(am, 0));

    lv_obj_t *ap = lv_btn_create(ac);
    lv_obj_remove_style_all(ap);
    lv_obj_set_size(ap, 48, 40);
    lv_obj_set_style_radius(ap, p->radius, 0);
    lv_obj_set_style_border_width(ap, 1, 0);
    lv_obj_set_style_border_color(ap, p->line, 0);
    lv_obj_align(ap, LV_ALIGN_BOTTOM_RIGHT, -12, -12);
    lv_obj_add_event_cb(ap, ac_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    hub_ico_add(ap, HUB_ICO_PLUS, p->t1, 18);
    lv_obj_center(lv_obj_get_child(ap, 0));

    lv_obj_t *ht = lv_obj_create(body);
    lv_obj_remove_style_all(ht);
    hub_apply_card(ht, d->heat_on);
    theme_card_chrome(ht, d->heat_on);
    lv_obj_set_width(ht, LV_PCT(100));
    lv_obj_set_height(ht, 140);
    lv_obj_set_style_radius(ht, p->radius, 0);

    hub_ico_add(ht, HUB_ICO_GAUGE, p->heat, 28);
    lv_obj_align(lv_obj_get_child(ht, 0), LV_ALIGN_TOP_LEFT, 12, 12);
    lbl(ht, hub_tr("地暖", "Floor heat"), p->t1, hub_font());
    lv_obj_align(lv_obj_get_child(ht, 1), LV_ALIGN_TOP_LEFT, 48, 16);

    lv_obj_t *hp = lv_btn_create(ht);
    lv_obj_remove_style_all(hp);
    lv_obj_set_size(hp, 36, 36);
    lv_obj_set_style_radius(hp, p->radius, 0);
    lv_obj_set_style_bg_opa(hp, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(hp, d->heat_on ? p->accent : p->bg_card2, 0);
    lv_obj_align(hp, LV_ALIGN_TOP_RIGHT, -12, 12);
    lv_obj_add_event_cb(hp, heat_toggle_cb, LV_EVENT_CLICKED, NULL);
    hub_ico_add(hp, HUB_ICO_POWER, d->heat_on ? p->ink_on : p->t2, 14);
    lv_obj_center(lv_obj_get_child(hp, 0));

    char hsp[16];
    snprintf(hsp, sizeof(hsp), d->heat_on ? "%d°" : "--", d->heat_sp);
    lv_obj_t *hv = lv_label_create(ht);
    lv_label_set_text(hv, hsp);
    hub_style_label(hv, d->heat_on ? p->heat : p->t4, hub_font_clock_lg());
    lv_obj_align(hv, LV_ALIGN_LEFT_MID, 12, 8);

    lv_obj_t *hm = lv_btn_create(ht);
    lv_obj_remove_style_all(hm);
    lv_obj_set_size(hm, 48, 40);
    lv_obj_set_style_radius(hm, p->radius, 0);
    lv_obj_set_style_border_width(hm, 1, 0);
    lv_obj_set_style_border_color(hm, p->line, 0);
    lv_obj_align(hm, LV_ALIGN_BOTTOM_RIGHT, -68, -12);
    lv_obj_add_event_cb(hm, heat_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    hub_ico_add(hm, HUB_ICO_MINUS, p->t1, 18);
    lv_obj_center(lv_obj_get_child(hm, 0));

    lv_obj_t *hpp = lv_btn_create(ht);
    lv_obj_remove_style_all(hpp);
    lv_obj_set_size(hpp, 48, 40);
    lv_obj_set_style_radius(hpp, p->radius, 0);
    lv_obj_set_style_border_width(hpp, 1, 0);
    lv_obj_set_style_border_color(hpp, p->line, 0);
    lv_obj_align(hpp, LV_ALIGN_BOTTOM_RIGHT, -12, -12);
    lv_obj_add_event_cb(hpp, heat_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    hub_ico_add(hpp, HUB_ICO_PLUS, p->t1, 18);
    lv_obj_center(lv_obj_get_child(hpp, 0));
}
