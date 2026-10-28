#include "hub_ui.h"
#include "hub_theme.h"
#include "hub_font.h"
#include "hub_model.h"
#include "hub_icons.h"
#include "hub_room_ui.h"
#include "hub_device_ui.h"
#include "hub_standby.h"
#include "hub_climate_offset.h"

#include "esp_log.h"
#include "withthewind_board_lvgl_init.h"
#include "nvs.h"

#include <string.h>

static const char *TAG = "hub_ui";
static lv_obj_t *s_root;
static hub_route_t s_route = HUB_ROUTE_HOME;

#define HUB_HIST_MAX 12
static hub_route_t s_hist[HUB_HIST_MAX];
static int s_hist_n;

static bool s_restore_scroll;
static lv_coord_t s_scroll_y;

void hub_apply_screen_bg(lv_obj_t *scr)
{
    const hub_palette_t *p = hub_palette();
    lv_obj_set_style_bg_color(scr, p->bg_deep, 0);
    lv_obj_set_style_bg_grad_color(scr, p->bg_base, 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
}

void hub_apply_card(lv_obj_t *obj, bool active)
{
    const hub_palette_t *p = hub_palette();
    lv_obj_set_style_bg_color(obj, active ? p->bg_elev : p->bg_card, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, p->radius, 0);
    lv_obj_set_style_border_width(obj, active ? 2 : 1, 0);
    lv_obj_set_style_border_color(obj, active ? p->accent : p->line, 0);
    lv_obj_set_style_pad_all(obj, 10, 0);
}

void hub_apply_card_themed(lv_obj_t *obj, bool active)
{
    hub_apply_card(obj, active);
    theme_card_chrome(obj, active);
}

void hub_style_label(lv_obj_t *label, lv_color_t color, const lv_font_t *font)
{
    lv_obj_set_style_text_color(label, color, 0);
    const lv_font_t *use = font;
    /* Unify: anything that isn't an explicit clock/symbol → hub body face */
    if (use == NULL || use == LV_FONT_DEFAULT || use == hub_font_small()) {
        use = hub_font();
    }
    lv_obj_set_style_text_font(label, use, 0);
}

static bool route_is_main_pager(hub_route_t r)
{
    return r == HUB_ROUTE_HOME || r == HUB_ROUTE_ROOM || r == HUB_ROUTE_SCENES ||
           r == HUB_ROUTE_ENERGY;
}

static lv_obj_t *find_shell_body(void)
{
    if (!s_root) {
        return NULL;
    }
    uint32_t n = lv_obj_get_child_cnt(s_root);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(s_root, i);
        if (!c) {
            continue;
        }
        lv_obj_t *body = (lv_obj_t *)lv_obj_get_user_data(c);
        if (body && lv_obj_has_flag(body, LV_OBJ_FLAG_SCROLLABLE)) {
            return body;
        }
    }
    return NULL;
}

static void capture_scroll(void)
{
    s_restore_scroll = false;
    s_scroll_y = 0;
    lv_obj_t *body = find_shell_body();
    if (body) {
        s_scroll_y = lv_obj_get_scroll_y(body);
        s_restore_scroll = true;
    }
}

static void restore_scroll(void)
{
    if (!s_restore_scroll) {
        return;
    }
    lv_obj_t *body = find_shell_body();
    if (body) {
        lv_obj_update_layout(body);
        lv_obj_scroll_to_y(body, s_scroll_y, LV_ANIM_OFF);
    }
    s_restore_scroll = false;
}

static void rebuild_route(void)
{
    const hub_palette_t *p = hub_palette();
    lv_disp_t *disp = lv_disp_get_default();
    /*
     * Tear-free rebuild:
     * 1) drop MB/type overlays so they cannot residual onto the next page
     * 2) hide root so mid-clean frames are not composed
     * 3) disable invalidation while the tree is rebuilt
     * 4) show + single lv_refr_now so the panel gets one complete frame
     */
    if (s_root) {
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    }
    hub_room_mb_overlay_discard();
    if (disp) {
        lv_disp_enable_invalidation(disp, false);
    }
    lv_obj_clean(s_root);
    hub_apply_screen_bg(s_root);
    /* FE Sand/Zen are paper-flat; wash blobs only for atmospheric themes */
    if (p->name && strcmp(p->name, "sand") != 0 && strcmp(p->name, "zen") != 0 &&
        strcmp(p->name, "metro") != 0) {
        hub_theme_wash(s_root, p->accent, LV_OPA_20);
    }
    /* Entire UI tree comes from the active theme pack — no shared pages. */
    hub_theme_build(s_root, s_route);
    /* Gesture bubbles from cards/buttons up to s_root (see hub_nav). */
    hub_nav_enable_gesture_bubble(s_root);
    hub_wifi_sync_model();
    hub_toast_present(s_root);
    restore_scroll();
    if (s_root) {
        lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    }
    if (disp) {
        lv_disp_enable_invalidation(disp, true);
        lv_obj_invalidate(s_root);
        lv_refr_now(disp);
    }
}

static void go_internal(hub_route_t route, bool push_hist, bool keep_scroll)
{
    if (route < 0 || route >= HUB_ROUTE_COUNT) {
        return;
    }
    if (s_root && route == s_route) {
        if (keep_scroll) {
            capture_scroll();
        }
        rebuild_route();
        return;
    }
    if (s_route == HUB_ROUTE_STANDBY && route != HUB_ROUTE_STANDBY) {
        hub_standby_on_exit_route();
    }
    if (push_hist && s_root) {
        if (route_is_main_pager(route)) {
            s_hist_n = 0;
        } else if (s_hist_n < HUB_HIST_MAX) {
            if (s_hist_n == 0 || s_hist[s_hist_n - 1] != s_route) {
                s_hist[s_hist_n++] = s_route;
            }
        }
    }
    s_route = route;
    if (s_root) {
        if (keep_scroll) {
            capture_scroll();
        } else {
            s_restore_scroll = false;
        }
        rebuild_route();
    }
}

void hub_ui_go(hub_route_t route)
{
    go_internal(route, true, false);
}

void hub_ui_back(void)
{
    /* MB opened as overlay on room-edit — close without tearing rebuild. */
    if (hub_room_mb_overlay_try_close()) {
        return;
    }
    if (s_hist_n > 0) {
        hub_route_t r = s_hist[--s_hist_n];
        go_internal(r, false, false);
        return;
    }
    go_internal(HUB_ROUTE_HOME, false, false);
}

lv_obj_t *hub_ui_root(void)
{
    return s_root;
}

void hub_ui_set_route(hub_route_t route)
{
    if (route >= 0 && route < HUB_ROUTE_COUNT) {
        s_route = route;
    }
}

void hub_ui_refresh(void)
{
    if (s_root) {
        capture_scroll();
        rebuild_route();
    }
}

bool hub_ui_route_is_editing(void)
{
    return s_route == HUB_ROUTE_ROOM_EDIT || s_route == HUB_ROUTE_ROOM_MB_EDIT;
}

void hub_ui_notify_soft(void)
{
    if (!s_root) {
        return;
    }
    /* Full rebuild mid-edit tears the page (LVGL tree wiped while user taps). */
    if (hub_ui_route_is_editing()) {
        hub_toast_present(s_root);
        return;
    }
    hub_ui_refresh();
}

hub_route_t hub_ui_route(void)
{
    return s_route;
}

void hub_ui_init(void)
{
    const hub_palette_t *p = hub_palette();
    ESP_LOGI(TAG, "hub theme pack (self-contained pages): %s", p->name ? p->name : "?");

    hub_font_init();
    hub_model_init();
    /* Customer default: English UI (settings can still toggle to 中文). */
    hub_model()->settings.lang_zh = false;
    hub_climate_offset_init();
    hub_wifi_sync_model();
    /* Decode SPIFFS PNG icons once into PSRAM — avoids lodepng on every page switch. */
    hub_ico_cache_init();

    /* Restore saved backlight for hub themes (default theme uses ui_runtime NVS). */
    {
        nvs_handle_t h;
        uint8_t bright = 100;
        if (nvs_open("hub_ui", NVS_READONLY, &h) == ESP_OK) {
            (void)nvs_get_u8(h, "bright", &bright);
            nvs_close(h);
        }
        bright = (uint8_t)board_backlight_clamp_percent((int)bright);
        (void)board_backlight_set(board_backlight_quantize_percent((int)bright));
    }

    lv_disp_t *disp = lv_disp_get_default();
    if (disp) {
        lv_theme_t *th = lv_theme_default_init(disp, p->accent, p->alert, true, hub_font());
        lv_disp_set_theme(disp, th);
    }

    s_root = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, 480, 480);
    hub_apply_screen_bg(s_root);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    hub_nav_attach(s_root);
    s_route = HUB_ROUTE_HOME;
    s_hist_n = 0;
    rebuild_route();
    lv_disp_load_scr(s_root);
    hub_standby_init();
}
