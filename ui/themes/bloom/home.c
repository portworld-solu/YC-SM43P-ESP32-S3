/** Bloom — ThemeHubBloom: airy 2×2 bubbles, not flex-crushed */
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
    /*
     * Fixed 480 canvas with absolute bands (FE vertical rhythm):
     *   head 0–100 | rooms 100–340 | quick 340–388 | pills 388–456
     * Avoid flex_grow crushing when wash orbs / content fight for space.
     */
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 480, 480);
    lv_obj_set_style_bg_color(root, p->bg_deep, 0);
    lv_obj_set_style_bg_grad_color(root, p->bg_base, 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /* Wash orbs — floating, never in layout */
    lv_obj_t *orb_a = lv_obj_create(root);
    lv_obj_remove_style_all(orb_a);
    lv_obj_set_size(orb_a, 220, 180);
    lv_obj_set_style_radius(orb_a, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(orb_a, p->accent, 0);
    lv_obj_set_style_bg_opa(orb_a, LV_OPA_20, 0);
    lv_obj_add_flag(orb_a, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(orb_a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(orb_a, LV_ALIGN_TOP_LEFT, -40, -60);

    lv_obj_t *orb_b = lv_obj_create(root);
    lv_obj_remove_style_all(orb_b);
    lv_obj_set_size(orb_b, 200, 160);
    lv_obj_set_style_radius(orb_b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(orb_b, p->cool, 0);
    lv_obj_set_style_bg_opa(orb_b, LV_OPA_20, 0);
    lv_obj_add_flag(orb_b, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(orb_b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(orb_b, LV_ALIGN_BOTTOM_RIGHT, 30, 40);

    /* ── Header band ─────────────────────────────────────────── */
    lv_obj_t *head = lv_obj_create(root);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, 480, 100);
    lv_obj_set_pos(head, 0, 0);
    lv_obj_set_style_pad_top(head, 24, 0);
    lv_obj_set_style_pad_hor(head, 24, 0);
    lv_obj_set_style_pad_bottom(head, 8, 0);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(head, LV_OBJ_FLAG_IGNORE_LAYOUT);

    lv_obj_t *hl = lv_obj_create(head);
    lv_obj_remove_style_all(hl);
    lv_obj_set_size(hl, 280, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hl, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(hl, 4, 0);
    lv_obj_align(hl, LV_ALIGN_LEFT_MID, 0, 0);
    lbl(hl, "Bloom", p->accent, hub_font());
    lv_obj_t *clk = hub_clock_label(hl, p->t1, hub_font_clock());
    lv_obj_set_style_text_letter_space(clk, -1, 0);
    {
        char clim[40];
        const bool clim_ok = hub_climate_ui_available();
        if (clim_ok) {
            snprintf(clim, sizeof(clim), hub_tr("室内 %.1f° · %d%%", "Indoor %.1f° · %d%%"),
                     (double)hub_climate_display_temp_c(), hub_climate_display_rh());
        } else {
            snprintf(clim, sizeof(clim), "%s", hub_tr("室内 --° · --%", "Indoor --° · --%"));
        }
        lv_obj_t *clim_btn = lv_btn_create(hl);
        lv_obj_remove_style_all(clim_btn);
        lv_obj_set_width(clim_btn, LV_SIZE_CONTENT);
        lv_obj_set_height(clim_btn, LV_SIZE_CONTENT);
        lv_obj_add_event_cb(clim_btn, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_SENSOR_CAL);
        lbl(clim_btn, clim, clim_ok ? p->t1 : p->t3, hub_font());
    }

    lv_obj_t *wifi = lv_btn_create(head);
    lv_obj_remove_style_all(wifi);
    lv_obj_set_size(wifi, 64, 64);
    lv_obj_set_style_radius(wifi, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(wifi, p->bg_card, 0);
    lv_obj_set_style_bg_opa(wifi, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wifi, 1, 0);
    lv_obj_set_style_border_color(wifi, p->line, 0);
    lv_obj_set_style_shadow_width(wifi, 16, 0);
    lv_obj_set_style_shadow_opa(wifi, LV_OPA_20, 0);
    lv_obj_set_style_shadow_ofs_y(wifi, 6, 0);
    lv_obj_set_style_shadow_color(wifi, lv_color_make(90, 60, 80), 0);
    lv_obj_align(wifi, LV_ALIGN_RIGHT_MID, 0, 4);
    lv_obj_add_event_cb(wifi, go_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)HUB_ROUTE_NETWORK);
    hub_ico_add(wifi, m->protos[3].ok ? HUB_ICO_WIFI : HUB_ICO_WIFI_OFF, p->accent, 22);
    lv_obj_center(lv_obj_get_child(wifi, 0));

    /* ── Room 2×2 — fixed cell size, airy gap (FE minHeight 96, gap 12) ── */
    const lv_coord_t grid_x = 20;
    const lv_coord_t grid_y = 108;
    const lv_coord_t gap = 14;
    const lv_coord_t cell_w = (480 - grid_x * 2 - gap) / 2; /* 213 */
    const lv_coord_t cell_h = 108;

    lv_color_t hues[] = {
        lv_color_make(232, 145, 168), lv_color_make(126, 184, 201),
        lv_color_make(232, 160, 106), lv_color_make(184, 160, 208),
    };
    for (int i = 0; i < 4; i++) {
        bool on = hub_model_room_active(i);
        int col = i % 2;
        int row = i / 2;
        lv_obj_t *b = lv_btn_create(root);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, cell_w, cell_h);
        lv_obj_set_pos(b, grid_x + col * (cell_w + gap), grid_y + row * (cell_h + gap));
        lv_obj_add_flag(b, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_style_radius(b, 28, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, on ? lv_color_mix(hues[i], p->bg_card, LV_OPA_40) : p->bg_card, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, lv_color_make(255, 255, 255), 0);
        lv_obj_set_style_border_opa(b, LV_OPA_70, 0);
        lv_obj_set_style_shadow_width(b, 18, 0);
        lv_obj_set_style_shadow_opa(b, LV_OPA_20, 0);
        lv_obj_set_style_shadow_ofs_y(b, 8, 0);
        lv_obj_set_style_shadow_color(b, lv_color_make(90, 60, 80), 0);
        lv_obj_set_style_pad_all(b, 14, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(b, room_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        /* Column stack: badge → spacer → title → status (no overlap) */
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(b, 0, 0);

        lv_obj_t *badge = lv_obj_create(b);
        lv_obj_remove_style_all(badge);
        lv_obj_set_size(badge, 40, 40);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(badge, lv_color_mix(hues[i], p->bg_card, LV_OPA_30), 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        hub_ico_add(badge, hub_ico_for_room(i), hues[i], 24);
        lv_obj_center(lv_obj_get_child(badge, 0));

        lv_obj_t *sp = lv_obj_create(b);
        lv_obj_remove_style_all(sp);
        lv_obj_set_width(sp, LV_PCT(100));
        lv_obj_set_height(sp, 0);
        lv_obj_set_flex_grow(sp, 1);
        lv_obj_clear_flag(sp, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        lbl(b, hub_model_room_name(i), p->t1, hub_font());
        lbl(b, on ? hub_tr("柔光开启", "Soft light on") : hub_tr("已入睡", "Asleep"), p->t3, hub_font());
    }

    /* ── Quick bar (dense) ───────────────────────────────────── */
    lv_obj_t *qb_wrap = lv_obj_create(root);
    lv_obj_remove_style_all(qb_wrap);
    lv_obj_set_size(qb_wrap, 440, 40);
    lv_obj_set_pos(qb_wrap, 20, 348);
    lv_obj_add_flag(qb_wrap, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(qb_wrap, LV_OBJ_FLAG_SCROLLABLE);
    quickbar(qb_wrap);

    /* ── Scene pills ─────────────────────────────────────────── */
    lv_obj_t *pills = lv_obj_create(root);
    lv_obj_remove_style_all(pills);
    lv_obj_set_size(pills, 440, 44);
    lv_obj_set_pos(pills, 20, 400);
    lv_obj_add_flag(pills, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_flex_flow(pills, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(pills, 10, 0);
    lv_obj_clear_flag(pills, LV_OBJ_FLAG_SCROLLABLE);

    const char *ids[] = { "home", "movie", "sleep" };
    for (int i = 0; i < 3; i++) {
        bool on = strcmp(m->active_scene, ids[i]) == 0;
        lv_obj_t *b = lv_btn_create(pills);
        lv_obj_remove_style_all(b);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, on ? p->accent : p->bg_card, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_shadow_width(b, 12, 0);
        lv_obj_set_style_shadow_opa(b, LV_OPA_20, 0);
        lv_obj_set_style_shadow_ofs_y(b, 4, 0);
        lv_obj_set_style_shadow_color(b, lv_color_make(90, 60, 80), 0);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_height(b, 40);
        lv_obj_add_event_cb(b, scene_cb, LV_EVENT_CLICKED, (void *)ids[i]);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, hub_model_scene_label(ids[i]));
        hub_style_label(l, on ? p->ink_on : p->t1, hub_font());
        lv_obj_center(l);
    }
}
