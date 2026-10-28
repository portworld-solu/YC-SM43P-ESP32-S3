#include "hub_ui.h"
#include "hub_model.h"
#include "hub_device_ui.h"

#include <stdbool.h>

typedef struct {
    lv_coord_t x;
    lv_coord_t y;
    bool active;
    bool blocked;
} swipe_state_t;

static swipe_state_t s_swipe;
static uint32_t s_last_swipe_ms;

static const hub_route_t s_pager[] = {
    HUB_ROUTE_HOME,
    HUB_ROUTE_ROOM,
    HUB_ROUTE_SCENES,
    HUB_ROUTE_ENERGY,
};
#define PAGER_N (sizeof(s_pager) / sizeof(s_pager[0]))
#define ROOM_PAGE_SIZE 6

/* Stricter than before: reduce accidental page changes. */
#define SWIPE_MIN_DX 110
#define SWIPE_DOMINANCE_NUM 18 /* dx must be > dy * 1.8 */
#define SWIPE_DOMINANCE_DEN 10
#define SWIPE_COOLDOWN_MS 450

static int pager_index(hub_route_t r)
{
    for (int i = 0; i < (int)PAGER_N; i++) {
        if (s_pager[i] == r) {
            return i;
        }
    }
    return -1;
}

bool hub_nav_obj_blocks_swipe(const lv_obj_t *obj)
{
    while (obj) {
        /* Exact-class checks (LVGL 8) — sliders/inputs must not drive page swipe. */
        if (lv_obj_check_type(obj, &lv_slider_class) || lv_obj_check_type(obj, &lv_bar_class) ||
            lv_obj_check_type(obj, &lv_textarea_class) || lv_obj_check_type(obj, &lv_keyboard_class) ||
            lv_obj_check_type(obj, &lv_dropdown_class) || lv_obj_check_type(obj, &lv_roller_class) ||
            lv_obj_check_type(obj, &lv_switch_class) || lv_obj_check_type(obj, &lv_arc_class)) {
            return true;
        }
        obj = lv_obj_get_parent((lv_obj_t *)obj);
    }
    return false;
}

static void do_swipe(bool to_next)
{
    uint32_t now = lv_tick_get();
    if (s_last_swipe_ms != 0 && lv_tick_elaps(s_last_swipe_ms) < SWIPE_COOLDOWN_MS) {
        return;
    }

    hub_route_t cur = hub_ui_route();

    /* Only main pager routes participate — settings/network never swipe-switch. */
    int idx = pager_index(cur);
    if (idx < 0 && cur != HUB_ROUTE_ROOM) {
        return;
    }

    /* Room AdaptiveGrid: swipe across subpages before leaving room route */
    if (cur == HUB_ROUTE_ROOM) {
        hub_model_t *m = hub_model();
        int enabled = hub_model_enabled_widget_count(m->room_idx);
        int pages = enabled > 0 ? (enabled + ROOM_PAGE_SIZE - 1) / ROOM_PAGE_SIZE : 1;
        if (pages > 1) {
            if (to_next) {
                if (m->room_subpage < pages - 1) {
                    m->room_subpage++;
                    s_last_swipe_ms = now;
                    hub_ui_refresh();
                    return;
                }
            } else if (m->room_subpage > 0) {
                m->room_subpage--;
                s_last_swipe_ms = now;
                hub_ui_refresh();
                return;
            }
        }
        idx = pager_index(HUB_ROUTE_ROOM);
    }

    if (idx < 0) {
        return;
    }

    /* No wrap-around — wrapping felt like random jumps (Home↔Energy). */
    if (to_next) {
        if (idx + 1 >= (int)PAGER_N) {
            return;
        }
        s_last_swipe_ms = now;
        hub_ui_go(s_pager[idx + 1]);
    } else {
        if (idx <= 0) {
            return;
        }
        s_last_swipe_ms = now;
        hub_ui_go(s_pager[idx - 1]);
    }
}

static void on_pressed(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) {
        return;
    }
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    s_swipe.x = pt.x;
    s_swipe.y = pt.y;
    s_swipe.active = true;
    s_swipe.blocked = hub_nav_obj_blocks_swipe(lv_indev_get_obj_act());
}

static void on_pressing(lv_event_t *e)
{
    (void)e;
    if (!s_swipe.active || s_swipe.blocked) {
        return;
    }
    /* If finger moves onto a slider mid-gesture, cancel pager swipe. */
    lv_obj_t *act = lv_indev_get_obj_act();
    if (hub_nav_obj_blocks_swipe(act)) {
        s_swipe.blocked = true;
        s_swipe.active = false;
    }
}

static void on_released(lv_event_t *e)
{
    (void)e;
    if (!s_swipe.active) {
        s_swipe.blocked = false;
        return;
    }
    bool blocked = s_swipe.blocked;
    s_swipe.active = false;
    s_swipe.blocked = false;
    if (blocked) {
        return;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) {
        return;
    }
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    lv_coord_t dx = pt.x - s_swipe.x;
    lv_coord_t dy = pt.y - s_swipe.y;
    if (LV_ABS(dx) < SWIPE_MIN_DX) {
        return;
    }
    if (LV_ABS(dx) * SWIPE_DOMINANCE_DEN < LV_ABS(dy) * SWIPE_DOMINANCE_NUM) {
        return;
    }
    do_swipe(dx < 0);
}

void hub_nav_attach(lv_obj_t *root)
{
    /* Use press/release only — do NOT also handle LV_EVENT_GESTURE (double-fire). */
    lv_obj_add_event_cb(root, on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(root, on_pressing, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(root, on_released, LV_EVENT_RELEASED, NULL);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

void hub_nav_enable_gesture_bubble(lv_obj_t *root)
{
    if (!root) {
        return;
    }
    uint32_t n = lv_obj_get_child_cnt(root);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(root, i);
        if (!c) {
            continue;
        }
        /* Interactive controls must NOT bubble horizontal drags to the pager. */
        if (hub_nav_obj_blocks_swipe(c)) {
            lv_obj_clear_flag(c, LV_OBJ_FLAG_GESTURE_BUBBLE);
            continue;
        }
        lv_obj_add_flag(c, LV_OBJ_FLAG_GESTURE_BUBBLE);
        hub_nav_enable_gesture_bubble(c);
    }
}
