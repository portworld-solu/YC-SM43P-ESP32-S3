#include "hub_icons.h"
#include "hub_font.h"
#include "hub_theme.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

static const char *TAG = "hub_ico";

/** FE LIco name → spiffs_image/icons/nt/{name}.png */
static const char *s_names[HUB_ICO_COUNT] = {
    [HUB_ICO_HOME] = "home",
    [HUB_ICO_BULB] = "bulb",
    [HUB_ICO_CURTAIN] = "curtain",
    [HUB_ICO_SNOW] = "snow",
    [HUB_ICO_SHIELD] = "shield",
    [HUB_ICO_GAUGE] = "gauge",
    [HUB_ICO_LAYERS] = "layers",
    [HUB_ICO_WIFI] = "wifi",
    [HUB_ICO_WIFI_OFF] = "wifiOff",
    [HUB_ICO_COG] = "cog",
    [HUB_ICO_GRID] = "grid",
    [HUB_ICO_CLOCK] = "clock",
    [HUB_ICO_POWER] = "power",
    [HUB_ICO_MOON] = "moon",
    [HUB_ICO_MOON_SLEEP] = "moonSleep",
    [HUB_ICO_AWAY] = "away",
    [HUB_ICO_BUS] = "bus",
    [HUB_ICO_DROP] = "drop",
    [HUB_ICO_HEAT] = "heat",
    [HUB_ICO_PLUS] = "plus",
    [HUB_ICO_MINUS] = "minus",
    [HUB_ICO_UP] = "up",
    [HUB_ICO_DOWN] = "down",
    [HUB_ICO_STOP] = "stop",
    [HUB_ICO_BACK] = "back",
    [HUB_ICO_LEFT] = "left",
    [HUB_ICO_RIGHT] = "right",
    [HUB_ICO_CHEVRON] = "chevron",
    [HUB_ICO_LINK] = "link",
    [HUB_ICO_BRIGHT] = "bright",
    [HUB_ICO_INFO] = "info",
    [HUB_ICO_LOCK] = "lock",
    [HUB_ICO_PLUG] = "plug2",
    [HUB_ICO_FAN] = "fan",
    [HUB_ICO_SHUTTER] = "shutter",
    [HUB_ICO_LANG] = "lang",
    [HUB_ICO_ROOM_LIVING] = "roomLiving",
    [HUB_ICO_ROOM_BED] = "roomBed",
    [HUB_ICO_ROOM_KITCHEN] = "roomKitchen",
    [HUB_ICO_ROOM_STUDY] = "roomStudy",
    [HUB_ICO_RS485] = "rs485",
    [HUB_ICO_MQTT] = "mqtt",
};

/** Fallback symbols if PNG missing. */
static const char *s_sym[HUB_ICO_COUNT] = {
    [HUB_ICO_HOME] = LV_SYMBOL_HOME,
    [HUB_ICO_BULB] = LV_SYMBOL_CHARGE,
    [HUB_ICO_CURTAIN] = LV_SYMBOL_IMAGE,
    [HUB_ICO_SNOW] = LV_SYMBOL_REFRESH,
    [HUB_ICO_SHIELD] = LV_SYMBOL_OK,
    [HUB_ICO_GAUGE] = LV_SYMBOL_BATTERY_FULL,
    [HUB_ICO_LAYERS] = LV_SYMBOL_COPY,
    [HUB_ICO_WIFI] = LV_SYMBOL_WIFI,
    [HUB_ICO_WIFI_OFF] = LV_SYMBOL_CLOSE,
    [HUB_ICO_COG] = LV_SYMBOL_SETTINGS,
    [HUB_ICO_GRID] = LV_SYMBOL_LIST,
    [HUB_ICO_CLOCK] = LV_SYMBOL_DIRECTORY,
    [HUB_ICO_POWER] = LV_SYMBOL_POWER,
    [HUB_ICO_MOON] = LV_SYMBOL_DRIVE,
    [HUB_ICO_MOON_SLEEP] = LV_SYMBOL_DRIVE,
    [HUB_ICO_AWAY] = LV_SYMBOL_NEW_LINE,
    [HUB_ICO_BUS] = LV_SYMBOL_LIST,
    [HUB_ICO_DROP] = LV_SYMBOL_TINT,
    [HUB_ICO_HEAT] = LV_SYMBOL_WARNING,
    [HUB_ICO_PLUS] = LV_SYMBOL_PLUS,
    [HUB_ICO_MINUS] = LV_SYMBOL_MINUS,
    [HUB_ICO_UP] = LV_SYMBOL_UP,
    [HUB_ICO_DOWN] = LV_SYMBOL_DOWN,
    [HUB_ICO_STOP] = LV_SYMBOL_STOP,
    [HUB_ICO_BACK] = LV_SYMBOL_LEFT,
    [HUB_ICO_LEFT] = LV_SYMBOL_LEFT,
    [HUB_ICO_RIGHT] = LV_SYMBOL_RIGHT,
    [HUB_ICO_CHEVRON] = LV_SYMBOL_RIGHT,
    [HUB_ICO_LINK] = LV_SYMBOL_SHUFFLE,
    [HUB_ICO_BRIGHT] = LV_SYMBOL_EYE_OPEN,
    [HUB_ICO_INFO] = LV_SYMBOL_WARNING,
    [HUB_ICO_LOCK] = LV_SYMBOL_OK,
    [HUB_ICO_PLUG] = LV_SYMBOL_CHARGE,
    [HUB_ICO_FAN] = LV_SYMBOL_REFRESH,
    [HUB_ICO_SHUTTER] = LV_SYMBOL_IMAGE,
    [HUB_ICO_LANG] = LV_SYMBOL_EDIT,
    [HUB_ICO_ROOM_LIVING] = LV_SYMBOL_HOME,
    [HUB_ICO_ROOM_BED] = LV_SYMBOL_HOME,
    [HUB_ICO_ROOM_KITCHEN] = LV_SYMBOL_HOME,
    [HUB_ICO_ROOM_STUDY] = LV_SYMBOL_DIRECTORY,
    [HUB_ICO_RS485] = LV_SYMBOL_LIST,
    [HUB_ICO_MQTT] = LV_SYMBOL_SHUFFLE,
};

typedef struct {
    lv_img_dsc_t dsc;
    bool ready;
} hub_ico_cache_t;

static hub_ico_cache_t s_cache[HUB_ICO_COUNT];
static bool s_cache_inited;

const char *hub_ico_name(hub_ico_t ico)
{
    if ((int)ico < 0 || ico >= HUB_ICO_COUNT) {
        return NULL;
    }
    return s_names[ico];
}

const char *hub_ico_sym(hub_ico_t ico)
{
    if ((int)ico < 0 || ico >= HUB_ICO_COUNT) {
        return LV_SYMBOL_DUMMY;
    }
    return s_sym[ico];
}

hub_ico_t hub_ico_for_room(int room_idx)
{
    switch (room_idx) {
    case 0:
        return HUB_ICO_ROOM_LIVING;
    case 1:
        return HUB_ICO_ROOM_BED;
    case 2:
        return HUB_ICO_ROOM_KITCHEN;
    case 3:
        return HUB_ICO_ROOM_STUDY;
    default:
        return HUB_ICO_HOME;
    }
}

hub_ico_t hub_ico_for_proto(const char *proto_id)
{
    if (!proto_id) {
        return HUB_ICO_BUS;
    }
    if (strcmp(proto_id, "rs485") == 0) {
        return HUB_ICO_RS485;
    }
    if (strcmp(proto_id, "mqtt") == 0) {
        return HUB_ICO_MQTT;
    }
    if (strcmp(proto_id, "wifi") == 0) {
        return HUB_ICO_WIFI;
    }
    return HUB_ICO_BUS;
}

lv_obj_t *hub_ico_label(lv_obj_t *parent, hub_ico_t ico, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, hub_ico_sym(ico));
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    return l;
}

static void hub_ico_path(const char *name, char *buf, size_t len)
{
    snprintf(buf, len, "S:/icons/nt/%s.png", name ? name : "home");
}

static bool hub_ico_cache_one(hub_ico_t ico)
{
    const char *name = hub_ico_name(ico);
    if (!name) {
        return false;
    }
    char path[48];
    hub_ico_path(name, path, sizeof(path));

    lv_img_decoder_dsc_t dec;
    lv_memset_00(&dec, sizeof(dec));
    if (lv_img_decoder_open(&dec, path, lv_color_white(), 0) != LV_RES_OK || !dec.img_data) {
        return false;
    }

    uint32_t px = (uint32_t)dec.header.w * (uint32_t)dec.header.h;
    uint32_t bytes = px * LV_IMG_PX_SIZE_ALPHA_BYTE;
    uint8_t *pix = (uint8_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pix) {
        pix = (uint8_t *)heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    }
    if (!pix) {
        lv_img_decoder_close(&dec);
        return false;
    }
    memcpy(pix, dec.img_data, bytes);

    s_cache[ico].dsc.header.always_zero = 0;
    s_cache[ico].dsc.header.w = dec.header.w;
    s_cache[ico].dsc.header.h = dec.header.h;
    s_cache[ico].dsc.header.cf = dec.header.cf ? dec.header.cf : LV_IMG_CF_TRUE_COLOR_ALPHA;
    s_cache[ico].dsc.data_size = bytes;
    s_cache[ico].dsc.data = pix;
    s_cache[ico].ready = true;

    lv_img_decoder_close(&dec);
    return true;
}

void hub_ico_cache_init(void)
{
    if (s_cache_inited) {
        return;
    }
    s_cache_inited = true;

    int64_t t0 = esp_timer_get_time();
    int ok = 0;
    size_t bytes = 0;
    for (int i = 0; i < (int)HUB_ICO_COUNT; i++) {
        if (hub_ico_cache_one((hub_ico_t)i)) {
            ok++;
            bytes += s_cache[i].dsc.data_size;
        }
    }
    ESP_LOGI(TAG, "PSRAM icon cache %d/%d icons, %u KB, %lld ms",
             ok, (int)HUB_ICO_COUNT, (unsigned)(bytes / 1024),
             (long long)((esp_timer_get_time() - t0) / 1000));
}

static uint16_t hub_ico_zoom_px(lv_coord_t px)
{
    if (px < 8) {
        px = 8;
    }
    return (uint16_t)((((uint32_t)px * 7U / 5U) * 256U + 48U) / 96U);
}

lv_obj_t *hub_ico_add(lv_obj_t *parent, hub_ico_t ico, lv_color_t color, lv_coord_t size)
{
    if (size < 12) {
        size = 12;
    }
    if (!s_cache_inited) {
        hub_ico_cache_init();
    }

    lv_obj_t *wrap = lv_obj_create(parent);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_size(wrap, size, size);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(wrap, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    lv_obj_t *img = lv_img_create(wrap);
    if ((int)ico >= 0 && ico < HUB_ICO_COUNT && s_cache[ico].ready) {
        lv_img_set_src(img, &s_cache[ico].dsc);
        lv_obj_set_style_img_recolor(img, color, LV_PART_MAIN);
        lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, LV_PART_MAIN);
        lv_img_set_zoom(img, hub_ico_zoom_px(size));
        lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_center(img);
        return wrap;
    }

    /* SPIFFS-only: no LV_SYMBOL / built-in glyph fallback — keep empty wrap. */
    lv_obj_del(img);
    (void)color;
    return wrap;
}

void hub_ico_rebind(lv_obj_t *ico_obj, hub_ico_t ico, lv_color_t color, lv_coord_t size)
{
    if (!ico_obj) {
        return;
    }
    if (size < 12) {
        size = 12;
    }
    if (!s_cache_inited) {
        hub_ico_cache_init();
    }

    lv_obj_t *img = lv_obj_get_child(ico_obj, 0);
    if (img && lv_obj_check_type(img, &lv_img_class)) {
        lv_obj_set_size(ico_obj, size, size);
        if ((int)ico >= 0 && ico < HUB_ICO_COUNT && s_cache[ico].ready) {
            lv_img_set_src(img, &s_cache[ico].dsc);
            lv_obj_set_style_img_recolor(img, color, LV_PART_MAIN);
            lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, LV_PART_MAIN);
            lv_img_set_zoom(img, hub_ico_zoom_px(size));
            lv_obj_center(img);
        }
        return;
    }

    if (lv_obj_check_type(ico_obj, &lv_label_class)) {
        lv_label_set_text(ico_obj, hub_ico_sym(ico));
        lv_obj_set_style_text_color(ico_obj, color, 0);
    }
}

lv_obj_t *hub_ico_badge(lv_obj_t *parent, hub_ico_t ico, lv_color_t color, lv_coord_t badge_sz,
                        lv_coord_t ico_sz)
{
    const hub_palette_t *p = hub_palette();
    bool zen = p->radius == 0;
    if (badge_sz < 24) {
        badge_sz = 24;
    }
    if (ico_sz < 12) {
        ico_sz = (badge_sz * 50) / 100;
        if (ico_sz < 12) {
            ico_sz = 12;
        }
    }
    lv_obj_t *badge = lv_obj_create(parent);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, badge_sz, badge_sz);
    lv_obj_set_style_radius(badge, zen ? 0 : (badge_sz / 4), 0);
    lv_obj_set_style_bg_color(badge, color, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_20, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(badge, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    lv_obj_t *ic = hub_ico_add(badge, ico, color, ico_sz);
    lv_obj_center(ic);
    return badge;
}

static void clock_del_cb(lv_event_t *e)
{
    lv_timer_t *tmr = (lv_timer_t *)lv_event_get_user_data(e);
    if (tmr) {
        lv_timer_del(tmr);
    }
}

static void clock_timer_cb(lv_timer_t *t)
{
    lv_obj_t *lab = (lv_obj_t *)t->user_data;
    if (!lab) {
        return;
    }
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    if (!tmv) {
        return;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", tmv->tm_hour, tmv->tm_min);
    if (strcmp(lv_label_get_text(lab), buf) != 0) {
        lv_label_set_text(lab, buf);
    }
}

lv_obj_t *hub_clock_label(lv_obj_t *parent, lv_color_t color, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(parent);
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    char buf[8] = "00:00";
    if (tmv) {
        snprintf(buf, sizeof(buf), "%02d:%02d", tmv->tm_hour, tmv->tm_min);
    }
    lv_label_set_text(l, buf);
    hub_style_label(l, color, font ? font : hub_font());
    lv_timer_t *tmr = lv_timer_create(clock_timer_cb, 1000, l);
    lv_obj_add_event_cb(l, clock_del_cb, LV_EVENT_DELETE, tmr);
    return l;
}

void hub_theme_wash(lv_obj_t *parent, lv_color_t accent, lv_opa_t opa)
{
    lv_obj_t *wash = lv_obj_create(parent);
    lv_obj_remove_style_all(wash);
    lv_obj_set_size(wash, 300, 300);
    lv_obj_set_style_radius(wash, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(wash, accent, 0);
    lv_obj_set_style_bg_opa(wash, opa, 0);
    lv_obj_add_flag(wash, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(wash, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(wash, LV_ALIGN_TOP_RIGHT, 48, -90);
    lv_obj_move_background(wash);

    lv_obj_t *wash2 = lv_obj_create(parent);
    lv_obj_remove_style_all(wash2);
    lv_obj_set_size(wash2, 220, 220);
    lv_obj_set_style_radius(wash2, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(wash2, accent, 0);
    lv_obj_set_style_bg_opa(wash2, opa > 12 ? (lv_opa_t)(opa - 8) : opa, 0);
    lv_obj_add_flag(wash2, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(wash2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(wash2, LV_ALIGN_BOTTOM_LEFT, -70, 60);
    lv_obj_move_background(wash2);
}

void hub_hud_bevel(lv_obj_t *card, lv_color_t tick_color)
{
    if (!card) {
        return;
    }
    lv_obj_set_style_radius(card, 2, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, tick_color, 0);
}

lv_obj_t *hub_energy_ring(lv_obj_t *parent, lv_color_t accent, lv_color_t cool)
{
    lv_obj_t *ring = lv_obj_create(parent);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 148, 148);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 10, 0);
    lv_obj_set_style_border_color(ring, accent, 0);
    lv_obj_set_style_border_opa(ring, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *inner = lv_obj_create(ring);
    lv_obj_remove_style_all(inner);
    lv_obj_set_size(inner, 110, 110);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(inner, 6, 0);
    lv_obj_set_style_border_color(inner, cool, 0);
    lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, 0);
    lv_obj_center(inner);
    lv_obj_clear_flag(inner, LV_OBJ_FLAG_SCROLLABLE);
    return inner;
}
