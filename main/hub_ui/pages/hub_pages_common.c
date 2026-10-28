#include "hub_pages_common.h"

#include "hub_ui.h"
#include "hub_theme.h"
#include "hub_font.h"
#include "hub_model.h"
#include "hub_icons.h"
#include "hub_device_ui.h"
#include "hub_i18n.h"
#include "hub_climate_offset.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static void go_cb(lv_event_t *e)
{
    hub_ui_go((hub_route_t)(uintptr_t)lv_event_get_user_data(e));
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    hub_ui_back();
}

static void scene_cb(lv_event_t *e)
{
    const char *id = (const char *)lv_event_get_user_data(e);
    hub_model_apply_scene(id);
    hub_ui_refresh();
}

static void proto_cb(lv_event_t *e)
{
    const char *id = (const char *)lv_event_get_user_data(e);
    if (id && strcmp(id, "wifi") == 0) {
        hub_ui_go(HUB_ROUTE_NETWORK);
        return;
    }
    hub_model_toggle_proto(id);
    hub_ui_refresh();
}

static lv_obj_t *mk(lv_obj_t *par, const char *txt, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(par);
    lv_label_set_text(l, txt ? txt : "");
    hub_style_label(l, c, hub_font());
    return l;
}

/** HubScenesPage — 2×3 hue grid (FE shared across themes). */
void hub_build_scenes(lv_obj_t *parent)
{
    const hub_palette_t *p = hub_palette();
    hub_model_t *m = hub_model();
    lv_obj_t *shell = hub_shell_create(parent, hub_tr("情景", "Scenes"), true);
    lv_obj_t *body = hub_shell_body(shell);
    hub_shell_set_dots(shell, 4, 2);

    char right[48];
    snprintf(right, sizeof(right), hub_tr("当前 · %s", "Now · %s"),
             hub_model_scene_label(m->active_scene));
    mk(body, right, p->accent);

    struct {
        const char *id;
        hub_ico_t ico;
        lv_color_t hue;
    } sc[] = {
        { "home", HUB_ICO_HOME, LV_COLOR_MAKE(52, 211, 153) },
        { "away", HUB_ICO_AWAY, LV_COLOR_MAKE(245, 158, 11) },
        { "movie", HUB_ICO_MOON, LV_COLOR_MAKE(129, 140, 248) },
        { "sleep", HUB_ICO_MOON_SLEEP, LV_COLOR_MAKE(45, 212, 191) },
        { "guest", HUB_ICO_BULB, LV_COLOR_MAKE(234, 179, 8) },
        { "eco", HUB_ICO_GAUGE, LV_COLOR_MAKE(34, 211, 238) },
    };

    lv_obj_t *grid = lv_obj_create(body);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_row(grid, 8, 0);
    lv_obj_set_style_pad_column(grid, 8, 0);
    static lv_coord_t cols[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t rows[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(grid, cols, rows);

    bool zen = p->name && strcmp(p->name, "zen") == 0;
    bool metro = p->name && strcmp(p->name, "metro") == 0;

    for (int i = 0; i < 6; i++) {
        bool active = strcmp(m->active_scene, sc[i].id) == 0;
        lv_obj_t *b = lv_btn_create(grid);
        lv_obj_remove_style_all(b);
        hub_apply_card(b, active);
        theme_card_chrome(b, active);
        lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, i % 2, 1, LV_GRID_ALIGN_STRETCH, i / 2, 1);
        lv_obj_set_style_pad_all(b, 12, 0);
        lv_obj_set_style_radius(b, (zen || metro) ? 0 : p->radius, 0);
        if (active) {
            lv_color_t bg = lv_color_mix(sc[i].hue, p->bg_card, LV_OPA_30);
            lv_obj_set_style_bg_color(b, bg, 0);
            lv_obj_set_style_border_color(b, sc[i].hue, 0);
            lv_obj_set_style_border_width(b, 1, 0);
        }
        lv_obj_add_event_cb(b, scene_cb, LV_EVENT_CLICKED, (void *)sc[i].id);

        lv_obj_t *badge = lv_obj_create(b);
        lv_obj_remove_style_all(badge);
        lv_obj_set_size(badge, 36, 36);
        lv_obj_set_style_radius(badge, zen ? 0 : 10, 0);
        lv_obj_set_style_bg_color(badge, lv_color_mix(sc[i].hue, p->bg_card, LV_OPA_20), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_align(badge, LV_ALIGN_TOP_LEFT, 0, 0);
        hub_ico_add(badge, sc[i].ico, sc[i].hue, 18);
        lv_obj_center(lv_obj_get_child(badge, 0));

        lv_obj_t *title = lv_label_create(b);
        lv_label_set_text(title, hub_model_scene_label(sc[i].id));
        hub_style_label(title, p->t1, hub_font());
        lv_obj_align(title, LV_ALIGN_BOTTOM_LEFT, 0, -18);

        lv_obj_t *sub = lv_label_create(b);
        lv_label_set_text(sub, hub_model_scene_sub(sc[i].id));
        hub_style_label(sub, p->t3, hub_font());
        lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 0, -2);
    }
}

/** TplEnergy — Today kWh + category bars. */
void hub_build_energy(lv_obj_t *parent)
{
    const hub_palette_t *p = hub_palette();
    hub_model_t *m = hub_model();
    lv_obj_t *shell = hub_shell_create(parent, hub_tr("能耗", "Energy"), true);
    lv_obj_t *body = hub_shell_body(shell);
    hub_shell_set_dots(shell, 4, 3);

    lv_obj_t *hero = lv_obj_create(body);
    lv_obj_remove_style_all(hero);
    hub_apply_card(hero, false);
    theme_card_chrome(hero, false);
    lv_obj_set_width(hero, LV_PCT(100));
    lv_obj_set_height(hero, 110);
    lv_obj_set_style_pad_all(hero, 14, 0);
    mk(hero, hub_tr("今日用电", "Today"), p->t3);
    lv_obj_align(lv_obj_get_child(hero, 0), LV_ALIGN_TOP_LEFT, 0, 0);

    double today = 12.0 + (double)m->power_kw * 2.1;
    char kwh[24];
    snprintf(kwh, sizeof(kwh), "%.1f", today);
    lv_obj_t *big = lv_label_create(hero);
    lv_label_set_text(big, kwh);
    hub_style_label(big, p->t1, hub_font_clock());
    lv_obj_align(big, LV_ALIGN_LEFT_MID, 0, 4);
    lv_obj_t *unit = lv_label_create(hero);
    lv_label_set_text(unit, "kWh");
    hub_style_label(unit, p->t3, hub_font());
    lv_obj_align_to(unit, big, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -4);

    char meta[80];
    snprintf(meta, sizeof(meta), hub_tr("峰值 %.1f kW · 室内 %.1f° · RH %d%%",
                                       "Peak %.1f kW · Indoor %.1f° · RH %d%%"),
             (double)m->power_kw, (double)m->indoor_c, m->rh);
    lv_obj_t *meta_l = lv_label_create(hero);
    lv_label_set_text(meta_l, meta);
    hub_style_label(meta_l, p->t3, hub_font());
    lv_obj_align(meta_l, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    struct {
        const char *zh;
        const char *en;
        int pct;
        lv_color_t hue;
    } bars[] = {
        { "照明", "Lighting", 28, LV_COLOR_MAKE(234, 179, 8) },
        { "温控", "Climate", 46, LV_COLOR_MAKE(34, 211, 238) },
        { "插座", "Outlets", 18, LV_COLOR_MAKE(129, 140, 248) },
        { "其他", "Other", 8, LV_COLOR_MAKE(125, 135, 146) },
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *row = lv_obj_create(body);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 36);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        mk(row, hub_tr(bars[i].zh, bars[i].en), p->t1);
        lv_obj_align(lv_obj_get_child(row, 0), LV_ALIGN_TOP_LEFT, 0, 0);
        char pct[8];
        snprintf(pct, sizeof(pct), "%d%%", bars[i].pct);
        mk(row, pct, p->t3);
        lv_obj_align(lv_obj_get_child(row, 1), LV_ALIGN_TOP_RIGHT, 0, 0);

        lv_obj_t *rail = lv_obj_create(row);
        lv_obj_remove_style_all(rail);
        lv_obj_set_size(rail, LV_PCT(100), 8);
        lv_obj_set_style_radius(rail, (p->radius == 0) ? 0 : 2, 0);
        lv_obj_set_style_bg_color(rail, p->bg_elev, 0);
        lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, 0);
        lv_obj_align(rail, LV_ALIGN_BOTTOM_MID, 0, 0);

        lv_obj_t *fill = lv_obj_create(rail);
        lv_obj_remove_style_all(fill);
        lv_obj_set_height(fill, LV_PCT(100));
        lv_obj_set_width(fill, LV_PCT(bars[i].pct));
        lv_obj_set_style_radius(fill, (p->radius == 0) ? 0 : 2, 0);
        lv_obj_set_style_bg_color(fill, bars[i].hue, 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
        lv_obj_align(fill, LV_ALIGN_LEFT_MID, 0, 0);
    }

    lv_obj_t *tip = lv_btn_create(body);
    lv_obj_remove_style_all(tip);
    hub_apply_card(tip, false);
    theme_card_chrome(tip, false);
    lv_obj_set_width(tip, LV_PCT(100));
    lv_obj_set_height(tip, 44);
    lv_obj_add_event_cb(tip, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_GATEWAY);
    mk(tip, hub_tr("查看计量总线 →", "Metering bus →"), p->t1);
    lv_obj_center(lv_obj_get_child(tip, 0));
}

/** HubGatewayPage + ProtoPill. */
void hub_build_gateway(lv_obj_t *parent)
{
    const hub_palette_t *p = hub_palette();
    hub_model_t *m = hub_model();
    lv_obj_t *shell = hub_shell_create(parent, hub_tr("总线", "Gateway"), true);
    lv_obj_t *body = hub_shell_body(shell);

    char head[32];
    snprintf(head, sizeof(head), hub_tr("%d / %d 在线", "%d / %d online"),
             hub_model_ok_count(), HUB_PROTO_COUNT);
    mk(body, head, p->t3);

    for (int i = 0; i < HUB_PROTO_COUNT; i++) {
        bool ok = m->protos[i].ok;
        lv_obj_t *card = lv_btn_create(body);
        lv_obj_remove_style_all(card);
        hub_apply_card(card, false);
        theme_card_chrome(card, ok);
        lv_obj_set_width(card, LV_PCT(100));
        lv_obj_set_height(card, 56);
        lv_obj_set_style_pad_hor(card, 12, 0);
        lv_obj_add_event_cb(card, proto_cb, LV_EVENT_CLICKED, (void *)m->protos[i].id);

        lv_obj_t *badge = lv_obj_create(card);
        lv_obj_remove_style_all(badge);
        lv_obj_set_size(badge, 36, 36);
        lv_obj_set_style_radius(badge, (p->radius == 0) ? 0 : 10, 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(badge, ok ? lv_color_mix(p->on, p->bg_card, LV_OPA_20)
                                           : lv_color_mix(p->alert, p->bg_card, LV_OPA_20), 0);
        lv_obj_align(badge, LV_ALIGN_LEFT_MID, 0, 0);
        hub_ico_add(badge, hub_ico_for_proto(m->protos[i].id), ok ? p->on : p->alert, 18);
        lv_obj_center(lv_obj_get_child(badge, 0));

        mk(card, m->protos[i].name, p->t1);
        lv_obj_align(lv_obj_get_child(card, 1), LV_ALIGN_LEFT_MID, 48, -8);
        mk(card, hub_tr("点击切换", "Tap to toggle"), p->t3);
        lv_obj_align(lv_obj_get_child(card, 2), LV_ALIGN_LEFT_MID, 48, 10);

        lv_obj_t *pill = lv_obj_create(card);
        lv_obj_remove_style_all(pill);
        lv_obj_set_size(pill, 56, 24);
        lv_obj_set_style_radius(pill, 4, 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(pill, ok ? lv_color_mix(p->on, p->bg_card, LV_OPA_20)
                                           : lv_color_mix(p->alert, p->bg_card, LV_OPA_20), 0);
        lv_obj_set_style_border_width(pill, 1, 0);
        lv_obj_set_style_border_color(pill, ok ? p->on : p->alert, 0);
        lv_obj_align(pill, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_t *pl = lv_label_create(pill);
        lv_label_set_text(pl, ok ? "OK" : "Fault");
        hub_style_label(pl, ok ? p->on : p->alert, hub_font());
        lv_obj_center(pl);
    }

    lv_obj_t *pts = lv_btn_create(body);
    lv_obj_remove_style_all(pts);
    hub_apply_card(pts, false);
    theme_card_chrome(pts, false);
    lv_obj_set_width(pts, LV_PCT(100));
    lv_obj_set_height(pts, 44);
    lv_obj_add_event_cb(pts, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_POINTS);
    mk(pts, hub_tr("查看点表 →", "Point table →"), p->t1);
    lv_obj_center(lv_obj_get_child(pts, 0));
}

/** TplPointTable. */
void hub_build_points(lv_obj_t *parent)
{
    const hub_palette_t *p = hub_palette();
    lv_obj_t *shell = hub_shell_create(parent, hub_tr("点表", "Points"), true);
    lv_obj_t *body = hub_shell_body(shell);

    struct {
        const char *zh;
        const char *en;
        const char *proto;
        const char *addr;
        const char *type;
    } pts[] = {
        { "客厅主灯", "Living light", "Modbus", "1 / 0x0001", "Coil" },
        { "客厅灯带", "Living strip", "Modbus", "1 / 0x000A", "Hold" },
        { "空调设定", "AC setpoint", "MQTT", "home/lr/ac", "Num" },
        { "窗帘", "Curtain", "RS485", "BusA · ID3", "Hold" },
        { "入户门", "Entry door", "MQTT", "home/entry", "Bool" },
        { "地暖", "Floor heat", "Modbus", "2 / 0x0100", "Hold" },
    };
    for (int i = 0; i < 6; i++) {
        lv_obj_t *row = lv_obj_create(body);
        lv_obj_remove_style_all(row);
        hub_apply_card(row, false);
        theme_card_chrome(row, false);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 44);
        lv_obj_set_style_pad_hor(row, 10, 0);
        lv_obj_set_style_radius(row, (p->radius == 0) ? 0 : 8, 0);

        mk(row, hub_tr(pts[i].zh, pts[i].en), p->t1);
        lv_obj_align(lv_obj_get_child(row, 0), LV_ALIGN_LEFT_MID, 0, 0);
        char right[40];
        snprintf(right, sizeof(right), "%s · %s", pts[i].proto, pts[i].type);
        mk(row, right, p->t3);
        lv_obj_align(lv_obj_get_child(row, 1), LV_ALIGN_RIGHT_MID, 0, -8);
        mk(row, pts[i].addr, p->t4);
        lv_obj_align(lv_obj_get_child(row, 2), LV_ALIGN_RIGHT_MID, 0, 8);
    }
}

/** FE TplMinimalHome — clock top, 2×2 SceneBtn pinned to bottom (absolute layout). */
void hub_build_standby(lv_obj_t *parent)
{
    const hub_palette_t *p = hub_palette();
    hub_model_t *m = hub_model();
    bool zen = p->radius == 0;

    /* Fixed 480 canvas — avoid flex-grow pushing the grid off-screen. */
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 480, 480);
    lv_obj_set_style_bg_color(root, p->bg_deep, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    /* Return to previous page (e.g. settings preview) when possible. */
    lv_obj_add_event_cb(root, back_cb, LV_EVENT_CLICKED, NULL);

    /* Top: clock + caption + climate (FE padding 36/24) */
    lv_obj_t *clk = hub_clock_label(root, p->t1, hub_font_clock_lg());
    lv_obj_set_style_text_letter_space(clk, -2, 0);
    lv_obj_align(clk, LV_ALIGN_TOP_LEFT, 24, 32);
    lv_obj_clear_flag(clk, LV_OBJ_FLAG_CLICKABLE);

    char sub[72];
    snprintf(sub, sizeof(sub), "%s · %s",
             hub_tr("低功耗待机", "Low-power"),
             hub_model_scene_label(m->active_scene));
    lv_obj_t *cap = mk(root, sub, p->t3);
    lv_obj_align_to(cap, clk, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_obj_clear_flag(cap, LV_OBJ_FLAG_CLICKABLE);

    /*
     * Indoor temp / RH — use hub_font (not clock): clock lacks ° / % / · glyphs,
     * which showed as gaps between the two values. RH via %s so '%' is not eaten
     * by snprintf. Prefer ASCII " / " separator (always in Latin fallback).
     */
    hub_climate_offset_init();
    char tbuf[24], rbuf[24];
    if (hub_climate_ui_available()) {
        snprintf(tbuf, sizeof(tbuf), "%.1f%s", (double)hub_climate_display_temp_c(), HUB_UNIT_C);
        snprintf(rbuf, sizeof(rbuf), "%d%s", hub_climate_display_rh(), HUB_UNIT_RH_FULL);
    } else {
        snprintf(tbuf, sizeof(tbuf), "--%s", HUB_UNIT_C);
        snprintf(rbuf, sizeof(rbuf), "--%s", HUB_UNIT_RH_FULL);
    }
    lv_obj_t *clim_row = lv_obj_create(root);
    lv_obj_remove_style_all(clim_row);
    lv_obj_set_size(clim_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(clim_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(clim_row, 12, 0);
    lv_obj_set_flex_align(clim_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(clim_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *tv = mk(clim_row, tbuf, hub_climate_ui_available() ? p->t1 : p->t3);
    lv_obj_set_style_text_font(tv, hub_font(), 0);
    lv_obj_t *sep = mk(clim_row, "/", p->t3);
    lv_obj_set_style_text_font(sep, hub_font(), 0);
    lv_obj_t *rv = mk(clim_row, rbuf, hub_climate_ui_available() ? p->t1 : p->t3);
    lv_obj_set_style_text_font(rv, hub_font(), 0);
    lv_obj_align_to(clim_row, cap, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    /*
     * Bottom 2×2 pinned to canvas (slightly taller cells for +3 type).
     * Absolute layout — flex-grow spacer previously pushed this grid off-screen
     * so visual coords and touch hit-tests no longer matched.
     */
    const lv_coord_t cell_w = 218;
    const lv_coord_t cell_h = 108;
    const lv_coord_t gap = 8;
    const lv_coord_t grid_w = cell_w * 2 + gap; /* 444 = 480 - 18*2 */
    const lv_coord_t grid_h = cell_h * 2 + gap;
    lv_obj_t *grid = lv_obj_create(root);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, grid_w, grid_h);
    lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    static const struct {
        const char *id;
        hub_ico_t ico;
        const char *title_zh;
        const char *title_en;
        const char *sub_zh;
        const char *sub_en;
        int hue_kind;
    } cells[] = {
        { "alloff", HUB_ICO_POWER, "全部关闭", "All off", "广播", "Broadcast", 0 },
        { "home", HUB_ICO_HOME, "舒适", "Comfort", "空调+灯", "AC + lights", 1 },
        { "away", HUB_ICO_SHIELD, "离家", "Away", "安防", "Security", 2 },
        { "bus", HUB_ICO_BUS, "总线", "Bus", "协议", "Protocols", 3 },
    };

    for (int i = 0; i < 4; i++) {
        lv_color_t hue = p->t2;
        if (cells[i].hue_kind == 1) {
            hue = p->accent;
        } else if (cells[i].hue_kind == 2) {
            hue = p->warm;
        } else if (cells[i].hue_kind == 3) {
            hue = p->cool;
        }

        const lv_coord_t x = (i % 2) * (cell_w + gap);
        const lv_coord_t y = (i / 2) * (cell_h + gap);

        lv_obj_t *btn = lv_btn_create(grid);
        lv_obj_remove_style_all(btn);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_size(btn, cell_w, cell_h);
        lv_obj_set_style_radius(btn, zen ? 0 : 12, 0);
        lv_obj_set_style_bg_color(btn, p->bg_card, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, p->line, 0);
        lv_obj_set_style_pad_all(btn, 12, 0);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(btn, 8, 0);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        if (strcmp(cells[i].id, "bus") == 0) {
            lv_obj_add_event_cb(btn, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_GATEWAY);
        } else {
            lv_obj_add_event_cb(btn, scene_cb, LV_EVENT_CLICKED, (void *)cells[i].id);
        }

        /* FE SceneBtn: 36 badge above title/sub; children not clickable → hit btn */
        hub_ico_badge(btn, cells[i].ico, hue, 36, 18);

        lv_obj_t *col = lv_obj_create(btn);
        lv_obj_remove_style_all(col);
        lv_obj_set_width(col, LV_PCT(100));
        lv_obj_set_height(col, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(col, 2, 0);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        mk(col, hub_tr(cells[i].title_zh, cells[i].title_en), p->t1);
        mk(col, hub_tr(cells[i].sub_zh, cells[i].sub_en), p->t3);
    }
}
