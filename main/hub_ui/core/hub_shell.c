#include "hub_ui.h"
#include "hub_theme.h"
#include "hub_font.h"
#include "hub_icons.h"

#include <string.h>

typedef struct {
    lv_obj_t *shell;
    lv_obj_t *body;
    lv_obj_t *dots;
} hub_shell_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    hub_ui_back();
}

lv_obj_t *hub_shell_create(lv_obj_t *parent, const char *title, bool show_back)
{
    const hub_palette_t *p = hub_palette();
    bool zen = p->name && strcmp(p->name, "zen") == 0;
    lv_obj_t *shell = lv_obj_create(parent);
    lv_obj_remove_style_all(shell);
    lv_obj_set_size(shell, 480, 480);
    lv_obj_set_flex_flow(shell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(shell, p->bg_deep, 0);
    lv_obj_set_style_bg_opa(shell, LV_OPA_COVER, 0);
    lv_obj_clear_flag(shell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(shell, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *head = lv_obj_create(shell);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, 480, zen ? 56 : 60); /* +4 for larger type */
    lv_obj_set_style_pad_hor(head, 14, 0);
    lv_obj_set_style_border_width(head, 0, 0);
    lv_obj_set_style_border_side(head, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(head, 1, 0);
    lv_obj_set_style_border_color(head, p->line, 0);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(head, LV_OBJ_FLAG_GESTURE_BUBBLE);

    if (show_back) {
        lv_obj_t *btn = lv_btn_create(head);
        lv_obj_set_size(btn, 40, 40);
        hub_apply_card_themed(btn, false);
        lv_obj_set_style_pad_all(btn, 0, 0);
        if (zen) {
            lv_obj_set_style_radius(btn, 0, 0);
        }
        lv_obj_add_event_cb(btn, back_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_t *bl = lv_label_create(btn);
        lv_label_set_text(bl, LV_SYMBOL_LEFT);
        hub_style_label(bl, p->t2, hub_font_symbol());
        lv_obj_center(bl);
    } else {
        lv_obj_t *clk = hub_clock_label(head, p->t1, hub_font_clock());
        (void)clk;
    }

    lv_obj_t *ttl = lv_label_create(head);
    lv_label_set_text(ttl, title ? title : "");
    hub_style_label(ttl, p->t1, hub_font());

    lv_obj_t *wx = lv_label_create(head);
    lv_label_set_text(wx, p->name ? p->name : "");
    hub_style_label(wx, p->t3, hub_font());

    lv_obj_t *body = lv_obj_create(shell);
    lv_obj_remove_style_all(body);
    lv_obj_set_width(body, 480);
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_all(body, 14, 0);
    lv_obj_set_style_pad_row(body, 12, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *dots = lv_obj_create(shell);
    lv_obj_remove_style_all(dots);
    lv_obj_set_size(dots, 480, 24);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 6, 0);
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dots, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_set_user_data(shell, body);
    lv_obj_set_user_data(body, dots);

    return shell;
}

lv_obj_t *hub_shell_body(lv_obj_t *shell)
{
    return (lv_obj_t *)lv_obj_get_user_data(shell);
}

void hub_shell_set_dots(lv_obj_t *shell, int count, int active)
{
    const hub_palette_t *p = hub_palette();
    lv_obj_t *body = hub_shell_body(shell);
    if (!body) {
        return;
    }
    lv_obj_t *dots = (lv_obj_t *)lv_obj_get_user_data(body);
    if (!dots) {
        return;
    }
    lv_obj_clean(dots);
    if (count < 1) {
        count = 1;
    }
    bool zen = p->name && strcmp(p->name, "zen") == 0;
    for (int i = 0; i < count; i++) {
        lv_obj_t *d = lv_obj_create(dots);
        lv_obj_remove_style_all(d);
        if (zen) {
            /* FE zen .lv-dot: 16×1 hairline, on → 28×1 t1 */
            lv_obj_set_size(d, (i == active) ? 28 : 16, 1);
            lv_obj_set_style_radius(d, 0, 0);
            lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(d, (i == active) ? p->t1 : p->line, 0);
        } else {
            lv_obj_set_size(d, (i == active) ? 18 : 8, 3);
            lv_obj_set_style_radius(d, 1, 0);
            lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(d, (i == active) ? p->accent : p->line, 0);
        }
    }
}
