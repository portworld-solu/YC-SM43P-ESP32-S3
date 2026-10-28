#include "hub_device_ui.h"

#include "hub_ui.h"
#include "hub_theme.h"
#include "hub_font.h"
#include "hub_model.h"
#include "hub_icons.h"

#include "wifi_management.h"
#include "withthewind_board_lvgl_init.h"
#include "board_ethernet_ch390.h"
#include "hub_i18n.h"

#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define HUB_WIFI_SCAN_MAX 16
#define HUB_BRIGHT_NS "hub_ui"
#define HUB_BRIGHT_KEY "bright"

static int s_bright = 100;
static bool s_bright_loaded;
static lv_obj_t *s_dd;
static lv_obj_t *s_pwd;
static lv_obj_t *s_kb;
static lv_obj_t *s_net_body;
static lv_obj_t *s_kb_pad;
static char s_ssid_opts[HUB_WIFI_SCAN_MAX * 36];
static char s_ssids[HUB_WIFI_SCAN_MAX][33];
static uint16_t s_ssid_n;
static int16_t s_ssid_sel = -1;
static lv_timer_t *s_wifi_poll;
static int s_wifi_poll_ticks;
static char s_pwd_cache[65];
static bool s_pwd_visible;
static lv_obj_t *s_pwd_eye;

static void style_card(lv_obj_t *obj, bool active)
{
    hub_apply_card(obj, active);
    theme_card_chrome(obj, active);
}

static lv_obj_t *mk_lbl(lv_obj_t *par, const char *txt, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(par);
    lv_label_set_text(l, txt ? txt : "");
    hub_style_label(l, c, hub_font());
    return l;
}

void hub_wifi_sync_model(void)
{
    hub_model_t *m = hub_model();
    bool ok = wifi_management_is_connected();
    m->protos[3].ok = ok;
    m->protos[3].health = ok ? 95 : (wifi_management_start_failed() ? 0 : 40);
}

static void load_brightness(void)
{
    if (s_bright_loaded) {
        return;
    }
    s_bright_loaded = true;
    s_bright = 100;
    nvs_handle_t h;
    if (nvs_open(HUB_BRIGHT_NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t v = 100;
        if (nvs_get_u8(h, HUB_BRIGHT_KEY, &v) == ESP_OK) {
            s_bright = (int)v;
        }
        nvs_close(h);
    }
    s_bright = board_backlight_clamp_percent(s_bright);
}

static void save_brightness(int pct)
{
    nvs_handle_t h;
    if (nvs_open(HUB_BRIGHT_NS, NVS_READWRITE, &h) == ESP_OK) {
        (void)nvs_set_u8(h, HUB_BRIGHT_KEY, (uint8_t)pct);
        (void)nvs_commit(h);
        nvs_close(h);
    }
}

static void apply_brightness(int pct, bool persist)
{
    pct = board_backlight_clamp_percent(pct);
    s_bright = pct;
    int eff = pct;
    hub_model_t *m = hub_model();
    if (m && m->settings.night_mode) {
        eff = (pct * 70) / 100;
        eff = board_backlight_clamp_percent(eff);
    }
    int band = board_backlight_quantize_percent(eff);
    (void)board_backlight_set(band);
    if (persist) {
        save_brightness(pct);
        hub_model_toast(hub_tr("亮度已保存", "Brightness saved"));
    }
}

int hub_device_brightness_get(void)
{
    load_brightness();
    return s_bright;
}

void hub_device_brightness_apply_effective(void)
{
    load_brightness();
    apply_brightness(s_bright, false);
}

static void bright_changed_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    int val = (int)lv_slider_get_value(sl);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        apply_brightness(val, false);
        return;
    }
    if (code == LV_EVENT_RELEASED) {
        apply_brightness(val, true);
        hub_ui_refresh();
    }
}

void hub_build_brightness_block(lv_obj_t *body)
{
    const hub_palette_t *p = hub_palette();
    bool zen = p->name && strcmp(p->name, "zen") == 0;
    load_brightness();

    /* FE SettingRow: icon + title/sub left, LinearTrack ~120px right */
    lv_obj_t *row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 64);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    if (zen) {
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, p->line, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    } else {
        style_card(row, false);
    }

    hub_ico_badge(row, HUB_ICO_BRIGHT, p->accent, 36, 18);
    lv_obj_align(lv_obj_get_child(row, 0), LV_ALIGN_LEFT_MID, 4, 0);

    mk_lbl(row, hub_model()->settings.lang_zh ? "屏幕亮度" : "Brightness", p->t1);
    lv_obj_align(lv_obj_get_child(row, 1), LV_ALIGN_LEFT_MID, 48, -8);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", s_bright);
    mk_lbl(row, buf, p->t3);
    lv_obj_align(lv_obj_get_child(row, 2), LV_ALIGN_LEFT_MID, 48, 10);

    lv_obj_t *sl = lv_slider_create(row);
    lv_obj_set_size(sl, 140, 10);
    lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -8, 0);
    int lo = board_backlight_clamp_percent(1);
    lv_slider_set_range(sl, lo < 100 ? lo : 1, 100);
    lv_slider_set_value(sl, s_bright, LV_ANIM_OFF);
    if (zen) {
        lv_obj_set_style_radius(sl, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(sl, 0, LV_PART_INDICATOR);
        lv_obj_set_style_radius(sl, 0, LV_PART_KNOB);
        lv_obj_set_style_bg_color(sl, lv_color_mix(p->t1, p->bg_deep, LV_OPA_10), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(sl, p->t1, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(sl, p->t1, LV_PART_KNOB);
        lv_obj_set_style_pad_ver(sl, 5, LV_PART_KNOB);
        lv_obj_set_style_pad_hor(sl, 0, LV_PART_KNOB);
        lv_obj_set_style_width(sl, 3, LV_PART_KNOB);
        lv_obj_set_style_shadow_width(sl, 0, LV_PART_KNOB);
    } else {
        lv_obj_set_style_radius(sl, 2, LV_PART_MAIN);
        lv_obj_set_style_radius(sl, 2, LV_PART_INDICATOR);
        lv_obj_set_style_radius(sl, 2, LV_PART_KNOB);
        lv_obj_set_style_bg_color(sl, p->bg_elev, LV_PART_MAIN);
        lv_obj_set_style_bg_color(sl, p->accent, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(sl, p->bg_card, LV_PART_KNOB);
        lv_obj_set_style_pad_all(sl, 4, LV_PART_KNOB);
    }
    lv_obj_clear_flag(sl, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(sl, bright_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, bright_changed_cb, LV_EVENT_RELEASED, NULL);
}

static void fill_ip(char *ip, size_t n)
{
    ip[0] = '\0';
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEFAULT");
    }
    if (!netif) {
        return;
    }
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(netif, &info) == ESP_OK) {
        snprintf(ip, n, IPSTR, IP2STR(&info.ip));
    }
}

static void fill_ssid(char *ssid, size_t n)
{
    ssid[0] = '\0';
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        snprintf(ssid, n, "%s", (const char *)ap.ssid);
    }
}

static void rebuild_dropdown(void)
{
    if (!s_dd) {
        return;
    }
    s_ssid_opts[0] = '\0';
    for (uint16_t i = 0; i < s_ssid_n; i++) {
        if (i) {
            strncat(s_ssid_opts, "\n", sizeof(s_ssid_opts) - strlen(s_ssid_opts) - 1);
        }
        strncat(s_ssid_opts, s_ssids[i], sizeof(s_ssid_opts) - strlen(s_ssid_opts) - 1);
    }
    if (s_ssid_n == 0) {
        lv_dropdown_set_options(s_dd, hub_tr("无热点 · 先扫描", "Empty · scan first"));
        s_ssid_sel = -1;
    } else {
        lv_dropdown_set_options(s_dd, s_ssid_opts);
        if (s_ssid_sel < 0 || s_ssid_sel >= (int16_t)s_ssid_n) {
            s_ssid_sel = 0;
        }
        lv_dropdown_set_selected(s_dd, (uint16_t)s_ssid_sel);
    }
}

static void dd_changed_cb(lv_event_t *e)
{
    (void)e;
    if (!s_dd || s_ssid_n == 0) {
        return;
    }
    s_ssid_sel = (int16_t)lv_dropdown_get_selected(s_dd);
}

static void kb_set_visible(bool show)
{
    if (!s_kb) {
        return;
    }
    if (show) {
        lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        if (s_kb_pad) {
            lv_obj_set_height(s_kb_pad, 188);
        }
        if (s_pwd && s_net_body) {
            lv_obj_update_layout(s_net_body);
            lv_obj_scroll_to_view(s_pwd, LV_ANIM_ON);
        }
    } else {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        if (s_kb_pad) {
            lv_obj_set_height(s_kb_pad, 0);
        }
    }
}

static void kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        kb_set_visible(false);
        if (s_pwd) {
            lv_obj_clear_state(s_pwd, LV_STATE_FOCUSED);
        }
    }
}

static void wifi_poll_stop(void)
{
    if (s_wifi_poll) {
        lv_timer_del(s_wifi_poll);
        s_wifi_poll = NULL;
    }
    s_wifi_poll_ticks = 0;
}

static void wifi_poll_cb(lv_timer_t *t)
{
    (void)t;
    s_wifi_poll_ticks++;
    bool connected = wifi_management_is_connected();
    if (connected) {
        wifi_poll_stop();
        hub_wifi_sync_model();
        hub_model_toast(hub_tr("Wi-Fi 已连接", "Wi-Fi connected"));
        hub_ui_refresh();
        return;
    }
    /* ~20 × 500ms = 10s */
    if (s_wifi_poll_ticks >= 20) {
        wifi_poll_stop();
        hub_wifi_sync_model();
        hub_model_toast(hub_tr("连接超时，请重试", "Connect timeout"));
        hub_ui_refresh();
    }
}

static void net_page_del_cb(lv_event_t *e)
{
    (void)e;
    wifi_poll_stop();
    s_dd = NULL;
    s_pwd = NULL;
    s_pwd_eye = NULL;
    s_kb = NULL;
    s_net_body = NULL;
    s_kb_pad = NULL;
}

static void pwd_changed_cb(lv_event_t *e)
{
    (void)e;
    if (!s_pwd) {
        return;
    }
    const char *t = lv_textarea_get_text(s_pwd);
    snprintf(s_pwd_cache, sizeof(s_pwd_cache), "%s", t ? t : "");
}

static void pwd_eye_cb(lv_event_t *e)
{
    (void)e;
    s_pwd_visible = !s_pwd_visible;
    if (s_pwd) {
        lv_textarea_set_password_mode(s_pwd, !s_pwd_visible);
    }
    if (s_pwd_eye) {
        lv_obj_t *lab = lv_obj_get_child(s_pwd_eye, 0);
        if (lab) {
            lv_label_set_text(lab, s_pwd_visible ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
        }
    }
}

static void scan_cb(lv_event_t *e)
{
    (void)e;
    wifi_ap_record_t recs[HUB_WIFI_SCAN_MAX];
    uint16_t n = HUB_WIFI_SCAN_MAX;
    hub_model_toast(hub_tr("正在扫描...", "Scanning..."));
    esp_err_t err = wifi_management_scan_blocking(recs, &n);
    s_ssid_n = 0;
    if (err == ESP_OK) {
        for (uint16_t i = 0; i < n && s_ssid_n < HUB_WIFI_SCAN_MAX; i++) {
            if (recs[i].ssid[0] == '\0') {
                continue;
            }
            snprintf(s_ssids[s_ssid_n], sizeof(s_ssids[0]), "%s", (const char *)recs[i].ssid);
            s_ssid_n++;
        }
        hub_model_toast(s_ssid_n ? hub_tr("扫描完成", "Scan done") : hub_tr("未发现热点", "No APs found"));
    } else {
        hub_model_toast(hub_tr("扫描失败", "Scan failed"));
    }
    hub_ui_refresh();
}

static void connect_cb(lv_event_t *e)
{
    (void)e;
    if (s_dd && s_ssid_n > 0) {
        s_ssid_sel = (int16_t)lv_dropdown_get_selected(s_dd);
    }
    if (s_ssid_n == 0 || s_ssid_sel < 0 || s_ssid_sel >= (int16_t)s_ssid_n) {
        hub_model_toast(hub_tr("请先扫描并选择热点", "Scan and select AP"));
        return;
    }
    if (s_pwd) {
        const char *t = lv_textarea_get_text(s_pwd);
        snprintf(s_pwd_cache, sizeof(s_pwd_cache), "%s", t ? t : "");
    }
    const char *pwd = s_pwd_cache;
    kb_set_visible(false);
    hub_model_toast(hub_tr("正在连接...", "Connecting..."));
    esp_err_t err = wifi_management_connect(s_ssids[s_ssid_sel], pwd ? pwd : "");
    if (err != ESP_OK) {
        hub_model_toast(hub_tr("连接请求失败", "Connect failed"));
        hub_wifi_sync_model();
        hub_ui_refresh();
        return;
    }
    wifi_poll_stop();
    s_wifi_poll_ticks = 0;
    s_wifi_poll = lv_timer_create(wifi_poll_cb, 500, NULL);
    /* Keep page; status row updates on poll success via refresh */
}

static void disconnect_cb(lv_event_t *e)
{
    (void)e;
    wifi_poll_stop();
    wifi_management_disconnect_user();
    hub_wifi_sync_model();
    hub_model_toast(hub_tr("已断开 Wi-Fi", "Wi-Fi disconnected"));
    hub_ui_refresh();
}

static void pwd_focus_cb(lv_event_t *e)
{
    if (!s_kb || !s_pwd) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        lv_keyboard_set_textarea(s_kb, s_pwd);
        kb_set_visible(true);
    } else if (code == LV_EVENT_DEFOCUSED) {
        /* Keep keyboard until ✓ / Cancel — do not hide on accidental defocus */
    }
}

static void wifi_toggle_cb(lv_event_t *e)
{
    (void)e;
    if (wifi_management_is_connected() || wifi_management_is_started()) {
        wifi_management_disconnect_user();
        hub_model_toast(hub_tr("Wi-Fi 已关闭", "Wi-Fi off"));
    } else {
        esp_err_t err = wifi_management_start();
        hub_model_toast(err == ESP_OK ? hub_tr("Wi-Fi 已开启", "Wi-Fi on")
                                      : hub_tr("Wi-Fi 启动失败", "Wi-Fi start failed"));
    }
    hub_wifi_sync_model();
    hub_ui_refresh();
}

static void eth_try_cb(lv_event_t *e)
{
    (void)e;
#if !(defined(CONFIG_BOARD_ETH_CH390_ENABLE) && CONFIG_BOARD_ETH_CH390_ENABLE)
    hub_model_toast(hub_tr("以太网未编译进固件", "Ethernet not in build"));
    hub_ui_refresh();
    return;
#else
    if (!board_ethernet_ch390_is_ready()) {
        esp_err_t init_err = board_ethernet_ch390_init();
        if (init_err != ESP_OK || !board_ethernet_ch390_is_ready()) {
            hub_model_toast(hub_tr("以太网初始化失败", "Ethernet init failed"));
            hub_ui_refresh();
            return;
        }
    }
    if (board_ethernet_ch390_is_started()) {
        board_ethernet_ch390_set_traffic_paused(true);
        hub_model_toast(hub_tr("以太网已暂停", "Ethernet paused"));
    } else {
        esp_err_t err = board_ethernet_ch390_try_start();
        if (err == ESP_OK) {
            if (board_ethernet_ch390_link_up()) {
                hub_model_toast(hub_tr("以太网已连接", "Ethernet linked"));
            } else {
                hub_model_toast(hub_tr("以太网已开启 · 等待网线", "Ethernet on · wait cable"));
            }
        } else if (err == ESP_ERR_NO_MEM) {
            hub_model_toast(hub_tr("内存不足，稍后再试", "Low memory, retry later"));
        } else {
            hub_model_toast(hub_tr("以太网启动失败", "Ethernet failed"));
        }
    }
    hub_ui_refresh();
#endif
}

static lv_obj_t *mk_switch_row(lv_obj_t *par, bool on, lv_event_cb_t cb)
{
    const hub_palette_t *p = hub_palette();
    bool zen = p->name && strcmp(p->name, "zen") == 0;
    lv_obj_t *sw = lv_btn_create(par);
    lv_obj_remove_style_all(sw);
    lv_obj_set_size(sw, 44, 24);
    lv_obj_set_style_radius(sw, zen ? 0 : 12, 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sw, on ? p->accent : lv_color_mix(p->t1, p->bg_deep, LV_OPA_20), 0);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *kn = lv_obj_create(sw);
    lv_obj_remove_style_all(kn);
    lv_obj_set_size(kn, 18, 18);
    lv_obj_set_style_radius(kn, zen ? 0 : LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(kn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(kn, p->ink_on, 0);
    lv_obj_align(kn, on ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, on ? -3 : 3, 0);
    lv_obj_clear_flag(kn, LV_OBJ_FLAG_CLICKABLE);
    return sw;
}

static void go_gateway_cb(lv_event_t *e)
{
    (void)e;
    hub_ui_go(HUB_ROUTE_GATEWAY);
}

void hub_build_network(lv_obj_t *parent)
{
    const hub_palette_t *p = hub_palette();
    bool zen = p->name && strcmp(p->name, "zen") == 0;
    bool zh = hub_model()->settings.lang_zh;
    hub_wifi_sync_model();
    s_dd = NULL;
    s_pwd = NULL;
    s_kb = NULL;
    s_net_body = NULL;
    s_kb_pad = NULL;
    if (s_ssid_sel >= (int16_t)s_ssid_n) {
        s_ssid_sel = s_ssid_n > 0 ? 0 : -1;
    }

    bool wifi_on = wifi_management_is_started() || wifi_management_is_connected();
    bool connected = wifi_management_is_connected();
    char ssid[36] = {0};
    char wifi_ip[24] = {0};
    if (connected) {
        fill_ssid(ssid, sizeof(ssid));
        fill_ip(wifi_ip, sizeof(wifi_ip));
    }

    char eth_ip[24] = {0};
    bool eth_link = board_ethernet_ch390_link_up();
    bool eth_ip_ok = board_ethernet_ch390_get_ip(eth_ip, sizeof(eth_ip));
    bool eth_ready = board_ethernet_ch390_is_ready();

    lv_obj_t *shell = hub_shell_create(parent, zh ? "网络" : "Network", true);
    lv_obj_t *body = hub_shell_body(shell);
    s_net_body = body;
    lv_obj_add_event_cb(shell, net_page_del_cb, LV_EVENT_DELETE, NULL);

    /* Wi-Fi row — FE SettingRow */
    lv_obj_t *wrow = lv_obj_create(body);
    lv_obj_remove_style_all(wrow);
    lv_obj_set_width(wrow, LV_PCT(100));
    lv_obj_set_height(wrow, 56);
    if (zen) {
        lv_obj_set_style_radius(wrow, 0, 0);
        lv_obj_set_style_border_side(wrow, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(wrow, 1, 0);
        lv_obj_set_style_border_color(wrow, p->line, 0);
    } else {
        style_card(wrow, wifi_on);
    }
    hub_ico_badge(wrow, connected ? HUB_ICO_WIFI : HUB_ICO_WIFI_OFF, p->accent, 36, 18);
    lv_obj_align(lv_obj_get_child(wrow, 0), LV_ALIGN_LEFT_MID, 4, 0);
    mk_lbl(wrow, "Wi-Fi", p->t1);
    lv_obj_align(lv_obj_get_child(wrow, 1), LV_ALIGN_LEFT_MID, 48, -8);
    char wsub[64];
    if (!wifi_on) {
        snprintf(wsub, sizeof(wsub), "%s", zh ? "已关闭" : "Off");
    } else if (connected && ssid[0]) {
        snprintf(wsub, sizeof(wsub), "%s · %s", ssid, wifi_ip[0] ? wifi_ip : "--");
    } else if (wifi_management_start_failed()) {
        snprintf(wsub, sizeof(wsub), "%s", zh ? "启动失败" : "Start failed");
    } else {
        snprintf(wsub, sizeof(wsub), "%s", zh ? "未连接" : "Not connected");
    }
    mk_lbl(wrow, wsub, p->t3);
    lv_obj_align(lv_obj_get_child(wrow, 2), LV_ALIGN_LEFT_MID, 48, 10);
    mk_switch_row(wrow, wifi_on, wifi_toggle_cb);
    lv_obj_align(lv_obj_get_child(wrow, -1), LV_ALIGN_RIGHT_MID, -4, 0);

    /* Ethernet row */
    lv_obj_t *erow = lv_obj_create(body);
    lv_obj_remove_style_all(erow);
    lv_obj_set_width(erow, LV_PCT(100));
    lv_obj_set_height(erow, 56);
    if (zen) {
        lv_obj_set_style_radius(erow, 0, 0);
        lv_obj_set_style_border_side(erow, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(erow, 1, 0);
        lv_obj_set_style_border_color(erow, p->line, 0);
    } else {
        style_card(erow, eth_link);
    }
    hub_ico_badge(erow, HUB_ICO_LINK, p->accent, 36, 18);
    lv_obj_align(lv_obj_get_child(erow, 0), LV_ALIGN_LEFT_MID, 4, 0);
    mk_lbl(erow, zh ? "以太网" : "Ethernet", p->t1);
    lv_obj_align(lv_obj_get_child(erow, 1), LV_ALIGN_LEFT_MID, 48, -8);
    char esub[72];
    if (!eth_ready) {
        snprintf(esub, sizeof(esub), "%s", zh ? "硬件未启用" : "Not available");
    } else if (eth_ip_ok) {
        snprintf(esub, sizeof(esub), "%s · %s", zh ? "已连接" : "Linked", eth_ip);
    } else if (eth_link) {
        snprintf(esub, sizeof(esub), "%s", zh ? "已连接 · 获取 IP…" : "Link up · DHCP…");
    } else if (board_ethernet_ch390_is_started()) {
        snprintf(esub, sizeof(esub), "%s", zh ? "未插入网线" : "No cable");
    } else {
        snprintf(esub, sizeof(esub), "%s", zh ? "已停止" : "Stopped");
    }
    mk_lbl(erow, esub, p->t3);
    lv_obj_align(lv_obj_get_child(erow, 2), LV_ALIGN_LEFT_MID, 48, 10);
    mk_switch_row(erow, eth_link || board_ethernet_ch390_is_started(), eth_try_cb);
    lv_obj_align(lv_obj_get_child(erow, -1), LV_ALIGN_RIGHT_MID, -4, 0);

    mk_lbl(body, zh ? "附近热点 · 下拉选择后输入密码" : "Nearby APs · dropdown + password", p->t3);

    lv_obj_t *actions = lv_obj_create(body);
    lv_obj_remove_style_all(actions);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, 40);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, 8, 0);
    lv_obj_set_style_opa(actions, wifi_on ? LV_OPA_COVER : LV_OPA_40, 0);

    lv_obj_t *scan = lv_btn_create(actions);
    lv_obj_remove_style_all(scan);
    style_card(scan, true);
    lv_obj_set_style_bg_color(scan, p->accent, 0);
    if (zen) {
        lv_obj_set_style_radius(scan, 0, 0);
    }
    lv_obj_set_flex_grow(scan, 1);
    lv_obj_set_height(scan, LV_PCT(100));
    if (wifi_on) {
        lv_obj_add_event_cb(scan, scan_cb, LV_EVENT_CLICKED, NULL);
    }
    mk_lbl(scan, zh ? "扫描" : "Scan", p->ink_on);
    lv_obj_center(lv_obj_get_child(scan, 0));

    lv_obj_t *disc = lv_btn_create(actions);
    lv_obj_remove_style_all(disc);
    style_card(disc, false);
    if (zen) {
        lv_obj_set_style_radius(disc, 0, 0);
    }
    lv_obj_set_flex_grow(disc, 1);
    lv_obj_set_height(disc, LV_PCT(100));
    if (wifi_on) {
        lv_obj_add_event_cb(disc, disconnect_cb, LV_EVENT_CLICKED, NULL);
    }
    mk_lbl(disc, zh ? "断开" : "Disconnect", p->t1);
    lv_obj_center(lv_obj_get_child(disc, 0));

    mk_lbl(body, zh ? "选择热点" : "Select AP", p->t3);
    s_dd = lv_dropdown_create(body);
    lv_obj_set_width(s_dd, LV_PCT(100));
    hub_style_dropdown(s_dd);
    if (zen) {
        lv_obj_set_style_radius(s_dd, 0, 0);
    }
    rebuild_dropdown();
    lv_obj_add_event_cb(s_dd, dd_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if (!wifi_on) {
        lv_obj_add_state(s_dd, LV_STATE_DISABLED);
    }

    mk_lbl(body, zh ? "密码" : "Password", p->t3);

    lv_obj_t *pwd_row = lv_obj_create(body);
    lv_obj_remove_style_all(pwd_row);
    lv_obj_set_width(pwd_row, LV_PCT(100));
    lv_obj_set_height(pwd_row, 40);
    lv_obj_set_flex_flow(pwd_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pwd_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pwd_row, 8, 0);
    lv_obj_clear_flag(pwd_row, LV_OBJ_FLAG_SCROLLABLE);

    s_pwd = lv_textarea_create(pwd_row);
    lv_obj_set_flex_grow(s_pwd, 1);
    lv_obj_set_height(s_pwd, 40);
    lv_textarea_set_one_line(s_pwd, true);
    lv_textarea_set_password_mode(s_pwd, !s_pwd_visible);
    lv_textarea_set_max_length(s_pwd, (uint32_t)(sizeof(s_pwd_cache) - 1));
    lv_textarea_set_placeholder_text(s_pwd,
                                     zh ? "点此输入 · " LV_SYMBOL_OK " 关闭键盘"
                                        : "Tap · " LV_SYMBOL_OK " hides keyboard");
    /* CJK body + Montserrat symbols (✓ / eye) via fallback chain */
    lv_obj_set_style_text_font(s_pwd, hub_font(), 0);
    if (zen) {
        lv_obj_set_style_radius(s_pwd, 0, 0);
    }
    if (s_pwd_cache[0] != '\0') {
        lv_textarea_set_text(s_pwd, s_pwd_cache);
    }
    lv_obj_add_event_cb(s_pwd, pwd_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_pwd, pwd_focus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_pwd, pwd_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if (!wifi_on) {
        lv_obj_add_state(s_pwd, LV_STATE_DISABLED);
    }

    s_pwd_eye = lv_btn_create(pwd_row);
    lv_obj_remove_style_all(s_pwd_eye);
    style_card(s_pwd_eye, false);
    if (zen) {
        lv_obj_set_style_radius(s_pwd_eye, 0, 0);
    }
    lv_obj_set_size(s_pwd_eye, 40, 40);
    lv_obj_add_event_cb(s_pwd_eye, pwd_eye_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *eye_l = lv_label_create(s_pwd_eye);
    lv_label_set_text(eye_l, s_pwd_visible ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    hub_style_label(eye_l, p->t1, hub_font_symbol());
    lv_obj_center(eye_l);
    if (!wifi_on) {
        lv_obj_add_state(s_pwd_eye, LV_STATE_DISABLED);
        lv_obj_set_style_opa(s_pwd_eye, LV_OPA_40, 0);
    }

    lv_obj_t *join = lv_btn_create(body);
    lv_obj_remove_style_all(join);
    style_card(join, true);
    lv_obj_set_style_bg_color(join, p->accent, 0);
    if (zen) {
        lv_obj_set_style_radius(join, 0, 0);
    }
    lv_obj_set_width(join, LV_PCT(100));
    lv_obj_set_height(join, 44);
    if (wifi_on) {
        lv_obj_add_event_cb(join, connect_cb, LV_EVENT_CLICKED, NULL);
    } else {
        lv_obj_set_style_opa(join, LV_OPA_40, 0);
    }
    mk_lbl(join, zh ? "连接" : "Connect", p->ink_on);
    lv_obj_center(lv_obj_get_child(join, 0));

    lv_obj_t *cta = lv_btn_create(body);
    lv_obj_remove_style_all(cta);
    if (zen) {
        lv_obj_set_style_radius(cta, 0, 0);
        lv_obj_set_style_bg_opa(cta, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(cta, p->t1, 0);
    } else {
        style_card(cta, true);
        lv_obj_set_style_bg_color(cta, p->accent, 0);
    }
    lv_obj_set_width(cta, LV_PCT(100));
    lv_obj_set_height(cta, 40);
    lv_obj_add_event_cb(cta, go_gateway_cb, LV_EVENT_CLICKED, NULL);
    mk_lbl(cta, zh ? "协议总线状态 →" : "Gateway status →", p->ink_on);
    lv_obj_center(lv_obj_get_child(cta, 0));

    /* Spacer so body can scroll password above the floating keyboard */
    s_kb_pad = lv_obj_create(body);
    lv_obj_remove_style_all(s_kb_pad);
    lv_obj_set_width(s_kb_pad, LV_PCT(100));
    lv_obj_set_height(s_kb_pad, 0);
    lv_obj_clear_flag(s_kb_pad, LV_OBJ_FLAG_CLICKABLE);

    s_kb = lv_keyboard_create(parent);
    lv_obj_set_size(s_kb, 480, 180);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_FLOATING);
    lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    /* Montserrat has LV_SYMBOL_OK (✓) used by the Ready key — hub CJK fonts do not. */
    lv_obj_set_style_text_font(s_kb, hub_font_symbol(), 0);
    lv_obj_set_style_text_font(s_kb, hub_font_symbol(), LV_PART_ITEMS);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_CANCEL, NULL);
}

