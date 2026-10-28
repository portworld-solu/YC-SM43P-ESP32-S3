#include "hub_room_ui.h"

#include "hub_ui.h"
#include "hub_theme.h"
#include "hub_font.h"
#include "hub_model.h"
#include "hub_icons.h"
#include "hub_i18n.h"
#include "app_bus_bridge.h"
#include "app_beep.h"
#include "app_modbus_map.h"

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef CONFIG_HUB_UI_ENGINEER_EDIT
#define CONFIG_HUB_UI_ENGINEER_EDIT 0
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define PAGE_SIZE 6
/**
 * Slot model mirrors Smart-LVGL-main_v2.0 `lv_nt_slot` + `lv_nt_layout`:
 *   n=1        → SINGLE  (full page)
 *   n=2 / n=3  → TALL    (full-width rows; 二分 / 三分)
 *   n=5 idx0   → WIDE    (top span; still compact vertically)
 *   n=4/5/6    → HALF    (2-col cells; 四分 / 六分)
 * Body height ~340px after chrome on 480×480.
 */
#define ROOM_GRID_GAP_ROW 12
#define ROOM_GRID_GAP_COL 12

typedef enum {
    ROOM_TILE_SINGLE = 0, /**< n=1 单个 */
    ROOM_TILE_TALL = 1,   /**< n=2 二分 / n=3 三分 — full width */
    ROOM_TILE_WIDE = 2,   /**< n=5 index0 — full-width top */
    ROOM_TILE_HALF = 3    /**< n=4 / n=5 rest / n=6 — half width */
} room_tile_t;

typedef struct {
    lv_coord_t pad;
    lv_coord_t gap;
    lv_coord_t badge;
    lv_coord_t ico;
    lv_coord_t pwr;
    lv_coord_t pwr_ico;
    lv_coord_t ctrl_h;
    lv_coord_t ctrl_ico;
    lv_coord_t step;
    lv_coord_t slider_h;
    bool use_spacer; /**< SINGLE: push footer down */
    bool big_value;  /**< larger temp / value font */
} room_tile_metrics_t;

/** Per-cell slot from page count + index (Smart-LVGL `lv_nt_slot_from`). */
static room_tile_t tile_for_slot(int n, int index)
{
    if (n <= 1) {
        return ROOM_TILE_SINGLE;
    }
    if (n == 5 && index == 0) {
        return ROOM_TILE_WIDE;
    }
    if (n == 2 || n == 3) {
        return ROOM_TILE_TALL;
    }
    return ROOM_TILE_HALF;
}

static void tile_metrics(room_tile_t t, room_tile_metrics_t *m)
{
    /* Tuned from Smart-LVGL lv_nt_slot_metrics + 480 budget. */
    static const room_tile_metrics_t k_tab[] = {
        /* SINGLE */ { 16, 14, 52, 28, 48, 22, 52, 22, 48, 18, true,  true  },
        /* TALL   */ { 12, 10, 44, 24, 40, 18, 44, 18, 40, 14, false, true  },
        /* WIDE   */ { 12, 8,  42, 22, 42, 18, 42, 18, 40, 14, false, true  },
        /* HALF   */ { 10, 8,  36, 20, 36, 16, 36, 16, 34, 12, false, false },
    };
    *m = k_tab[(int)t];
}

typedef struct {
    int room;
    int slot;
    int delta;
} w_ud_t;

static w_ud_t s_ud_pool[32];
static int s_ud_n;
static int s_mb_edit_slot;

void hub_room_mb_edit_set_slot(int slot)
{
    s_mb_edit_slot = slot;
}

static w_ud_t *ud_alloc(int room, int slot, int delta)
{
    if (s_ud_n >= 32) {
        s_ud_n = 0;
    }
    w_ud_t *u = &s_ud_pool[s_ud_n++];
    u->room = room;
    u->slot = slot;
    u->delta = delta;
    return u;
}

static void refresh_cb(lv_event_t *e)
{
    (void)e;
    hub_ui_refresh();
}

static void go_edit_cb(lv_event_t *e)
{
    (void)e;
    hub_ui_go(HUB_ROUTE_ROOM_EDIT);
}

static void go_room_cb(lv_event_t *e)
{
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    hub_model_set_room(idx);
    hub_ui_go(HUB_ROUTE_ROOM);
}

static void go_home_cb(lv_event_t *e)
{
    (void)e;
    hub_ui_go(HUB_ROUTE_HOME);
}

static void go_back_cb(lv_event_t *e)
{
    (void)e;
    hub_ui_back();
}

static void go_scenes_cb(lv_event_t *e)
{
    (void)e;
    hub_ui_go(HUB_ROUTE_SCENES);
}

static void subpage_cb(lv_event_t *e)
{
    /* Kept for potential UI chrome; primary paging is hub_nav swipe. */
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    hub_model_t *m = hub_model();
    int enabled = hub_model_enabled_widget_count(m->room_idx);
    int pages = enabled > 0 ? (enabled + PAGE_SIZE - 1) / PAGE_SIZE : 1;
    if (dir > 0) {
        if (m->room_subpage < pages - 1) {
            m->room_subpage++;
            hub_ui_refresh();
        } else {
            hub_ui_go(HUB_ROUTE_SCENES);
        }
    } else {
        if (m->room_subpage > 0) {
            m->room_subpage--;
            hub_ui_refresh();
        } else {
            hub_ui_go(HUB_ROUTE_HOME);
        }
    }
}

static void bus_notify(int room, int slot)
{
    app_beep_click_if_enabled();
    /* Non-blocking: encode from map + queue FC06. No map row → no-op. */
    (void)app_bus_bridge_notify_widget(room, slot);
}

static void toggle_slot_cb(lv_event_t *e)
{
    w_ud_t *u = (w_ud_t *)lv_event_get_user_data(e);
    hub_model_toggle_widget(u->room, u->slot);
    hub_ui_refresh();
    bus_notify(u->room, u->slot);
}

static void step_slot_cb(lv_event_t *e)
{
    w_ud_t *u = (w_ud_t *)lv_event_get_user_data(e);
    hub_model_step_widget(u->room, u->slot, u->delta);
    hub_ui_refresh();
    bus_notify(u->room, u->slot);
}

static void set_level_cb(lv_event_t *e)
{
    w_ud_t *u = (w_ud_t *)lv_event_get_user_data(e);
    hub_model_set_widget_level(u->room, u->slot, u->delta);
    hub_ui_refresh();
    bus_notify(u->room, u->slot);
}

static void mb_persist_async(void); /* defined with Modbus editor */
static void style_card(lv_obj_t *obj, bool active); /* defined below */
static lv_obj_t *mk_lbl(lv_obj_t *par, const char *txt, lv_color_t c);
static void room_edit_fill_body(lv_obj_t *body);
static void room_edit_paint_head(void);
static lv_obj_t *room_edit_make_row(lv_obj_t *list, int room, int slot);
static int room_edit_row_index(lv_obj_t *row);

/**
 * Live edit-page refs — same idea as MB editor mb_paint():
 * mutate pool rows in place; never lv_obj_clean / create / del on add/remove.
 */
typedef struct {
    lv_obj_t *body;
    lv_obj_t *lbl_head;
    lv_obj_t *list;
    lv_obj_t *pool[HUB_WIDGET_MAX]; /**< pre-built rows — never create/del on add/remove */
    uint16_t gen;
} room_edit_ui_t;

static room_edit_ui_t s_edit_ui;
static uint16_t s_edit_gen;

static void room_mb_open_overlay(int slot);
static void room_edit_paint_row(lv_obj_t *row, int room, int slot);
static void room_edit_soft_paint_slot(int slot);
static void map_persist_later(void);
static void edit_overlay_commit(int slot, bool paint);
static hub_ico_t ico_for(hub_wtype_t t);
static void room_edit_ensure_slots(int room);
static bool room_slot_is_vacant(const hub_widget_t *w);
static void room_slot_mark_vacant(int room, int slot);
static void room_slot_assign_type(int room, int slot, hub_wtype_t type);
static void type_sheet_close(void);

/**
 * Fixed 12-slot model (no add/delete/clear):
 * - vacant = empty name → hidden on room page until Type assigns
 * - Type picker switches widget type in place (no PNG icons on edit rows)
 */
static bool room_slot_is_vacant(const hub_widget_t *w)
{
    return !w || (w->name[0] == '\0' && w->name_en[0] == '\0');
}

/** Canonical type names — never factory instance names (顶灯/夜灯/桌插). */
static const char *room_wtype_label(hub_wtype_t t)
{
    switch (t) {
    case HUB_W_ONOFF:
        return hub_tr("开关", "Switch");
    case HUB_W_DIMMER:
        return hub_tr("灯带", "Dimmer");
    case HUB_W_CURTAIN:
        return hub_tr("窗帘", "Curtain");
    case HUB_W_SHUTTER:
        return hub_tr("卷帘", "Shutter");
    case HUB_W_CLIM:
        return hub_tr("空调", "AC");
    case HUB_W_THERMO:
        return hub_tr("地暖", "Heat");
    case HUB_W_FAN:
        return hub_tr("风扇", "Fan");
    case HUB_W_PLUG:
        return hub_tr("插座", "Outlet");
    default:
        return hub_tr("开关", "Switch");
    }
}

static void room_slot_set_names(hub_widget_t *w, hub_wtype_t type)
{
    const char *lab = room_wtype_label(type);
    snprintf(w->name, sizeof(w->name), "%s", lab ? lab : "");
    snprintf(w->name_en, sizeof(w->name_en), "%s", lab ? lab : "");
}

static void room_slot_mark_vacant(int room, int slot)
{
    hub_widget_t *w = hub_model_widget_by_slot(room, slot);
    if (!w) {
        return;
    }
    w->enabled = false;
    w->on = false;
    w->level = 0;
    w->type = HUB_W_ONOFF;
    w->name[0] = '\0';
    w->name_en[0] = '\0';
    app_modbus_binding_t *b = app_modbus_map_get(room, slot);
    if (b) {
        b->enabled = false;
    }
    /* No hub_model_sync_legacy here — it walks all rooms and tears the edit list. */
}

static void room_slot_assign_type(int room, int slot, hub_wtype_t type)
{
    hub_widget_t *w = hub_model_widget_by_slot(room, slot);
    if (!w || type < 0 || type >= HUB_W_TYPE_COUNT) {
        return;
    }
    bool vacant = room_slot_is_vacant(w);
    w->type = type;
    room_slot_set_names(w, type);
    if (type == HUB_W_CLIM || type == HUB_W_THERMO) {
        w->level = 24;
    } else if (vacant) {
        w->level = 0;
        w->on = false;
    }
    w->enabled = true;
    if (vacant) {
        app_modbus_map_on_widget_added(room, slot, type);
    } else {
        app_modbus_map_ensure(room, slot, type);
    }
}

static void room_edit_ensure_slots(int room)
{
    hub_model_t *m = hub_model();
    if (!m || room < 0 || room >= HUB_ROOM_COUNT) {
        return;
    }
    while (m->widget_count[room] < HUB_WIDGET_MAX) {
        if (!hub_model_add_widget(room, HUB_W_ONOFF)) {
            break;
        }
        room_slot_mark_vacant(room, m->widget_count[room] - 1);
    }
}

static int room_edit_configured_count(int room)
{
    hub_model_t *m = hub_model();
    int n = 0;
    int max = m->widget_count[room];
    if (max > HUB_WIDGET_MAX) {
        max = HUB_WIDGET_MAX;
    }
    for (int i = 0; i < max; i++) {
        if (!room_slot_is_vacant(&m->widgets[room][i])) {
            n++;
        }
    }
    return n;
}

static void enable_slot_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_obj_get_parent(lv_event_get_target(e));
    int slot = room_edit_row_index(row);
    int room = hub_model()->room_idx;
    hub_widget_t *w = hub_model_widget_by_slot(room, slot);
    if (!w || !row || slot < 0) {
        return;
    }
    if (room_slot_is_vacant(w)) {
        hub_model_toast(hub_tr("先点「类型」配置槽位", "Pick Type first"));
        return;
    }
    hub_model_set_widget_enabled(room, slot, !w->enabled);
    room_edit_soft_paint_slot(slot);
}

static lv_obj_t *s_type_sheet;
static lv_obj_t *s_type_btns[HUB_W_TYPE_COUNT];
static int s_type_pick_slot = -1;
/* Declared early — edit_overlay_commit hides MB shell before its section. */
static lv_obj_t *s_mb_shell;
static bool s_mb_overlay;

/** Edit row children (no PNG): idx, title, sub, type, MB, vis */
enum {
    EDIT_ROW_IDX = 0,
    EDIT_ROW_TITLE,
    EDIT_ROW_SUB,
    EDIT_ROW_TYPE,
    EDIT_ROW_MB,
    EDIT_ROW_VIS,
};

static void type_sheet_close(void)
{
    /*
     * Park off-screen — HIDDEN alone still dirties a large region.
     * Off-screen + IGNORE_LAYOUT keeps the edit list frame stable.
     */
    if (s_type_sheet && lv_obj_is_valid(s_type_sheet)) {
        lv_obj_add_flag(s_type_sheet, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_pos(s_type_sheet, 0, -520);
    }
}

/**
 * Tear-free overlay reveal (same idea as hub_ui rebuild_route):
 * disable invalidation → hide overlays → optional text paint → one lv_refr_now.
 * Partial mid-frame updates on RGB are what users see as "内容撕裂".
 */
static void edit_overlay_commit(int slot, bool paint)
{
    lv_disp_t *disp = lv_disp_get_default();
    lv_obj_t *root = hub_ui_root();

    if (disp) {
        lv_disp_enable_invalidation(disp, false);
    }

    type_sheet_close();
    if (s_mb_shell && lv_obj_is_valid(s_mb_shell)) {
        lv_obj_add_flag(s_mb_shell, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_pos(s_mb_shell, 0, -520);
    }

    if (paint && slot >= 0) {
        room_edit_soft_paint_slot(slot);
        room_edit_paint_head();
    }

    if (disp) {
        lv_disp_enable_invalidation(disp, true);
        if (root && lv_obj_is_valid(root)) {
            lv_obj_invalidate(root);
        }
        lv_refr_now(disp);
    }
}

/** Text-only row paint — never touches images / hub_ico_rebind. */
static void room_edit_paint_row_lite(lv_obj_t *row, int room, int slot)
{
    if (!row || !lv_obj_is_valid(row)) {
        return;
    }
    hub_widget_t *w = hub_model_widget_by_slot(room, slot);
    if (!w) {
        return;
    }
    bool vacant = room_slot_is_vacant(w);
#if !CONFIG_HUB_UI_ENGINEER_EDIT
    if (vacant) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
        return;
    }
    lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
#endif
    const app_modbus_binding_t *mb = vacant ? NULL : app_modbus_map_get(room, slot);
    const hub_palette_t *p = hub_palette();

    lv_obj_set_style_border_color(row, (!vacant && w->enabled) ? p->accent : p->line, 0);

    lv_obj_t *title = lv_obj_get_child(row, EDIT_ROW_TITLE);
    if (title) {
        if (vacant) {
            lv_label_set_text(title, hub_tr("空闲", "Idle"));
            hub_style_label(title, p->t3, hub_font());
        } else {
            lv_label_set_text(title, room_wtype_label(w->type));
            hub_style_label(title, p->t1, hub_font());
        }
    }

    char sub[48];
    if (vacant) {
        snprintf(sub, sizeof(sub), "%s", hub_tr("代码未配置", "Not in seed"));
    } else if (mb && mb->enabled) {
        snprintf(sub, sizeof(sub), "S%d · 0x%04X", (int)mb->slave, (unsigned)mb->wr_addr);
    } else {
        snprintf(sub, sizeof(sub), "%s", hub_tr("未绑定 Modbus", "No Modbus"));
    }
    lv_obj_t *sl = lv_obj_get_child(row, EDIT_ROW_SUB);
    if (sl) {
        lv_label_set_text(sl, sub);
        hub_style_label(sl, p->t3, hub_font());
    }

    lv_obj_t *mb_btn = lv_obj_get_child(row, EDIT_ROW_MB);
    if (mb_btn) {
        lv_obj_set_style_border_color(mb_btn, (!vacant && mb && mb->enabled) ? p->accent : p->line, 0);
    }
    lv_obj_t *vis = lv_obj_get_child(row, EDIT_ROW_VIS);
    if (vis) {
        lv_obj_set_style_bg_color(vis, (!vacant && w->enabled) ? p->accent : p->bg_elev, 0);
        lv_obj_t *vl = lv_obj_get_child(vis, 0);
        if (vl) {
            lv_label_set_text(vl, (!vacant && w->enabled) ? hub_tr("显", "On") : hub_tr("隐", "Off"));
            hub_style_label(vl, (!vacant && w->enabled) ? p->ink_on : p->t1, hub_font());
        }
    }
}

static void room_edit_soft_paint_slot(int slot)
{
    if (s_edit_ui.gen != s_edit_gen || slot < 0 || slot >= HUB_WIDGET_MAX) {
        return;
    }
    lv_obj_t *row = s_edit_ui.pool[slot];
    if (!row || !lv_obj_is_valid(row)) {
        return;
    }
    room_edit_paint_row_lite(row, hub_model()->room_idx, slot);
}

static void type_pick_item_cb(lv_event_t *e)
{
    hub_wtype_t t = (hub_wtype_t)(uintptr_t)lv_event_get_user_data(e);
    int slot = s_type_pick_slot;
    int room = hub_model()->room_idx;
    if (slot < 0 || slot >= HUB_WIDGET_MAX) {
        type_sheet_close();
        return;
    }
    /* Mutate under cover, then one atomic reveal (no timer cascade). */
    room_slot_assign_type(room, slot, t);
    edit_overlay_commit(slot, true);
    map_persist_later();
}

static void type_sheet_scrim_cb(lv_event_t *e)
{
    if (lv_event_get_target(e) != s_type_sheet) {
        return;
    }
    edit_overlay_commit(-1, false);
}

static void type_picker_refresh_sel(int slot)
{
    const hub_palette_t *p = hub_palette();
    hub_widget_t *cur = hub_model_widget_by_slot(hub_model()->room_idx, slot);
    for (int t = 0; t < HUB_W_TYPE_COUNT; t++) {
        lv_obj_t *b = s_type_btns[t];
        if (!b || !lv_obj_is_valid(b)) {
            continue;
        }
        bool sel = cur && !room_slot_is_vacant(cur) && cur->type == (hub_wtype_t)t;
        lv_obj_set_style_bg_color(b, sel ? p->accent : p->bg_elev, 0);
        lv_obj_set_style_border_width(b, sel ? 0 : 1, 0);
        lv_obj_t *l = lv_obj_get_child(b, 0);
        if (l) {
            lv_label_set_text(l, room_wtype_label((hub_wtype_t)t));
            hub_style_label(l, sel ? p->ink_on : p->t1, hub_font());
        }
    }
}

/** Persistent type grid — show/hide, never recreate on each tap. */
static void open_type_picker(int slot)
{
    const hub_palette_t *p = hub_palette();
    lv_obj_t *root = hub_ui_root();
    if (!root || slot < 0) {
        return;
    }
    s_type_pick_slot = slot;

    if (!s_type_sheet || !lv_obj_is_valid(s_type_sheet)) {
        memset(s_type_btns, 0, sizeof(s_type_btns));
        s_type_sheet = lv_obj_create(root);
        lv_obj_remove_style_all(s_type_sheet);
        lv_obj_set_size(s_type_sheet, 480, 480);
        /* Opaque cover — avoids semi-transparent reveal tear of the edit list. */
        lv_obj_set_style_bg_color(s_type_sheet, p->bg_deep, 0);
        lv_obj_set_style_bg_opa(s_type_sheet, LV_OPA_COVER, 0);
        lv_obj_add_flag(s_type_sheet, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_FLOATING);
        lv_obj_clear_flag(s_type_sheet, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(s_type_sheet, type_sheet_scrim_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *panel = lv_obj_create(s_type_sheet);
        lv_obj_remove_style_all(panel);
        lv_obj_set_size(panel, 420, 300);
        lv_obj_set_style_radius(panel, p->radius ? p->radius : 12, 0);
        lv_obj_set_style_bg_color(panel, p->bg_card, 0);
        lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(panel, 12, 0);
        lv_obj_set_style_pad_row(panel, 8, 0);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);

        mk_lbl(panel, hub_tr("选择控件类型", "Choose widget type"), p->t1);

        lv_obj_t *grid = lv_obj_create(panel);
        lv_obj_remove_style_all(grid);
        lv_obj_set_width(grid, LV_PCT(100));
        lv_obj_set_flex_grow(grid, 1);
        lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_row(grid, 8, 0);
        lv_obj_set_style_pad_column(grid, 8, 0);
        lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE);

        for (int t = 0; t < HUB_W_TYPE_COUNT; t++) {
            lv_obj_t *b = lv_btn_create(grid);
            lv_obj_remove_style_all(b);
            lv_obj_set_size(b, 190, 48);
            lv_obj_set_style_radius(b, p->radius ? p->radius : 10, 0);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(b, p->line, 0);
            lv_obj_set_style_shadow_width(b, 0, 0);
            lv_obj_clear_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
            lv_obj_add_event_cb(b, type_pick_item_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)t);
            mk_lbl(b, room_wtype_label((hub_wtype_t)t), p->t1);
            lv_obj_center(lv_obj_get_child(b, 0));
            s_type_btns[t] = b;
        }
    }

    type_picker_refresh_sel(slot);
    lv_obj_set_style_bg_color(s_type_sheet, p->bg_deep, 0);
    lv_obj_set_style_bg_opa(s_type_sheet, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_type_sheet, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(s_type_sheet, 0, 0);
    lv_obj_move_foreground(s_type_sheet);
}

/** Open type picker (direct select — no cycle tear). */
static void type_cycle_cb(lv_event_t *e)
{
#if !CONFIG_HUB_UI_ENGINEER_EDIT
    (void)e;
    hub_model_toast(hub_tr("请在代码中改类型", "Edit type in code"));
    return;
#else
    lv_obj_t *row = lv_obj_get_parent(lv_event_get_target(e));
    int slot = room_edit_row_index(row);
    if (slot < 0) {
        return;
    }
    open_type_picker(slot);
#endif
}

static void go_mb_edit_cb(lv_event_t *e)
{
#if !CONFIG_HUB_UI_ENGINEER_EDIT
    (void)e;
    hub_model_toast(hub_tr("请在代码中改点表", "Edit map in code"));
    return;
#else
    lv_obj_t *row = lv_obj_get_parent(lv_event_get_target(e));
    int slot = room_edit_row_index(row);
    int room = hub_model()->room_idx;
    if (slot < 0) {
        return;
    }
    hub_widget_t *w = hub_model_widget_by_slot(room, slot);
    if (!w || room_slot_is_vacant(w)) {
        hub_model_toast(hub_tr("空槽 · 先选类型", "Empty · pick type"));
        return;
    }
    room_mb_open_overlay(slot);
#endif
}

static void room_page_nav_cb(lv_event_t *e)
{
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    hub_model_t *m = hub_model();
    int enabled = hub_model_enabled_widget_count(m->room_idx);
    int pages = enabled > 0 ? (enabled + PAGE_SIZE - 1) / PAGE_SIZE : 1;
    if (dir > 0) {
        if (m->room_subpage < pages - 1) {
            m->room_subpage++;
            hub_ui_refresh();
        }
    } else if (m->room_subpage > 0) {
        m->room_subpage--;
        hub_ui_refresh();
    }
}

static hub_ico_t ico_for(hub_wtype_t t)
{
    switch (t) {
    case HUB_W_ONOFF:
    case HUB_W_DIMMER:
        return HUB_ICO_BULB;
    case HUB_W_CURTAIN:
        return HUB_ICO_CURTAIN;
    case HUB_W_SHUTTER:
        return HUB_ICO_SHUTTER;
    case HUB_W_CLIM:
        return HUB_ICO_SNOW;
    case HUB_W_THERMO:
        return HUB_ICO_HEAT;
    case HUB_W_FAN:
        return HUB_ICO_FAN;
    case HUB_W_PLUG:
        return HUB_ICO_PLUG;
    default:
        return HUB_ICO_HOME;
    }
}

static bool widget_active(const hub_widget_t *w)
{
    if (!w) {
        return false;
    }
    if (w->type == HUB_W_CURTAIN || w->type == HUB_W_SHUTTER) {
        return w->level > 50;
    }
    if (w->type == HUB_W_DIMMER) {
        return w->on && w->level > 0;
    }
    return w->on;
}

static lv_obj_t *mk_lbl(lv_obj_t *par, const char *txt, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(par);
    lv_label_set_text(l, txt ? txt : "");
    hub_style_label(l, c, hub_font());
    return l;
}

static int slot_of(int room, const hub_widget_t *w)
{
    hub_model_t *m = hub_model();
    for (int i = 0; i < m->widget_count[room]; i++) {
        if (&m->widgets[room][i] == w) {
            return i;
        }
    }
    return -1;
}

typedef struct {
    lv_coord_t card_r;
    lv_coord_t badge_r;
    lv_coord_t pwr_r;
    lv_coord_t ctrl_r;
    bool accent_bar;
    bool zen_hairline;
} tw_chrome_t;

static void tw_chrome(tw_chrome_t *c)
{
    const hub_palette_t *p = hub_palette();
    const char *n = p->name ? p->name : "";
    memset(c, 0, sizeof(*c));
    c->card_r = p->radius;
    c->badge_r = 12;
    c->pwr_r = LV_RADIUS_CIRCLE;
    c->ctrl_r = 12;
    c->accent_bar = true;

    if (strcmp(n, "zen") == 0) {
        c->card_r = 0;
        c->badge_r = 0;
        c->pwr_r = 0;
        c->ctrl_r = 0;
        c->accent_bar = false;
        c->zen_hairline = true;
    } else if (strcmp(n, "bloom") == 0) {
        /* FE bloom: pill everything, no left accent bar */
        c->card_r = 28;
        c->badge_r = LV_RADIUS_CIRCLE;
        c->pwr_r = LV_RADIUS_CIRCLE;
        c->ctrl_r = LV_RADIUS_CIRCLE;
        c->accent_bar = false;
    } else if (strcmp(n, "metro") == 0) {
        c->card_r = 0;
        c->badge_r = 0;
        c->pwr_r = 0;
        c->ctrl_r = 0;
        c->accent_bar = false;
    } else if (strcmp(n, "ink") == 0) {
        c->card_r = 4;
        c->badge_r = 0;
        c->pwr_r = 0;
        c->ctrl_r = 0;
        c->accent_bar = true;
    } else if (strcmp(n, "pulse") == 0) {
        c->card_r = 0;
        c->badge_r = 0;
        c->pwr_r = 0;
        c->ctrl_r = 0;
        c->accent_bar = true;
    } else if (strcmp(n, "dusk") == 0) {
        c->badge_r = 14;
        c->pwr_r = 14;
        c->ctrl_r = 14;
    } else if (strcmp(n, "slate") == 0) {
        /* Left accent via theme_card_chrome border — skip bar */
        c->accent_bar = false;
        c->badge_r = 10;
        c->ctrl_r = 10;
    } else if (strcmp(n, "sand") == 0 || strcmp(n, "forest") == 0 || strcmp(n, "ocean") == 0) {
        c->badge_r = 12;
        c->ctrl_r = 12;
    }
}

static void style_card(lv_obj_t *obj, bool active)
{
    const hub_palette_t *p = hub_palette();
    const char *n = p->name ? p->name : "";
    tw_chrome_t ch;
    tw_chrome(&ch);

    hub_apply_card(obj, false);
    theme_card_chrome(obj, active);
    lv_obj_set_style_radius(obj, ch.card_r, 0);

    if (ch.zen_hairline) {
        lv_obj_set_style_radius(obj, 0, 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(obj, 0, 0);
        lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_border_color(obj, active ? p->t1 : p->line, 0);
        lv_obj_set_style_pad_ver(obj, 14, 0);
        lv_obj_set_style_pad_hor(obj, 4, 0);
        lv_obj_set_style_shadow_width(obj, 0, 0);
        return;
    }

    lv_obj_set_style_pad_all(obj, 10, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);

    if (strcmp(n, "bloom") == 0) {
        /* Soft frosted card — FE .w bloom */
        lv_obj_set_style_bg_color(obj, p->bg_card, 0);
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_border_color(obj, lv_color_make(255, 255, 255), 0);
        lv_obj_set_style_border_opa(obj, LV_OPA_60, 0);
        lv_obj_set_style_shadow_width(obj, 18, 0);
        lv_obj_set_style_shadow_opa(obj, LV_OPA_20, 0);
        lv_obj_set_style_shadow_ofs_y(obj, 6, 0);
        lv_obj_set_style_shadow_color(obj, lv_color_make(90, 60, 80), 0);
        if (active) {
            lv_obj_set_style_bg_color(obj, lv_color_mix(p->accent, p->bg_card, LV_OPA_30), 0);
            lv_obj_set_style_border_color(obj, lv_color_mix(p->accent, p->line, LV_OPA_50), 0);
            lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
        }
        return;
    }
    if (strcmp(n, "metro") == 0) {
        lv_obj_set_style_border_width(obj, 0, 0);
        lv_obj_set_style_shadow_width(obj, 0, 0);
        lv_obj_set_style_bg_color(obj, active ? lv_color_make(58, 58, 58)
                                              : lv_color_make(45, 45, 45), 0);
        return;
    }
    if (strcmp(n, "ink") == 0) {
        lv_obj_set_style_bg_color(obj, active ? lv_color_make(17, 17, 17) : p->bg_card, 0);
        lv_obj_set_style_border_width(obj, active ? 1 : 1, 0);
        lv_obj_set_style_border_color(obj, active ? p->accent : p->line, 0);
        lv_obj_set_style_shadow_width(obj, 0, 0);
        return;
    }
    if (strcmp(n, "pulse") == 0) {
        lv_obj_set_style_bg_color(obj, p->bg_card, 0);
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_border_color(obj, active ? p->accent : p->line, 0);
        lv_obj_set_style_shadow_width(obj, 0, 0);
        return;
    }
    if (strcmp(n, "dusk") == 0) {
        lv_obj_set_style_bg_color(obj, p->bg_card, 0);
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_border_color(obj, lv_color_make(220, 200, 255), 0);
        lv_obj_set_style_border_opa(obj, LV_OPA_30, 0);
        lv_obj_set_style_shadow_width(obj, 20, 0);
        lv_obj_set_style_shadow_opa(obj, LV_OPA_30, 0);
        lv_obj_set_style_shadow_ofs_y(obj, 8, 0);
        if (active) {
            lv_obj_set_style_border_color(obj, p->accent, 0);
            lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
        }
        return;
    }
    if (strcmp(n, "sand") == 0) {
        lv_obj_set_style_bg_color(obj, p->bg_card, 0);
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_border_color(obj, active ? p->accent : p->line, 0);
        lv_obj_set_style_shadow_width(obj, 14, 0);
        lv_obj_set_style_shadow_opa(obj, LV_OPA_20, 0);
        lv_obj_set_style_shadow_ofs_y(obj, 4, 0);
        lv_obj_set_style_shadow_color(obj, lv_color_make(60, 45, 30), 0);
        return;
    }
    if (strcmp(n, "forest") == 0) {
        lv_obj_set_style_bg_color(obj, active ? lv_color_mix(p->accent, p->bg_card, LV_OPA_20)
                                              : p->bg_card, 0);
        lv_obj_set_style_border_width(obj, active ? 1 : 1, 0);
        lv_obj_set_style_border_color(obj, active ? p->accent : p->line, 0);
        lv_obj_set_style_shadow_width(obj, 16, 0);
        lv_obj_set_style_shadow_opa(obj, LV_OPA_30, 0);
        return;
    }

    /* slate / ocean / default */
    lv_obj_set_style_bg_color(obj, active ? p->bg_card : p->bg_card, 0);
    if (active) {
        lv_obj_set_style_border_color(obj, p->accent, 0);
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_shadow_width(obj, 14, 0);
        lv_obj_set_style_shadow_opa(obj, LV_OPA_20, 0);
        lv_obj_set_style_shadow_ofs_y(obj, 4, 0);
        lv_obj_set_style_shadow_color(obj, p->t1, 0);
    }
}

/** FE .w::before — only themes that use a left accent rail */
static void add_accent_bar(lv_obj_t *cell, bool on)
{
    if (!on) {
        return;
    }
    tw_chrome_t ch;
    tw_chrome(&ch);
    if (!ch.accent_bar) {
        return;
    }
    const hub_palette_t *p = hub_palette();
    lv_obj_t *bar = lv_obj_create(cell);
    lv_obj_remove_style_all(bar);
    lv_coord_t w = (p->name && strcmp(p->name, "ink") == 0) ? 2 : 3;
    lv_obj_set_size(bar, w, LV_PCT(72));
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_bg_color(bar, p->accent, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, 0, 0);
}

static lv_obj_t *add_badge(lv_obj_t *cell, hub_ico_t ico, bool on, const room_tile_metrics_t *tm)
{
    const hub_palette_t *p = hub_palette();
    tw_chrome_t ch;
    tw_chrome(&ch);
    lv_obj_t *badge = lv_obj_create(cell);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, tm->badge, tm->badge);
    lv_obj_set_style_radius(badge, ch.badge_r, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(badge, on ? lv_color_mix(p->accent, p->bg_card, LV_OPA_20) : p->bg_card2, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    hub_ico_add(badge, ico, on ? p->accent : p->t3, tm->ico);
    lv_obj_center(lv_obj_get_child(badge, 0));
    return badge;
}

static lv_obj_t *add_pwr_btn(lv_obj_t *cell, bool on, int room, int slot, const room_tile_metrics_t *tm)
{
    const hub_palette_t *p = hub_palette();
    tw_chrome_t ch;
    tw_chrome(&ch);
    lv_obj_t *btn = lv_btn_create(cell);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, tm->pwr, tm->pwr);
    lv_obj_set_style_radius(btn, ch.pwr_r, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    if (on) {
        lv_obj_set_style_bg_color(btn, p->accent, 0);
    } else {
        lv_obj_set_style_bg_color(btn, p->bg_card2, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, p->line, 0);
    }
    lv_obj_add_event_cb(btn, toggle_slot_cb, LV_EVENT_CLICKED, ud_alloc(room, slot, 0));
    hub_ico_add(btn, HUB_ICO_POWER, on ? p->ink_on : p->t2, tm->pwr_ico);
    lv_obj_center(lv_obj_get_child(btn, 0));
    return btn;
}

static lv_obj_t *mk_flex_row(lv_obj_t *par)
{
    lv_obj_t *row = lv_obj_create(par);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return row;
}

static lv_obj_t *mk_title_lbl(lv_obj_t *par, const char *txt, lv_color_t c)
{
    lv_obj_t *l = mk_lbl(par, txt, c);
    lv_obj_set_style_text_font(l, hub_font(), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    /* width 0 + grow: works in both column (fill) and row (share leftover). */
    lv_obj_set_width(l, 0);
    lv_obj_set_flex_grow(l, 1);
    return l;
}

/** Flex grow spacer — mirrors Smart-LVGL `lv_nt_slot_apply_spacer`. */
static lv_obj_t *mk_spacer(lv_obj_t *par, bool grow)
{
    lv_obj_t *sp = lv_obj_create(par);
    lv_obj_remove_style_all(sp);
    lv_obj_set_width(sp, LV_PCT(100));
    lv_obj_clear_flag(sp, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    if (grow) {
        lv_obj_set_height(sp, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(sp, 1);
    } else {
        lv_obj_set_height(sp, 0);
        lv_obj_set_flex_grow(sp, 0);
        lv_obj_add_flag(sp, LV_OBJ_FLAG_HIDDEN);
    }
    return sp;
}

/** Prep room tile: no shadow (clips on 480), clip corners; column + spacer model. */
static void cell_prep(lv_obj_t *cell, bool on, const room_tile_metrics_t *tm)
{
    style_card(cell, on);
    lv_obj_set_style_shadow_width(cell, 0, 0);
    lv_obj_set_style_shadow_opa(cell, LV_OPA_TRANSP, 0);
    lv_obj_set_style_clip_corner(cell, true, 0);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    /* SINGLE packs from top (spacer fills). Others SPACE_BETWEEN like Smart-LVGL. */
    lv_obj_set_flex_align(cell,
                          tm->use_spacer ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(cell, tm->pad, 0);
    lv_obj_set_style_pad_left(cell, tm->pad + 4, 0); /* accent bar clearance */
    lv_obj_set_style_pad_row(cell, tm->gap, 0);
    add_accent_bar(cell, on);
}

static void style_ctrl_btn(lv_obj_t *b, bool active, bool zen)
{
    const hub_palette_t *p = hub_palette();
    tw_chrome_t ch;
    tw_chrome(&ch);
    style_card(b, active);
    if (zen) {
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        if (active) {
            lv_obj_set_style_bg_color(b, p->t1, 0);
            lv_obj_set_style_border_color(b, p->t1, 0);
        } else {
            lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(b, 1, 0);
            lv_obj_set_style_border_side(b, LV_BORDER_SIDE_FULL, 0);
            lv_obj_set_style_border_color(b, p->line, 0);
        }
    } else if (p->name && strcmp(p->name, "metro") == 0) {
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, active ? lv_color_make(0, 188, 242)
                                            : lv_color_make(58, 58, 58), 0);
    }
    lv_obj_set_style_radius(b, ch.ctrl_r, 0);
}

static void slider_cb(lv_event_t *e)
{
    w_ud_t *u = (w_ud_t *)lv_event_get_user_data(e);
    lv_obj_t *sl = lv_event_get_target(e);
    int val = (int)lv_slider_get_value(sl);
    hub_model_set_widget_level(u->room, u->slot, val);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        hub_ui_refresh();
        bus_notify(u->room, u->slot); /* write once on release, not every drag tick */
    }
}

static void style_dimmer_slider(lv_obj_t *sl, bool zen)
{
    const hub_palette_t *p = hub_palette();
    lv_obj_set_style_shadow_width(sl, 0, LV_PART_KNOB);
    if (zen) {
        lv_obj_set_style_radius(sl, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(sl, 0, LV_PART_INDICATOR);
        lv_obj_set_style_radius(sl, 0, LV_PART_KNOB);
        lv_obj_set_style_bg_color(sl, lv_color_mix(p->t1, p->bg_deep, LV_OPA_10), LV_PART_MAIN);
        lv_obj_set_style_bg_color(sl, p->t1, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(sl, p->t1, LV_PART_KNOB);
        lv_obj_set_style_pad_ver(sl, 3, LV_PART_KNOB);
        lv_obj_set_style_width(sl, 3, LV_PART_KNOB);
    } else {
        lv_obj_set_style_bg_color(sl, p->bg_elev, LV_PART_MAIN);
        lv_obj_set_style_bg_color(sl, p->accent, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(sl, p->bg_card, LV_PART_KNOB);
        lv_obj_set_style_pad_all(sl, 3, LV_PART_KNOB);
    }
}

/**
 * Paint one room tile — shared shell for all themes (hub_room_ui, not per-theme):
 *   Switch/Fan/Plug: top(badge|pwr) → spacer? → name|ON/OFF   ← same as Dimmer head
 *   Dimmer:          top(badge|pwr) → spacer? → name|pct → slider
 *   Curtain/Shutter: top(badge|name|%) → spacer? → 3 ctrl btns
 *   Clim/Thermo:     top(badge|title|pwr) → spacer? → temp ±
 */
static void paint_widget(lv_obj_t *cell, hub_widget_t *w, int room, room_tile_t tile)
{
    const hub_palette_t *p = hub_palette();
    int slot = slot_of(room, w);
    if (slot < 0) {
        return;
    }
    room_tile_metrics_t tm;
    tile_metrics(tile, &tm);
    bool on = widget_active(w);
    bool zen = p->name && strcmp(p->name, "zen") == 0;
    /* Only SINGLE uses a grow spacer; HALF/TALL use SPACE_BETWEEN (see cell_prep). */
    bool spacer_grow = tm.use_spacer;
    bool half = (tile == ROOM_TILE_HALF);
    cell_prep(cell, on, &tm);

    const char *wname = room_wtype_label(w->type);
    char val[24];

    if (w->type == HUB_W_ONOFF || w->type == HUB_W_FAN || w->type == HUB_W_PLUG) {
        /* Align with Dimmer: badge+pwr on top row, title+value on second row. */
        lv_obj_t *top = mk_flex_row(cell);
        lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        add_badge(top, ico_for(w->type), on, &tm);
        add_pwr_btn(top, w->on, room, slot, &tm);
        mk_spacer(cell, spacer_grow);

        lv_obj_t *title_row = mk_flex_row(cell);
        lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        mk_title_lbl(title_row, wname, p->t1);
        snprintf(val, sizeof(val), "%s", w->on ? "ON" : "OFF");
        mk_lbl(title_row, val, w->on ? p->accent : p->t3);

        lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(cell, toggle_slot_cb, LV_EVENT_CLICKED, ud_alloc(room, slot, 0));
    } else if (w->type == HUB_W_DIMMER) {
        /* top [badge|pwr] → (spacer) → [name|pct] → slider */
        lv_obj_t *top = mk_flex_row(cell);
        lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        add_badge(top, HUB_ICO_BULB, on, &tm);
        add_pwr_btn(top, w->on, room, slot, &tm);
        mk_spacer(cell, spacer_grow);

        lv_obj_t *title_row = mk_flex_row(cell);
        lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        mk_title_lbl(title_row, wname, p->t1);
        snprintf(val, sizeof(val), "%d%%", w->on ? w->level : 0);
        mk_lbl(title_row, val, on ? p->accent : p->t4);

        lv_obj_t *sl = lv_slider_create(cell);
        lv_obj_set_width(sl, LV_PCT(100));
        lv_obj_set_height(sl, tm.slider_h);
        lv_slider_set_range(sl, 0, 100);
        lv_slider_set_value(sl, w->on ? w->level : 0, LV_ANIM_OFF);
        style_dimmer_slider(sl, zen);
        lv_obj_clear_flag(sl, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(sl, slider_cb, LV_EVENT_VALUE_CHANGED, ud_alloc(room, slot, 0));
        lv_obj_add_event_cb(sl, slider_cb, LV_EVENT_RELEASED, ud_alloc(room, slot, 0));
        if (!w->on) {
            lv_obj_set_style_opa(sl, LV_OPA_40, 0);
        }
    } else if (w->type == HUB_W_CURTAIN || w->type == HUB_W_SHUTTER) {
        bool is_curtain = (w->type == HUB_W_CURTAIN);
        /* HALF: [badge|name|%] then 3 btns; taller: same + optional spacer */
        lv_obj_t *hdr = mk_flex_row(cell);
        add_badge(hdr, ico_for(w->type), on, &tm);
        mk_title_lbl(hdr, wname, p->t1);
        snprintf(val, sizeof(val), "%d%%", w->level);
        mk_lbl(hdr, val, p->t3);
        mk_spacer(cell, spacer_grow);

        lv_obj_t *btns = lv_obj_create(cell);
        lv_obj_remove_style_all(btns);
        lv_obj_set_width(btns, LV_PCT(100));
        lv_obj_set_height(btns, tm.ctrl_h);
        lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btns, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(btns, half ? 4 : 8, 0);
        lv_obj_clear_flag(btns, LV_OBJ_FLAG_SCROLLABLE);

        const int levels[] = { 100, 50, 0 };
        hub_ico_t icos[3];
        if (is_curtain) {
            icos[0] = HUB_ICO_LEFT;
            icos[1] = HUB_ICO_STOP;
            icos[2] = HUB_ICO_RIGHT;
        } else {
            icos[0] = HUB_ICO_UP;
            icos[1] = HUB_ICO_STOP;
            icos[2] = HUB_ICO_DOWN;
        }
        for (int i = 0; i < 3; i++) {
            lv_obj_t *b = lv_btn_create(btns);
            lv_obj_remove_style_all(b);
            style_ctrl_btn(b, w->level == levels[i], zen);
            lv_obj_set_style_shadow_width(b, 0, 0);
            lv_obj_set_flex_grow(b, 1);
            lv_obj_set_height(b, LV_PCT(100));
            lv_obj_add_event_cb(b, set_level_cb, LV_EVENT_CLICKED, ud_alloc(room, slot, levels[i]));
            hub_ico_add(b, icos[i],
                        (zen && w->level == levels[i]) ? p->ink_on
                        : (w->level == levels[i] ? p->accent : p->t2),
                        tm.ctrl_ico);
            lv_obj_center(lv_obj_get_child(b, 0));
        }
    } else if (w->type == HUB_W_CLIM || w->type == HUB_W_THERMO) {
        lv_color_t hue = w->type == HUB_W_CLIM ? p->cool : p->heat;
        /* [badge|title|pwr] → (spacer) → [temp | - | +] */
        lv_obj_t *hdr = mk_flex_row(cell);
        add_badge(hdr, ico_for(w->type), on, &tm);
        mk_title_lbl(hdr, wname, p->t1);
        add_pwr_btn(hdr, w->on, room, slot, &tm);
        mk_spacer(cell, spacer_grow);

        lv_obj_t *foot = mk_flex_row(cell);
        lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        snprintf(val, sizeof(val), w->on ? "%d°" : "--", w->level);
        lv_obj_t *deg = lv_label_create(foot);
        lv_label_set_text(deg, val);
        /* HALF: never use clock font — overflows short cells */
        hub_style_label(deg, on ? hue : p->t4,
                        (tm.big_value && !half) ? hub_font_clock() : hub_font());
        lv_obj_set_flex_grow(deg, 1);

        lv_obj_t *am = lv_btn_create(foot);
        lv_obj_remove_style_all(am);
        style_ctrl_btn(am, false, zen);
        lv_obj_set_style_shadow_width(am, 0, 0);
        lv_obj_set_size(am, tm.step, tm.step);
        lv_obj_add_event_cb(am, step_slot_cb, LV_EVENT_CLICKED, ud_alloc(room, slot, -1));
        hub_ico_add(am, HUB_ICO_MINUS, p->t1, tm.ctrl_ico);
        lv_obj_center(lv_obj_get_child(am, 0));

        lv_obj_t *ap = lv_btn_create(foot);
        lv_obj_remove_style_all(ap);
        style_ctrl_btn(ap, false, zen);
        lv_obj_set_style_shadow_width(ap, 0, 0);
        lv_obj_set_size(ap, tm.step, tm.step);
        lv_obj_add_event_cb(ap, step_slot_cb, LV_EVENT_CLICKED, ud_alloc(room, slot, 1));
        hub_ico_add(ap, HUB_ICO_PLUS, p->t1, tm.ctrl_ico);
        lv_obj_center(lv_obj_get_child(ap, 0));
    }
}

static void layout_grid(lv_obj_t *body, hub_widget_t **ws, int n, int room)
{
    if (n <= 0) {
        return;
    }
    lv_obj_t *grid = lv_obj_create(body);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_row(grid, ROOM_GRID_GAP_ROW, 0);
    lv_obj_set_style_pad_column(grid, ROOM_GRID_GAP_COL, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    static lv_coord_t c1[] = { LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t c2[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t r1[] = { LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t r2[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t r3[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };

    if (n == 1) {
        lv_obj_set_grid_dsc_array(grid, c1, r1);
        lv_obj_t *cell = lv_obj_create(grid);
        lv_obj_remove_style_all(cell);
        lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
        paint_widget(cell, ws[0], room, tile_for_slot(1, 0));
    } else if (n == 2) {
        lv_obj_set_grid_dsc_array(grid, c1, r2);
        for (int i = 0; i < 2; i++) {
            lv_obj_t *cell = lv_obj_create(grid);
            lv_obj_remove_style_all(cell);
            lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, i, 1);
            paint_widget(cell, ws[i], room, tile_for_slot(2, i));
        }
    } else if (n == 3) {
        lv_obj_set_grid_dsc_array(grid, c1, r3);
        for (int i = 0; i < 3; i++) {
            lv_obj_t *cell = lv_obj_create(grid);
            lv_obj_remove_style_all(cell);
            lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, i, 1);
            paint_widget(cell, ws[i], room, tile_for_slot(3, i));
        }
    } else if (n == 4) {
        lv_obj_set_grid_dsc_array(grid, c2, r2);
        for (int i = 0; i < 4; i++) {
            lv_obj_t *cell = lv_obj_create(grid);
            lv_obj_remove_style_all(cell);
            lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, i % 2, 1, LV_GRID_ALIGN_STRETCH, i / 2, 1);
            paint_widget(cell, ws[i], room, tile_for_slot(4, i));
        }
    } else if (n == 5) {
        /* Smart Five: top WIDE span + 2×2 HALF */
        lv_obj_set_grid_dsc_array(grid, c2, r3);
        lv_obj_t *top = lv_obj_create(grid);
        lv_obj_remove_style_all(top);
        lv_obj_set_grid_cell(top, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1);
        paint_widget(top, ws[0], room, tile_for_slot(5, 0));
        for (int i = 1; i < 5; i++) {
            lv_obj_t *cell = lv_obj_create(grid);
            lv_obj_remove_style_all(cell);
            int col = (i - 1) % 2;
            int row = 1 + (i - 1) / 2;
            lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
            paint_widget(cell, ws[i], room, tile_for_slot(5, i));
        }
    } else {
        lv_obj_set_grid_dsc_array(grid, c2, r3);
        for (int i = 0; i < 6; i++) {
            lv_obj_t *cell = lv_obj_create(grid);
            lv_obj_remove_style_all(cell);
            lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, i % 2, 1, LV_GRID_ALIGN_STRETCH, i / 2, 1);
            paint_widget(cell, ws[i], room, tile_for_slot(6, i));
        }
    }
}

void hub_toast_present(lv_obj_t *parent)
{
    char msg[HUB_TOAST_MAX];
    if (!hub_model_take_toast(msg, sizeof(msg))) {
        return;
    }
    const hub_palette_t *p = hub_palette();
    lv_obj_t *toast = lv_obj_create(parent);
    lv_obj_remove_style_all(toast);
    lv_obj_set_size(toast, 280, 40);
    lv_obj_set_style_radius(toast, (p->name && strcmp(p->name, "zen") == 0) ? 0 : 10, 0);
    lv_obj_set_style_bg_color(toast, p->bg_elev, 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(toast, 1, 0);
    lv_obj_set_style_border_color(toast, p->accent, 0);
    lv_obj_add_flag(toast, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_t *l = lv_label_create(toast);
    lv_label_set_text(l, msg);
    hub_style_label(l, p->t1, hub_font());
    lv_obj_center(l);
    lv_obj_fade_out(toast, 400, 1600);
    lv_obj_del_delayed(toast, 2200);
}

void hub_build_room_page(lv_obj_t *parent)
{
    s_ud_n = 0;
    const hub_palette_t *p = hub_palette();
    hub_model_t *m = hub_model();
    int room = m->room_idx;
    int enabled = hub_model_enabled_widget_count(room);
    int pages = enabled > 0 ? (enabled + PAGE_SIZE - 1) / PAGE_SIZE : 1;
    if (m->room_subpage >= pages) {
        m->room_subpage = pages - 1;
    }
    if (m->room_subpage < 0) {
        m->room_subpage = 0;
    }
    bool multi = pages > 1;

    char title[40];
    if (multi) {
        snprintf(title, sizeof(title), "%s  %d/%d", hub_model_room_name(room),
                 m->room_subpage + 1, pages);
    } else {
        snprintf(title, sizeof(title), "%s", hub_model_room_name(room));
    }
    lv_obj_t *shell = hub_shell_create(parent, title, true);
    lv_obj_t *body = hub_shell_body(shell);
    hub_shell_set_dots(shell, multi ? pages : 4, multi ? m->room_subpage : 1);
    /* Reclaim ~8–10px for the widget grid (less bottom chrome). */
    lv_obj_set_style_pad_bottom(body, 6, 0);
    {
        lv_obj_t *dots = (lv_obj_t *)lv_obj_get_user_data(body);
        if (dots) {
            lv_obj_set_height(dots, 16); /* default shell dots = 24 */
        }
    }

    /* room tabs + edit */
    bool zen = p->name && strcmp(p->name, "zen") == 0;
    lv_obj_t *tabs = lv_obj_create(body);
    lv_obj_remove_style_all(tabs);
    lv_obj_set_width(tabs, LV_PCT(100));
    lv_obj_set_height(tabs, 36);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tabs, 4, 0);
    for (int i = 0; i < HUB_ROOM_COUNT; i++) {
        lv_obj_t *t = lv_btn_create(tabs);
        lv_obj_remove_style_all(t);
        if (zen) {
            lv_obj_set_style_radius(t, 0, 0);
            lv_obj_set_style_border_width(t, 1, 0);
            lv_obj_set_style_border_color(t, p->line, 0);
            lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(t, i == room ? p->accent : p->bg_card, 0);
        } else if (p->name && strcmp(p->name, "bloom") == 0) {
            lv_obj_set_style_radius(t, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(t, i == room ? p->accent : p->bg_card, 0);
            lv_obj_set_style_border_width(t, i == room ? 0 : 1, 0);
            lv_obj_set_style_border_color(t, p->line, 0);
        } else {
            style_card(t, i == room);
            if (i == room) {
                lv_obj_set_style_bg_color(t, p->accent, 0);
            }
        }
        lv_obj_set_flex_grow(t, 1);
        lv_obj_set_height(t, LV_PCT(100));
        lv_obj_add_event_cb(t, go_room_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *tl = lv_label_create(t);
        lv_label_set_text(tl, hub_model_room_name(i));
        hub_style_label(tl, i == room ? p->ink_on : p->t2, hub_font());
        lv_obj_center(tl);
    }
    lv_obj_t *ed = lv_btn_create(tabs);
    lv_obj_remove_style_all(ed);
    if (zen) {
        lv_obj_set_style_radius(ed, 0, 0);
        lv_obj_set_style_border_width(ed, 1, 0);
        lv_obj_set_style_border_color(ed, p->line, 0);
        lv_obj_set_style_bg_opa(ed, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(ed, p->bg_card, 0);
    } else {
        style_card(ed, false);
    }
    lv_obj_set_width(ed, 56);
    lv_obj_set_height(ed, LV_PCT(100));
    lv_obj_add_event_cb(ed, go_edit_cb, LV_EVENT_CLICKED, NULL);
    mk_lbl(ed, hub_tr("编辑", "Edit"), p->accent);
    lv_obj_center(lv_obj_get_child(ed, 0));

    if (enabled == 0) {
        mk_lbl(body, hub_tr("暂无控件", "No widgets"), p->t3);
        lv_obj_t *add = lv_btn_create(body);
        lv_obj_remove_style_all(add);
        style_card(add, true);
        lv_obj_set_style_bg_color(add, p->accent, 0);
        lv_obj_set_size(add, 180, 44);
        lv_obj_add_event_cb(add, go_edit_cb, LV_EVENT_CLICKED, NULL);
        mk_lbl(add, hub_tr("添加家居控件", "Add widgets"), p->ink_on);
        lv_obj_center(lv_obj_get_child(add, 0));
        return;
    }

    /* collect page widgets, clim/thermo first when n==5 */
    hub_widget_t *page_ws[PAGE_SIZE];
    int page_n = 0;
    int start = m->room_subpage * PAGE_SIZE;
    int seen = 0;
    for (int i = 0; i < m->widget_count[room] && page_n < PAGE_SIZE; i++) {
        hub_widget_t *cw = &m->widgets[room][i];
        /* Room page: only non-vacant + shown slots (fixed-12 model). */
        if (room_slot_is_vacant(cw) || !cw->enabled) {
            continue;
        }
        if (seen++ < start) {
            continue;
        }
        page_ws[page_n++] = cw;
    }
    if (page_n == 5) {
        hub_widget_t *ordered[5];
        int o = 0;
        for (int i = 0; i < 5; i++) {
            if (page_ws[i]->type == HUB_W_CLIM || page_ws[i]->type == HUB_W_THERMO) {
                ordered[o++] = page_ws[i];
            }
        }
        for (int i = 0; i < 5; i++) {
            if (page_ws[i]->type != HUB_W_CLIM && page_ws[i]->type != HUB_W_THERMO) {
                ordered[o++] = page_ws[i];
            }
        }
        memcpy(page_ws, ordered, sizeof(ordered));
    }

    layout_grid(body, page_ws, page_n, room);

    /* Floating ◀ ▶ mid-left / mid-right — no layout squeeze on widgets */
    if (multi) {
        lv_obj_t *prev = lv_btn_create(shell);
        lv_obj_remove_style_all(prev);
        style_card(prev, m->room_subpage > 0);
        lv_obj_set_size(prev, 40, 72);
        lv_obj_set_style_bg_opa(prev, LV_OPA_80, 0);
        lv_obj_add_flag(prev, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_align(prev, LV_ALIGN_LEFT_MID, 4, 8);
        lv_obj_move_foreground(prev);
        if (m->room_subpage <= 0) {
            lv_obj_add_state(prev, LV_STATE_DISABLED);
            lv_obj_set_style_opa(prev, LV_OPA_30, 0);
        }
        lv_obj_add_event_cb(prev, room_page_nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
        {
            lv_obj_t *pl = lv_label_create(prev);
            lv_label_set_text(pl, LV_SYMBOL_LEFT);
            hub_style_label(pl, p->t1, hub_font_symbol());
            lv_obj_center(pl);
        }

        lv_obj_t *next = lv_btn_create(shell);
        lv_obj_remove_style_all(next);
        style_card(next, m->room_subpage < pages - 1);
        lv_obj_set_size(next, 40, 72);
        lv_obj_set_style_bg_opa(next, LV_OPA_80, 0);
        lv_obj_add_flag(next, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_align(next, LV_ALIGN_RIGHT_MID, -4, 8);
        lv_obj_move_foreground(next);
        if (m->room_subpage >= pages - 1) {
            lv_obj_add_state(next, LV_STATE_DISABLED);
            lv_obj_set_style_opa(next, LV_OPA_30, 0);
        }
        lv_obj_add_event_cb(next, room_page_nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
        {
            lv_obj_t *nl = lv_label_create(next);
            lv_label_set_text(nl, LV_SYMBOL_RIGHT);
            hub_style_label(nl, p->t1, hub_font_symbol());
            lv_obj_center(nl);
        }
    }
    (void)subpage_cb;
}

static void room_edit_shell_del_cb(lv_event_t *e)
{
    (void)e;
    memset(&s_edit_ui, 0, sizeof(s_edit_ui));
}

static int room_edit_row_index(lv_obj_t *row)
{
    if (!row || s_edit_ui.gen != s_edit_gen) {
        return -1;
    }
    for (int i = 0; i < HUB_WIDGET_MAX; i++) {
        if (s_edit_ui.pool[i] == row) {
            return i;
        }
    }
    return -1;
}

static void room_edit_paint_head(void)
{
    if (s_edit_ui.gen != s_edit_gen || !s_edit_ui.lbl_head || !lv_obj_is_valid(s_edit_ui.lbl_head)) {
        return;
    }
    int room = hub_model()->room_idx;
    char head[56];
    snprintf(head, sizeof(head), hub_tr("%d 显示 · %d 已配 · %d 槽", "%d shown · %d set · %d slots"),
             hub_model_enabled_widget_count(room), room_edit_configured_count(room), HUB_WIDGET_MAX);
    lv_label_set_text(s_edit_ui.lbl_head, head);
}

/**
 * Paint one fixed slot row — labels / styles only (never create/del, never PNG).
 * Child order: idx, title, sub, type, MB, vis.
 */
static void room_edit_paint_row(lv_obj_t *row, int room, int slot)
{
    if (!row || !lv_obj_is_valid(row)) {
        return;
    }
    hub_widget_t *w = hub_model_widget_by_slot(room, slot);
    if (!w) {
        return;
    }
    bool vacant = room_slot_is_vacant(w);
#if !CONFIG_HUB_UI_ENGINEER_EDIT
    /* Customer UI: only show factory-configured slots (hide empty placeholders). */
    if (vacant) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
        return;
    }
    lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
#endif
    if (!vacant) {
        app_modbus_map_ensure(room, slot, w->type);
    }
    const app_modbus_binding_t *mb = app_modbus_map_get(room, slot);
    const hub_palette_t *p = hub_palette();

    /* Flat chrome — style_card shadows cause large dirty regions / tear. */
    lv_obj_set_style_radius(row, p->radius ? p->radius : 10, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, p->bg_card, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, (!vacant && w->enabled) ? p->accent : p->line, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_opa(row, LV_OPA_COVER, 0);

    lv_obj_t *idx = lv_obj_get_child(row, EDIT_ROW_IDX);
    if (idx) {
        char ibuf[8];
        snprintf(ibuf, sizeof(ibuf), "%02d", slot + 1);
        lv_label_set_text(idx, ibuf);
        hub_style_label(idx, p->t3, hub_font());
    }

    lv_obj_t *title = lv_obj_get_child(row, EDIT_ROW_TITLE);
    if (title) {
        if (vacant) {
            lv_label_set_text(title, hub_tr("空闲", "Idle"));
        } else {
            lv_label_set_text(title, room_wtype_label(w->type));
        }
        hub_style_label(title, vacant ? p->t3 : p->t1, hub_font());
    }

    char sub[48];
    if (vacant) {
        snprintf(sub, sizeof(sub), "%s", hub_tr("代码未配置", "Not in seed"));
    } else if (mb && mb->enabled) {
        snprintf(sub, sizeof(sub), "S%d · 0x%04X", (int)mb->slave, (unsigned)mb->wr_addr);
    } else {
        snprintf(sub, sizeof(sub), "%s", hub_tr("未绑定 Modbus", "No Modbus"));
    }
    lv_obj_t *sl = lv_obj_get_child(row, EDIT_ROW_SUB);
    if (sl) {
        lv_label_set_text(sl, sub);
        hub_style_label(sl, p->t3, hub_font());
    }

    lv_obj_t *type_btn = lv_obj_get_child(row, EDIT_ROW_TYPE);
    lv_obj_t *mb_btn = lv_obj_get_child(row, EDIT_ROW_MB);
#if CONFIG_HUB_UI_ENGINEER_EDIT
    if (type_btn) {
        lv_obj_clear_flag(type_btn, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_style_bg_color(type_btn, p->bg_elev, 0);
        lv_obj_set_style_bg_opa(type_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(type_btn, 1, 0);
        lv_obj_set_style_border_color(type_btn, p->line, 0);
        lv_obj_set_style_shadow_width(type_btn, 0, 0);
        lv_obj_t *tl = lv_obj_get_child(type_btn, 0);
        if (tl) {
            lv_label_set_text(tl, hub_tr("类型", "Type"));
            hub_style_label(tl, p->accent, hub_font());
        }
    }
    if (mb_btn) {
        lv_obj_clear_flag(mb_btn, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_style_bg_color(mb_btn, p->bg_elev, 0);
        lv_obj_set_style_bg_opa(mb_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(mb_btn, 1, 0);
        lv_obj_set_style_border_color(mb_btn, (!vacant && mb && mb->enabled) ? p->accent : p->line, 0);
        lv_obj_set_style_shadow_width(mb_btn, 0, 0);
        lv_obj_set_style_opa(mb_btn, LV_OPA_COVER, 0);
    }
#else
    if (type_btn) {
        lv_obj_add_flag(type_btn, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
    }
    if (mb_btn) {
        lv_obj_add_flag(mb_btn, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
    }
#endif

    lv_obj_t *vis = lv_obj_get_child(row, EDIT_ROW_VIS);
    if (vis) {
        lv_obj_set_style_bg_color(vis, (!vacant && w->enabled) ? p->accent : p->bg_elev, 0);
        lv_obj_set_style_bg_opa(vis, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(vis, 1, 0);
        lv_obj_set_style_border_color(vis, p->line, 0);
        lv_obj_set_style_shadow_width(vis, 0, 0);
        lv_obj_set_style_opa(vis, LV_OPA_COVER, 0);
        lv_obj_t *vl = lv_obj_get_child(vis, 0);
        if (vl) {
            lv_label_set_text(vl, (!vacant && w->enabled) ? hub_tr("显", "On") : hub_tr("隐", "Off"));
            hub_style_label(vl, (!vacant && w->enabled) ? p->ink_on : p->t1, hub_font());
        }
    }
}

/**
 * Allocate one fixed slot row (events wired once). Never destroyed on edit.
 * Children: idx(label), title, sub, type, MB, vis — no PNG, no clear.
 */
static lv_obj_t *room_edit_make_row(lv_obj_t *list, int room, int slot)
{
    (void)room;
    const hub_palette_t *p = hub_palette();
    if (!list) {
        return NULL;
    }

    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    style_card(row, true);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 58);
    lv_obj_set_style_opa(row, LV_OPA_COVER, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    char ibuf[8];
    snprintf(ibuf, sizeof(ibuf), "%02d", slot + 1);
    mk_lbl(row, ibuf, p->t3);
    lv_obj_align(lv_obj_get_child(row, EDIT_ROW_IDX), LV_ALIGN_LEFT_MID, 10, 0);

    mk_lbl(row, "", p->t1);
    lv_obj_align(lv_obj_get_child(row, EDIT_ROW_TITLE), LV_ALIGN_LEFT_MID, 40, -10);
    mk_lbl(row, "", p->t3);
    lv_obj_align(lv_obj_get_child(row, EDIT_ROW_SUB), LV_ALIGN_LEFT_MID, 40, 10);

    /* Child order fixed: type / MB / vis — engineer-only buttons stay hidden off. */
    lv_obj_t *type_btn = lv_btn_create(row);
    lv_obj_remove_style_all(type_btn);
    style_card(type_btn, false);
    lv_obj_set_size(type_btn, 52, 34);
    lv_obj_align(type_btn, LV_ALIGN_RIGHT_MID, -100, 0);
    lv_obj_add_flag(type_btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(type_btn, type_cycle_cb, LV_EVENT_CLICKED, NULL);
    mk_lbl(type_btn, hub_tr("类型", "Type"), p->accent);
    lv_obj_center(lv_obj_get_child(type_btn, 0));

    lv_obj_t *mb_btn = lv_btn_create(row);
    lv_obj_remove_style_all(mb_btn);
    style_card(mb_btn, false);
    lv_obj_set_size(mb_btn, 42, 34);
    lv_obj_align(mb_btn, LV_ALIGN_RIGHT_MID, -52, 0);
    lv_obj_add_flag(mb_btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(mb_btn, go_mb_edit_cb, LV_EVENT_CLICKED, NULL);
    mk_lbl(mb_btn, "MB", p->accent);
    lv_obj_center(lv_obj_get_child(mb_btn, 0));

    lv_obj_t *vis = lv_btn_create(row);
    lv_obj_remove_style_all(vis);
    style_card(vis, true);
    lv_obj_set_size(vis, 42, 34);
    lv_obj_align(vis, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_add_flag(vis, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(vis, enable_slot_cb, LV_EVENT_CLICKED, NULL);
    mk_lbl(vis, hub_tr("显", "On"), p->t1);
    lv_obj_center(lv_obj_get_child(vis, 0));

#if !CONFIG_HUB_UI_ENGINEER_EDIT
    lv_obj_add_flag(type_btn, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(mb_btn, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(vis, LV_ALIGN_RIGHT_MID, -6, 0);
#endif
    return row;
}

static void room_edit_fill_body(lv_obj_t *body)
{
    if (!body) {
        return;
    }
    const hub_palette_t *p = hub_palette();
    hub_model_t *m = hub_model();
    int room = m->room_idx;
    room_edit_ensure_slots(room);

    /*
     * Fixed 12 slots: tip + scroll list + Done only (no add/delete strip).
     */
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 8, 0);

    s_edit_ui.lbl_head = mk_lbl(body, "", p->t3);
    room_edit_paint_head();
#if CONFIG_HUB_UI_ENGINEER_EDIT
    mk_lbl(body,
           hub_tr("工程师 · 类型 / MB / 显隐", "Engineer · Type / MB / Show"),
           p->t3);
#else
    mk_lbl(body,
           hub_tr("类型与点表见代码 · 此处仅显隐", "Types/MB in code · show/hide only"),
           p->t3);
#endif

    lv_obj_t *list = lv_obj_create(body);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    s_edit_ui.list = list;

    for (int i = 0; i < HUB_WIDGET_MAX; i++) {
        s_edit_ui.pool[i] = room_edit_make_row(list, room, i);
        if (s_edit_ui.pool[i]) {
            lv_obj_clear_flag(s_edit_ui.pool[i], LV_OBJ_FLAG_HIDDEN);
            room_edit_paint_row(s_edit_ui.pool[i], room, i);
        }
    }
    room_edit_paint_head();

    /* Fixed footer — Done only */
    lv_obj_t *foot = lv_obj_create(body);
    lv_obj_remove_style_all(foot);
    lv_obj_set_width(foot, LV_PCT(100));
    lv_obj_set_height(foot, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(foot, 6, 0);
    lv_obj_clear_flag(foot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_btn_create(foot);
    lv_obj_remove_style_all(back);
    style_card(back, true);
    lv_obj_set_style_bg_color(back, p->accent, 0);
    lv_obj_set_width(back, LV_PCT(100));
    lv_obj_set_height(back, 44);
    lv_obj_add_event_cb(back, go_back_cb, LV_EVENT_CLICKED, NULL);
    mk_lbl(back, hub_tr("完成", "Done"), p->ink_on);
    lv_obj_center(lv_obj_get_child(back, 0));
}

void hub_build_room_edit(lv_obj_t *parent)
{
    hub_model_t *m = hub_model();
    int room = m->room_idx;

    s_edit_gen++;
    memset(&s_edit_ui, 0, sizeof(s_edit_ui));
    s_edit_ui.gen = s_edit_gen;

    char title[40];
    snprintf(title, sizeof(title), hub_tr("编辑 · %s", "Edit · %s"), hub_model_room_name(room));
    lv_obj_t *shell = hub_shell_create(parent, title, true);
    lv_obj_t *body = hub_shell_body(shell);
    s_edit_ui.body = body;
    lv_obj_add_event_cb(shell, room_edit_shell_del_cb, LV_EVENT_DELETE, NULL);
    room_edit_fill_body(body);
}

/* ---- Modbus binding editor (RAM-only while editing; NVS on exit) ---- */

typedef struct {
    lv_obj_t *btn_en;
    lv_obj_t *lbl_en;
    lv_obj_t *lbl_slave;
    lv_obj_t *lbl_wr;
    lv_obj_t *lbl_rd;
    lv_obj_t *btn_enc;
    lv_obj_t *lbl_enc;
    lv_obj_t *lbl_enc_hint;
    lv_obj_t *row_on;
    lv_obj_t *row_offv;
    lv_obj_t *lbl_on;
    lv_obj_t *lbl_off;
    lv_obj_t *row_num;
    lv_obj_t *row_den;
    lv_obj_t *row_ofs;
    lv_obj_t *lbl_num;
    lv_obj_t *lbl_den;
    lv_obj_t *lbl_ofs;
    lv_obj_t *btn_poll;
    lv_obj_t *lbl_poll;
    uint16_t gen;
} mb_ui_t;

static mb_ui_t s_mb_ui;
static uint16_t s_mb_gen;
static bool s_mb_dirty;
static TaskHandle_t s_mb_nvs_task;

static app_modbus_binding_t *mb_cur(void)
{
    return app_modbus_map_get(hub_model()->room_idx, s_mb_edit_slot);
}

static void mb_set_toggle_look(lv_obj_t *btn, bool on)
{
    const hub_palette_t *p = hub_palette();
    if (!btn || !lv_obj_is_valid(btn)) {
        return;
    }
    lv_obj_set_style_bg_color(btn, on ? p->accent : p->bg_card, 0);
}

static void mb_set_lbl(lv_obj_t *lbl, const char *txt)
{
    if (lbl && lv_obj_is_valid(lbl) && txt) {
        lv_label_set_text(lbl, txt);
    }
}

static void mb_set_row_vis(lv_obj_t *row, bool show)
{
    if (!row || !lv_obj_is_valid(row)) {
        return;
    }
    if (show) {
        lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }
}

static void mb_paint(void)
{
    if (hub_ui_route() != HUB_ROUTE_ROOM_MB_EDIT) {
        return;
    }
    app_modbus_binding_t *b = mb_cur();
    if (!b || s_mb_ui.gen != s_mb_gen) {
        return;
    }
    char v[28];
    const bool onoff = (b->encode == APP_MB_ENC_ONOFF);

    mb_set_lbl(s_mb_ui.lbl_en, b->enabled ? hub_tr("控制  开", "Ctrl  ON ")
                                          : hub_tr("控制  关", "Ctrl OFF "));
    mb_set_toggle_look(s_mb_ui.btn_en, b->enabled);

    snprintf(v, sizeof(v), "%u", (unsigned)b->slave);
    mb_set_lbl(s_mb_ui.lbl_slave, v);
    snprintf(v, sizeof(v), "0x%04X", (unsigned)b->wr_addr);
    mb_set_lbl(s_mb_ui.lbl_wr, v);
    snprintf(v, sizeof(v), "0x%04X", (unsigned)b->rd_addr);
    mb_set_lbl(s_mb_ui.lbl_rd, v);

    mb_set_lbl(s_mb_ui.lbl_enc, onoff ? hub_tr("编码  开关量", "Encode  ONOFF")
                                      : hub_tr("编码  比例量", "Encode  LEVEL"));
    mb_set_lbl(s_mb_ui.lbl_enc_hint,
               onoff ? hub_tr("开/关写入固定寄存器值", "Write fixed ON/OFF register values")
                     : hub_tr("reg = level x 比例 / 分母 + 偏移", "reg = level x num / den + ofs"));

    snprintf(v, sizeof(v), "%u", (unsigned)b->on_val);
    mb_set_lbl(s_mb_ui.lbl_on, v);
    snprintf(v, sizeof(v), "%u", (unsigned)b->off_val);
    mb_set_lbl(s_mb_ui.lbl_off, v);
    snprintf(v, sizeof(v), "%d", (int)b->scale_num);
    mb_set_lbl(s_mb_ui.lbl_num, v);
    snprintf(v, sizeof(v), "%d", (int)b->scale_den);
    mb_set_lbl(s_mb_ui.lbl_den, v);
    snprintf(v, sizeof(v), "%d", (int)b->offset);
    mb_set_lbl(s_mb_ui.lbl_ofs, v);

    mb_set_row_vis(s_mb_ui.row_on, onoff);
    mb_set_row_vis(s_mb_ui.row_offv, onoff);
    mb_set_row_vis(s_mb_ui.row_num, !onoff);
    mb_set_row_vis(s_mb_ui.row_den, !onoff);
    mb_set_row_vis(s_mb_ui.row_ofs, !onoff);

    mb_set_lbl(s_mb_ui.lbl_poll, b->poll ? hub_tr("回读  开", "Poll  ON ")
                                         : hub_tr("回读  关", "Poll OFF "));
    mb_set_toggle_look(s_mb_ui.btn_poll, b->poll);
}

static void mb_nvs_task_fn(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_mb_dirty) {
            s_mb_dirty = false;
            (void)app_modbus_map_save();
        }
    }
}

/** Persist off the LVGL thread (NVS commit on UI thread tears the screen). */
static void mb_persist_async(void)
{
    s_mb_dirty = true;
    if (s_mb_nvs_task == NULL) {
        (void)xTaskCreate(mb_nvs_task_fn, "mb_nvs", 3072, NULL, 3, &s_mb_nvs_task);
    }
    if (s_mb_nvs_task) {
        (void)xTaskNotifyGive(s_mb_nvs_task);
    }
}

/**
 * Defer NVS until after LVGL has settled a frame.
 * Dirty MB save / add / remove tear = flash bus contention during refresh.
 */
static lv_timer_t *s_map_persist_tmr;

static void map_persist_tmr_cb(lv_timer_t *tmr)
{
    (void)tmr;
    s_map_persist_tmr = NULL; /* LVGL deletes one-shot timer after this cb */
    mb_persist_async();
}

static void map_persist_later(void)
{
    s_mb_dirty = true;
    if (s_map_persist_tmr) {
        lv_timer_reset(s_map_persist_tmr);
        return;
    }
    s_map_persist_tmr = lv_timer_create(map_persist_tmr_cb, 1200, NULL);
    if (s_map_persist_tmr) {
        lv_timer_set_repeat_count(s_map_persist_tmr, 1);
    }
}

static void mb_after_edit(void)
{
    s_mb_dirty = true;
    mb_paint(); /* labels only — no NVS / no page rebuild */
}

static void mb_shell_del_cb(lv_event_t *e)
{
    (void)e;
    s_mb_shell = NULL;
    s_mb_overlay = false;
    memset(&s_mb_ui, 0, sizeof(s_mb_ui));
}

/** Open MB on top of room-edit (keeps edit tree) — avoids save/back tear. */
static void room_mb_open_overlay(int slot)
{
    lv_obj_t *root = hub_ui_root();
    if (!root) {
        hub_room_mb_edit_set_slot(slot);
        hub_ui_go(HUB_ROUTE_ROOM_MB_EDIT);
        return;
    }
    if (s_mb_shell && lv_obj_is_valid(s_mb_shell)) {
        lv_obj_add_flag(s_mb_shell, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *old = s_mb_shell;
        s_mb_overlay = false;
        s_mb_shell = NULL;
        memset(&s_mb_ui, 0, sizeof(s_mb_ui));
        lv_obj_del(old); /* already hidden — not composed */
    }
    hub_room_mb_edit_set_slot(slot);
    hub_ui_set_route(HUB_ROUTE_ROOM_MB_EDIT);
    s_mb_overlay = true;
    hub_build_room_mb_edit(root);
    if (s_mb_shell && lv_obj_is_valid(s_mb_shell)) {
        lv_obj_move_foreground(s_mb_shell);
    }
}

void hub_room_mb_overlay_discard(void)
{
    /* Hide + drop refs. Caller (rebuild) lv_obj_clean frees the objects. */
    type_sheet_close();
    s_type_sheet = NULL;
    memset(s_type_btns, 0, sizeof(s_type_btns));
    s_mb_dirty = false;
    s_mb_overlay = false;
    if (s_mb_shell && lv_obj_is_valid(s_mb_shell)) {
        lv_obj_add_flag(s_mb_shell, LV_OBJ_FLAG_HIDDEN);
    }
    s_mb_shell = NULL;
    memset(&s_mb_ui, 0, sizeof(s_mb_ui));
}

bool hub_room_mb_overlay_try_close(void)
{
    if (!s_mb_overlay || !s_mb_shell || !lv_obj_is_valid(s_mb_shell)) {
        if (s_type_sheet && lv_obj_is_valid(s_type_sheet) &&
            !lv_obj_has_flag(s_type_sheet, LV_OBJ_FLAG_HIDDEN)) {
            edit_overlay_commit(-1, false);
            return true;
        }
        s_mb_overlay = false;
        return false;
    }
    int slot = s_mb_edit_slot;
    bool dirty = s_mb_dirty;
    s_mb_dirty = false;
    s_mb_overlay = false;
    hub_ui_set_route(HUB_ROUTE_ROOM_EDIT);

    /* One atomic reveal: hide MB + paint row under invalidation gate. */
    edit_overlay_commit(dirty ? slot : -1, dirty);
    if (dirty) {
        map_persist_later();
    }
    return true;
}

static void mb_done_cb(lv_event_t *e)
{
    (void)e;
    if (hub_room_mb_overlay_try_close()) {
        return;
    }
    bool dirty = s_mb_dirty;
    s_mb_dirty = false; /* del_cb must not persist during route rebuild */
    memset(&s_mb_ui, 0, sizeof(s_mb_ui));
    hub_ui_back();
    if (dirty) {
        map_persist_later();
    }
}

static void mb_toggle_en_cb(lv_event_t *e)
{
    (void)e;
    app_modbus_binding_t *b = mb_cur();
    if (!b) {
        return;
    }
    b->enabled = !b->enabled;
    mb_after_edit();
}

static void mb_toggle_poll_cb(lv_event_t *e)
{
    (void)e;
    app_modbus_binding_t *b = mb_cur();
    if (!b) {
        return;
    }
    b->poll = !b->poll;
    mb_after_edit();
}

static void mb_toggle_enc_cb(lv_event_t *e)
{
    (void)e;
    app_modbus_binding_t *b = mb_cur();
    if (!b) {
        return;
    }
    b->encode = (b->encode == APP_MB_ENC_ONOFF) ? APP_MB_ENC_LEVEL : APP_MB_ENC_ONOFF;
    mb_after_edit();
}

static int mb_clamp_i(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static void mb_step_cb(lv_event_t *e)
{
    w_ud_t *u = (w_ud_t *)lv_event_get_user_data(e);
    app_modbus_binding_t *b = mb_cur();
    if (!b || !u) {
        return;
    }
    /* field: 1 slave 2 wr 3 on 4 off 5 rd 6 num 7 den 8 offset */
    int field = u->room;
    int d = u->delta;
    if (field == 1) {
        b->slave = (uint8_t)mb_clamp_i((int)b->slave + d, 1, 247);
    } else if (field == 2) {
        b->wr_addr = (uint16_t)mb_clamp_i((int)b->wr_addr + d, 0, 65535);
    } else if (field == 3) {
        b->on_val = (uint16_t)mb_clamp_i((int)b->on_val + d, 0, 65535);
    } else if (field == 4) {
        b->off_val = (uint16_t)mb_clamp_i((int)b->off_val + d, 0, 65535);
    } else if (field == 5) {
        b->rd_addr = (uint16_t)mb_clamp_i((int)b->rd_addr + d, 0, 65535);
    } else if (field == 6) {
        b->scale_num = (int16_t)mb_clamp_i((int)b->scale_num + d, -1000, 1000);
    } else if (field == 7) {
        int v = mb_clamp_i((int)b->scale_den + d, -1000, 1000);
        if (v == 0) {
            v = d >= 0 ? 1 : -1;
        }
        b->scale_den = (int16_t)v;
    } else if (field == 8) {
        b->offset = (int16_t)mb_clamp_i((int)b->offset + d, -10000, 10000);
    }
    mb_after_edit();
}

static void mb_suggest_cb(lv_event_t *e)
{
    (void)e;
    hub_model_t *m = hub_model();
    int room = m->room_idx;
    int slot = s_mb_edit_slot;
    hub_widget_t *w = hub_model_widget_by_slot(room, slot);
    app_modbus_binding_t *b = app_modbus_map_get(room, slot);
    if (!w || !b) {
        return;
    }
    app_modbus_map_suggest(room, slot, w->type, b);
    mb_after_edit();
}

static void mb_sync_rd_cb(lv_event_t *e)
{
    (void)e;
    app_modbus_binding_t *b = mb_cur();
    if (!b) {
        return;
    }
    b->rd_addr = b->wr_addr;
    mb_after_edit();
}

static lv_obj_t *mb_step_row(lv_obj_t *body, const char *title, const char *value, int field,
                             int step_lo, int step_hi, const hub_palette_t *p, lv_obj_t **out_val)
{
    lv_obj_t *row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    style_card(row, false);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 52);

    mk_lbl(row, title, p->t3);
    lv_obj_align(lv_obj_get_child(row, 0), LV_ALIGN_LEFT_MID, 10, -8);
    lv_obj_t *vl = mk_lbl(row, value, p->t1);
    lv_obj_align(vl, LV_ALIGN_LEFT_MID, 10, 10);
    if (out_val) {
        *out_val = vl;
    }

    lv_obj_t *minus = lv_btn_create(row);
    lv_obj_remove_style_all(minus);
    style_card(minus, false);
    lv_obj_set_size(minus, 44, 40);
    lv_obj_align(minus, LV_ALIGN_RIGHT_MID, -54, 0);
    lv_obj_add_event_cb(minus, mb_step_cb, LV_EVENT_CLICKED, ud_alloc(field, 0, -step_lo));
    lv_obj_t *ml = lv_label_create(minus);
    lv_label_set_text(ml, "-");
    hub_style_label(ml, p->t1, hub_font_symbol());
    lv_obj_center(ml);

    lv_obj_t *plus = lv_btn_create(row);
    lv_obj_remove_style_all(plus);
    style_card(plus, false);
    lv_obj_set_size(plus, 44, 40);
    lv_obj_align(plus, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_add_event_cb(plus, mb_step_cb, LV_EVENT_CLICKED, ud_alloc(field, 0, step_hi));
    lv_obj_t *pl = lv_label_create(plus);
    lv_label_set_text(pl, "+");
    hub_style_label(pl, p->t1, hub_font_symbol());
    lv_obj_center(pl);
    return row;
}

void hub_build_room_mb_edit(lv_obj_t *parent)
{
    s_ud_n = 0;
    s_mb_gen++;
    memset(&s_mb_ui, 0, sizeof(s_mb_ui));
    s_mb_ui.gen = s_mb_gen;
    const hub_palette_t *p = hub_palette();
    hub_model_t *m = hub_model();
    int room = m->room_idx;
    int slot = s_mb_edit_slot;
    hub_widget_t *w = hub_model_widget_by_slot(room, slot);
    if (!w) {
        s_mb_overlay = false;
        hub_ui_set_route(HUB_ROUTE_ROOM_EDIT);
        return;
    }
    app_modbus_map_ensure(room, slot, w->type);
    app_modbus_binding_t *b = app_modbus_map_get(room, slot);
    if (!b) {
        s_mb_overlay = false;
        hub_ui_set_route(HUB_ROUTE_ROOM_EDIT);
        return;
    }

    char title[48];
    snprintf(title, sizeof(title), hub_tr("设备控制 · %s", "Device · %s"),
             room_wtype_label(w->type));
    lv_obj_t *shell = hub_shell_create(parent, title, true);
    s_mb_shell = shell;
    lv_obj_t *body = hub_shell_body(shell);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_add_event_cb(shell, mb_shell_del_cb, LV_EVENT_DELETE, NULL);
    if (s_mb_overlay) {
        /* Full opaque cover — prevents edit/MB residual bleed onto next frames. */
        lv_obj_set_style_bg_opa(shell, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(shell, p->bg_deep, 0);
        lv_obj_add_flag(shell, LV_OBJ_FLAG_FLOATING);
        lv_obj_move_foreground(shell);
    }

    char tip[80];
    snprintf(tip, sizeof(tip), "%s · #%d · %s", hub_model_room_name(room), slot,
             room_wtype_label(w->type));
    mk_lbl(body, tip, p->t3);

    s_mb_ui.btn_en = lv_btn_create(body);
    lv_obj_remove_style_all(s_mb_ui.btn_en);
    style_card(s_mb_ui.btn_en, b->enabled);
    lv_obj_set_width(s_mb_ui.btn_en, LV_PCT(100));
    lv_obj_set_height(s_mb_ui.btn_en, 48);
    lv_obj_add_event_cb(s_mb_ui.btn_en, mb_toggle_en_cb, LV_EVENT_CLICKED, NULL);
    s_mb_ui.lbl_en = mk_lbl(s_mb_ui.btn_en, "", p->t1);
    lv_obj_center(s_mb_ui.lbl_en);

    char v[28];
    snprintf(v, sizeof(v), "%u", (unsigned)b->slave);
    mb_step_row(body, hub_tr("站号 (1-247)", "Slave 1-247"), v, 1, 1, 1, p, &s_mb_ui.lbl_slave);

    snprintf(v, sizeof(v), "0x%04X", (unsigned)b->wr_addr);
    mb_step_row(body, hub_tr("写地址 FC06  -1/+16", "Write addr -1/+16"), v, 2, 1, 16, p,
                &s_mb_ui.lbl_wr);

    snprintf(v, sizeof(v), "0x%04X", (unsigned)b->rd_addr);
    mb_step_row(body, hub_tr("读地址 FC03  -1/+16", "Read addr -1/+16"), v, 5, 1, 16, p,
                &s_mb_ui.lbl_rd);

    lv_obj_t *sync = lv_btn_create(body);
    lv_obj_remove_style_all(sync);
    style_card(sync, false);
    lv_obj_set_width(sync, LV_PCT(100));
    lv_obj_set_height(sync, 40);
    lv_obj_add_event_cb(sync, mb_sync_rd_cb, LV_EVENT_CLICKED, NULL);
    mk_lbl(sync, hub_tr("读地址 = 写地址", "Read addr = write addr"), p->accent);
    lv_obj_center(lv_obj_get_child(sync, 0));

    s_mb_ui.btn_enc = lv_btn_create(body);
    lv_obj_remove_style_all(s_mb_ui.btn_enc);
    style_card(s_mb_ui.btn_enc, false);
    lv_obj_set_width(s_mb_ui.btn_enc, LV_PCT(100));
    lv_obj_set_height(s_mb_ui.btn_enc, 48);
    lv_obj_add_event_cb(s_mb_ui.btn_enc, mb_toggle_enc_cb, LV_EVENT_CLICKED, NULL);
    s_mb_ui.lbl_enc = mk_lbl(s_mb_ui.btn_enc, "", p->t1);
    lv_obj_center(s_mb_ui.lbl_enc);

    s_mb_ui.lbl_enc_hint = mk_lbl(body, "", p->t3);

    snprintf(v, sizeof(v), "%u", (unsigned)b->on_val);
    s_mb_ui.row_on = mb_step_row(body, hub_tr("开值", "ON value"), v, 3, 1, 1, p, &s_mb_ui.lbl_on);
    snprintf(v, sizeof(v), "%u", (unsigned)b->off_val);
    s_mb_ui.row_offv =
        mb_step_row(body, hub_tr("关值", "OFF value"), v, 4, 1, 1, p, &s_mb_ui.lbl_off);

    snprintf(v, sizeof(v), "%d", (int)b->scale_num);
    s_mb_ui.row_num = mb_step_row(body, hub_tr("比例 (分子)", "Scale num"), v, 6, 1, 1, p,
                                  &s_mb_ui.lbl_num);
    snprintf(v, sizeof(v), "%d", (int)b->scale_den);
    s_mb_ui.row_den = mb_step_row(body, hub_tr("分母", "Scale den"), v, 7, 1, 1, p, &s_mb_ui.lbl_den);
    snprintf(v, sizeof(v), "%d", (int)b->offset);
    s_mb_ui.row_ofs = mb_step_row(body, hub_tr("偏移", "Offset"), v, 8, 1, 10, p, &s_mb_ui.lbl_ofs);

    s_mb_ui.btn_poll = lv_btn_create(body);
    lv_obj_remove_style_all(s_mb_ui.btn_poll);
    style_card(s_mb_ui.btn_poll, b->poll);
    lv_obj_set_width(s_mb_ui.btn_poll, LV_PCT(100));
    lv_obj_set_height(s_mb_ui.btn_poll, 48);
    lv_obj_add_event_cb(s_mb_ui.btn_poll, mb_toggle_poll_cb, LV_EVENT_CLICKED, NULL);
    s_mb_ui.lbl_poll = mk_lbl(s_mb_ui.btn_poll, "", p->t1);
    lv_obj_center(s_mb_ui.lbl_poll);

    lv_obj_t *sug = lv_btn_create(body);
    lv_obj_remove_style_all(sug);
    style_card(sug, false);
    lv_obj_set_width(sug, LV_PCT(100));
    lv_obj_set_height(sug, 44);
    lv_obj_add_event_cb(sug, mb_suggest_cb, LV_EVENT_CLICKED, NULL);
    mk_lbl(sug, hub_tr("按房间/槽位重填默认", "Reset to suggested defaults"), p->accent);
    lv_obj_center(lv_obj_get_child(sug, 0));

    lv_obj_t *done = lv_btn_create(body);
    lv_obj_remove_style_all(done);
    style_card(done, true);
    lv_obj_set_style_bg_color(done, p->accent, 0);
    lv_obj_set_width(done, LV_PCT(100));
    lv_obj_set_height(done, 48);
    lv_obj_add_event_cb(done, mb_done_cb, LV_EVENT_CLICKED, NULL);
    mk_lbl(done, hub_tr("保存返回", "Save & back"), p->ink_on);
    lv_obj_center(lv_obj_get_child(done, 0));

    mb_paint();
}

static void toggle_living_type(hub_wtype_t type)
{
    hub_model_t *m = hub_model();
    for (int i = 0; i < m->widget_count[0]; i++) {
        if (m->widgets[0][i].type == type) {
            hub_model_toggle_widget(0, i);
            bus_notify(0, i);
            return;
        }
    }
}

static void app_toggle_cb(lv_event_t *e)
{
    int kind = (int)(uintptr_t)lv_event_get_user_data(e);
    hub_room_dev_t *d = &hub_model()->rooms[0];
    if (kind == 0) {
        d->pump_ok = !d->pump_ok;
        hub_model_toast(d->pump_ok ? hub_tr("水泵 On", "Pump on") : hub_tr("水泵 Off", "Pump off"));
    } else if (kind == 1) {
        d->valve_ok = !d->valve_ok;
        hub_model_toast(d->valve_ok ? hub_tr("水阀 On", "Valve on") : hub_tr("水阀 Off", "Valve off"));
    } else if (kind == 2) {
        toggle_living_type(HUB_W_PLUG);
        hub_model_toast(hub_tr("插座已切换", "Socket toggled"));
    } else {
        toggle_living_type(HUB_W_FAN);
        hub_model_toast(hub_tr("风扇已切换", "Fan toggled"));
    }
    hub_ui_refresh();
}

void hub_build_appliances(lv_obj_t *parent)
{
    const hub_palette_t *p = hub_palette();
    hub_room_dev_t *d = &hub_model()->rooms[0];
    lv_obj_t *shell = hub_shell_create(parent, hub_tr("设备", "Devices"), true);
    lv_obj_t *body = hub_shell_body(shell);

    struct {
        const char *zh;
        const char *en;
        bool on;
        int kind;
        hub_ico_t ico;
    } rows[] = {
        { "水泵 PUMP", "Pump", d->pump_ok, 0, HUB_ICO_DROP },
        { "水阀 VALVE", "Valve", d->valve_ok, 1, HUB_ICO_DROP },
        { "客厅插座", "Living socket", d->plug_on, 2, HUB_ICO_PLUG },
        { "客厅风扇", "Living fan", d->fan_on, 3, HUB_ICO_FAN },
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *row = lv_btn_create(body);
        lv_obj_remove_style_all(row);
        style_card(row, rows[i].on);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 56);
        lv_obj_add_event_cb(row, app_toggle_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)rows[i].kind);
        hub_ico_add(row, rows[i].ico, p->accent, 20);
        lv_obj_align(lv_obj_get_child(row, 0), LV_ALIGN_LEFT_MID, 8, 0);
        mk_lbl(row, hub_tr(rows[i].zh, rows[i].en), p->t1);
        lv_obj_align(lv_obj_get_child(row, 1), LV_ALIGN_LEFT_MID, 40, 0);
        mk_lbl(row, rows[i].on ? "On" : "Off", rows[i].on ? p->on : p->t3);
        lv_obj_align(lv_obj_get_child(row, 2), LV_ALIGN_RIGHT_MID, -8, 0);
    }
}

/* ── Security (PIN-gated disarm) ─────────────────────────────────── */

static char s_pin[5];
static bool s_pin_mode; /* true = waiting for PIN to disarm */

static void arm_now_cb(lv_event_t *e)
{
    (void)e;
    hub_model_set_armed(true);
    hub_model_toast(hub_tr("已布防", "Armed"));
    s_pin[0] = '\0';
    s_pin_mode = false;
    hub_ui_refresh();
}

static void disarm_req_cb(lv_event_t *e)
{
    (void)e;
    s_pin_mode = true;
    s_pin[0] = '\0';
    hub_model_toast(hub_tr("输入 PIN 撤防", "Enter PIN to disarm"));
    hub_ui_refresh();
}

static void pin_key_cb(lv_event_t *e)
{
    const char *key = (const char *)lv_event_get_user_data(e);
    if (!key) {
        return;
    }
    if (key[0] == 'C') {
        s_pin[0] = '\0';
        hub_ui_refresh();
        return;
    }
    if (key[0] == 'O') {
        if (strcmp(s_pin, "1234") == 0) {
            hub_model_set_armed(false);
            hub_model_toast(hub_tr("已撤防", "Disarmed"));
            s_pin_mode = false;
        } else {
            hub_model_toast(hub_tr("PIN 错误", "Wrong PIN"));
        }
        s_pin[0] = '\0';
        hub_ui_refresh();
        return;
    }
    size_t n = strlen(s_pin);
    if (n < 4) {
        s_pin[n] = key[0];
        s_pin[n + 1] = '\0';
        hub_ui_refresh();
    }
}

void hub_build_security(lv_obj_t *parent)
{
    const hub_palette_t *p = hub_palette();
    hub_model_t *m = hub_model();
    bool zen = p->name && strcmp(p->name, "zen") == 0;
    lv_obj_t *shell = hub_shell_create(parent, hub_tr("安防", "Security"), true);
    lv_obj_t *body = hub_shell_body(shell);

    /* FE AlarmWidget page: centered badge + Security + Armed·Away */
    lv_obj_t *card = lv_btn_create(body);
    lv_obj_remove_style_all(card);
    if (zen) {
        lv_obj_set_style_radius(card, 0, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_side(card, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, m->armed ? p->t1 : p->line, 0);
    } else {
        style_card(card, m->armed);
    }
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 14, 0);
    lv_obj_add_event_cb(card, m->armed ? disarm_req_cb : arm_now_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *badge = lv_obj_create(card);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, 96, 96);
    lv_obj_set_style_radius(badge, zen ? 0 : 20, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(badge,
                              lv_color_mix(m->armed ? p->on : p->t3, p->bg_card, LV_OPA_20), 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    hub_ico_add(badge, HUB_ICO_SHIELD, m->armed ? p->on : p->t3, 48);
    lv_obj_center(lv_obj_get_child(badge, 0));

    mk_lbl(card, hub_tr("安防", "Security"), p->t1);
    mk_lbl(card, m->armed ? hub_tr("已布防 · 离家", "Armed · Away") : hub_tr("已撤防", "Disarmed"),
           m->armed ? p->on : p->t3);

    lv_obj_t *row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 48);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);

    lv_obj_t *arm = lv_btn_create(row);
    lv_obj_remove_style_all(arm);
    if (zen) {
        lv_obj_set_style_radius(arm, 0, 0);
        lv_obj_set_style_bg_opa(arm, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(arm, p->accent, 0);
    } else {
        style_card(arm, true);
        lv_obj_set_style_bg_color(arm, p->accent, 0);
    }
    lv_obj_set_flex_grow(arm, 1);
    lv_obj_set_height(arm, LV_PCT(100));
    lv_obj_add_event_cb(arm, arm_now_cb, LV_EVENT_CLICKED, NULL);
    mk_lbl(arm, hub_tr("布防", "Arm"), p->ink_on);
    lv_obj_center(lv_obj_get_child(arm, 0));

    lv_obj_t *dis = lv_btn_create(row);
    lv_obj_remove_style_all(dis);
    if (zen) {
        lv_obj_set_style_radius(dis, 0, 0);
        lv_obj_set_style_border_width(dis, 1, 0);
        lv_obj_set_style_border_color(dis, p->line, 0);
        lv_obj_set_style_bg_opa(dis, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dis, p->bg_card, 0);
    } else {
        style_card(dis, false);
    }
    lv_obj_set_flex_grow(dis, 1);
    lv_obj_set_height(dis, LV_PCT(100));
    lv_obj_add_event_cb(dis, disarm_req_cb, LV_EVENT_CLICKED, NULL);
    mk_lbl(dis, hub_tr("撤防 PIN", "Disarm PIN"), p->t1);
    lv_obj_center(lv_obj_get_child(dis, 0));

    if (s_pin_mode) {
        lv_obj_t *dots = lv_obj_create(body);
        lv_obj_remove_style_all(dots);
        lv_obj_set_width(dots, LV_PCT(100));
        lv_obj_set_height(dots, 24);
        lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(dots, 12, 0);
        size_t pn = strlen(s_pin);
        for (int i = 0; i < 4; i++) {
            lv_obj_t *d = lv_obj_create(dots);
            lv_obj_remove_style_all(d);
            lv_obj_set_size(d, 12, 12);
            lv_obj_set_style_radius(d, zen ? 0 : 2, 0);
            if ((size_t)i < pn) {
                lv_obj_set_style_bg_color(d, p->accent, 0);
                lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
            } else {
                lv_obj_set_style_border_width(d, 2, 0);
                lv_obj_set_style_border_color(d, p->t4, 0);
            }
        }

        lv_obj_t *pad = lv_obj_create(body);
        lv_obj_remove_style_all(pad);
        lv_obj_set_width(pad, LV_PCT(100));
        lv_obj_set_height(pad, 200);
        lv_obj_set_layout(pad, LV_LAYOUT_GRID);
        lv_obj_set_style_pad_row(pad, 6, 0);
        lv_obj_set_style_pad_column(pad, 6, 0);
        static lv_coord_t cols[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
        static lv_coord_t rows[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
        lv_obj_set_grid_dsc_array(pad, cols, rows);
        const char *keys[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "C", "0", "O" };
        for (int i = 0; i < 12; i++) {
            lv_obj_t *k = lv_btn_create(pad);
            lv_obj_remove_style_all(k);
            if (zen) {
                lv_obj_set_style_radius(k, 0, 0);
                lv_obj_set_style_border_width(k, 1, 0);
                lv_obj_set_style_border_color(k, p->line, 0);
                lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
                lv_obj_set_style_bg_color(k, p->bg_card, 0);
            } else {
                style_card(k, false);
            }
            lv_obj_set_grid_cell(k, LV_GRID_ALIGN_STRETCH, i % 3, 1, LV_GRID_ALIGN_STRETCH, i / 3, 1);
            lv_obj_add_event_cb(k, pin_key_cb, LV_EVENT_CLICKED, (void *)keys[i]);
            const char *lab = keys[i];
            if (keys[i][0] == 'O') {
                lab = "OK";
            } else if (keys[i][0] == 'C') {
                lab = hub_tr("清除", "Clear");
            }
            mk_lbl(k, lab, p->t1);
            lv_obj_center(lv_obj_get_child(k, 0));
        }
    }

    (void)go_home_cb;
    (void)go_scenes_cb;
    (void)refresh_cb;
}

