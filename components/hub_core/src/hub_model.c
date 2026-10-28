#include "hub_model.h"

#include <stdio.h>
#include <string.h>

#include "nvs.h"

static hub_model_t s_m;
static bool s_inited;

static const char *s_room_names_zh[HUB_ROOM_COUNT] = { "客厅", "主卧", "厨房", "书房" };
static const char *s_room_names_en[HUB_ROOM_COUNT] = { "Living", "Bedroom", "Kitchen", "Study" };

static const char *s_wtype_labels_zh[HUB_W_TYPE_COUNT] = {
    "开关灯", "调光", "窗帘", "卷帘", "空调", "地暖", "风扇", "插座",
};
static const char *s_wtype_labels_en[HUB_W_TYPE_COUNT] = {
    "Light", "Dimmer", "Curtain", "Shutter", "AC", "Heat", "Fan", "Plug",
};

static const char *s_wtype_defaults_zh[HUB_W_TYPE_COUNT] = {
    "主灯", "灯带", "窗帘", "卷帘", "空调", "地暖", "风扇", "插座",
};
static const char *s_wtype_defaults_en[HUB_W_TYPE_COUNT] = {
    "Ceiling", "Strip", "Curtain", "Shutter", "AC", "Floor", "Fan", "Socket",
};

static void add_w(int room, hub_wtype_t type, const char *name_zh, const char *name_en, bool on, int level)
{
    if (room < 0 || room >= HUB_ROOM_COUNT) {
        return;
    }
    int n = s_m.widget_count[room];
    if (n >= HUB_WIDGET_MAX) {
        return;
    }
    hub_widget_t *w = &s_m.widgets[room][n];
    memset(w, 0, sizeof(*w));
    w->type = type;
    w->enabled = true;
    w->on = on;
    w->level = level;
    strncpy(w->name, name_zh ? name_zh : s_wtype_defaults_zh[type], HUB_WIDGET_NAME_MAX - 1);
    strncpy(w->name_en, name_en ? name_en : s_wtype_defaults_en[type], HUB_WIDGET_NAME_MAX - 1);
    s_m.widget_count[room] = n + 1;
}

void hub_model_sync_legacy(void)
{
    for (int r = 0; r < HUB_ROOM_COUNT; r++) {
        hub_room_dev_t *d = &s_m.rooms[r];
        bool ceiling = false, ac = false, heat = false, fan = false, plug = false;
        int strip = 0, curtain = 40, shutter = 0, ac_sp = 24, heat_sp = 22;

        for (int i = 0; i < s_m.widget_count[r]; i++) {
            hub_widget_t *w = &s_m.widgets[r][i];
            if (!w->enabled) {
                continue;
            }
            switch (w->type) {
            case HUB_W_ONOFF:
                ceiling = w->on;
                break;
            case HUB_W_DIMMER:
                strip = w->on ? w->level : 0;
                break;
            case HUB_W_CURTAIN:
                curtain = w->level;
                break;
            case HUB_W_SHUTTER:
                shutter = w->level;
                break;
            case HUB_W_CLIM:
                ac = w->on;
                ac_sp = w->level;
                break;
            case HUB_W_THERMO:
                heat = w->on;
                heat_sp = w->level;
                break;
            case HUB_W_FAN:
                fan = w->on;
                break;
            case HUB_W_PLUG:
                plug = w->on;
                break;
            default:
                break;
            }
        }
        d->ceiling = ceiling;
        d->strip = strip;
        d->curtain = curtain;
        d->shutter = shutter;
        d->ac_on = ac;
        d->ac_sp = ac_sp;
        d->heat_on = heat;
        d->heat_sp = heat_sp;
        d->fan_on = fan;
        d->plug_on = plug;
        /* keep pump/valve as matrix extras */
    }

    float kw = 0.35f;
    for (int r = 0; r < HUB_ROOM_COUNT; r++) {
        if (hub_model_room_active(r)) {
            kw += 0.45f;
        }
    }
    s_m.power_kw = kw;
}

void hub_model_init(void)
{
    if (s_inited) {
        return;
    }
    s_inited = true;
    memset(&s_m, 0, sizeof(s_m));
    strncpy(s_m.active_scene, "home", sizeof(s_m.active_scene) - 1);
    s_m.armed = true;
    s_m.indoor_c = 24.5f;
    s_m.rh = 48;
    s_m.online_pts = 42;
    s_m.room_idx = 0;
    s_m.rooms[0].pump_ok = true;
    s_m.rooms[0].valve_ok = true;

    /* living — 8 widgets (matches FE) */
    add_w(0, HUB_W_ONOFF, "主灯", "Ceiling", true, 0);
    add_w(0, HUB_W_DIMMER, "灯带", "Strip", true, 55);
    add_w(0, HUB_W_CURTAIN, "窗帘", "Curtain", true, 70);
    add_w(0, HUB_W_SHUTTER, "卷帘", "Shutter", true, 30);
    add_w(0, HUB_W_CLIM, "空调", "AC", true, 24);
    add_w(0, HUB_W_THERMO, "地暖", "Floor", true, 23);
    add_w(0, HUB_W_FAN, "风扇", "Fan", false, 0);
    add_w(0, HUB_W_PLUG, "插座", "Socket", true, 0);

    /* bed */
    add_w(1, HUB_W_ONOFF, "床头灯", "Bedside", false, 0);
    add_w(1, HUB_W_DIMMER, "夜灯", "Night", true, 20);
    add_w(1, HUB_W_CURTAIN, "窗帘", "Curtain", true, 100);
    add_w(1, HUB_W_THERMO, "地暖", "Floor", true, 22);
    add_w(1, HUB_W_CLIM, "空调", "AC", false, 26);

    /* kitchen */
    add_w(2, HUB_W_ONOFF, "厨灯", "Kitchen", true, 0);
    add_w(2, HUB_W_DIMMER, "台面灯", "Counter", true, 80);
    add_w(2, HUB_W_PLUG, "插座", "Socket", true, 0);
    add_w(2, HUB_W_FAN, "排风扇", "Exhaust", false, 0);
    add_w(2, HUB_W_CURTAIN, "窗帘", "Curtain", true, 40);

    /* study */
    add_w(3, HUB_W_ONOFF, "顶灯", "Ceiling", true, 0);
    add_w(3, HUB_W_DIMMER, "灯带", "Strip", true, 40);
    add_w(3, HUB_W_CLIM, "空调", "AC", true, 25);
    add_w(3, HUB_W_CURTAIN, "窗帘", "Curtain", true, 60);
    add_w(3, HUB_W_PLUG, "桌插", "Desk", true, 0);

    s_m.protos[0] = (hub_proto_t){ .id = "mqtt", .name = "MQTT", .ok = true, .health = 98 };
    s_m.protos[1] = (hub_proto_t){ .id = "modbus", .name = "Modbus", .ok = true, .health = 86 };
    s_m.protos[2] = (hub_proto_t){ .id = "rs485", .name = "RS485", .ok = false, .health = 12 };
    s_m.protos[3] = (hub_proto_t){ .id = "wifi", .name = "Wi-Fi", .ok = true, .health = 92 };

    s_m.settings.night_mode = false;
    s_m.settings.lang_zh = true;
    s_m.settings.click_sound = true;
    {
        nvs_handle_t h;
        if (nvs_open("hub_ui", NVS_READONLY, &h) == ESP_OK) {
            uint8_t v = 1;
            if (nvs_get_u8(h, "lang_zh", &v) == ESP_OK) {
                s_m.settings.lang_zh = (v != 0);
            }
            nvs_close(h);
        }
    }

    /* FE TplSchedule demo rows */
    s_m.schedules[0] = (hub_sched_t){
        .id = "s1", .scene_id = "home", .on = true,
    };
    strncpy(s_m.schedules[0].title, "晨起唤醒", sizeof(s_m.schedules[0].title) - 1);
    strncpy(s_m.schedules[0].time, "07:00", sizeof(s_m.schedules[0].time) - 1);
    strncpy(s_m.schedules[0].days, "周一至周五", sizeof(s_m.schedules[0].days) - 1);
    strncpy(s_m.schedules[0].action, "情景 · 回家", sizeof(s_m.schedules[0].action) - 1);

    s_m.schedules[1] = (hub_sched_t){
        .id = "s2", .scene_id = "sleep", .on = true,
    };
    strncpy(s_m.schedules[1].title, "夜间模式", sizeof(s_m.schedules[1].title) - 1);
    strncpy(s_m.schedules[1].time, "22:30", sizeof(s_m.schedules[1].time) - 1);
    strncpy(s_m.schedules[1].days, "每天", sizeof(s_m.schedules[1].days) - 1);
    strncpy(s_m.schedules[1].action, "情景 · 睡眠", sizeof(s_m.schedules[1].action) - 1);

    s_m.schedules[2] = (hub_sched_t){
        .id = "s3", .scene_id = "away", .on = false,
    };
    strncpy(s_m.schedules[2].title, "离家布防", sizeof(s_m.schedules[2].title) - 1);
    strncpy(s_m.schedules[2].time, "09:00", sizeof(s_m.schedules[2].time) - 1);
    strncpy(s_m.schedules[2].days, "工作日", sizeof(s_m.schedules[2].days) - 1);
    strncpy(s_m.schedules[2].action, "情景 · 离家", sizeof(s_m.schedules[2].action) - 1);

    hub_model_sync_legacy();
}

hub_model_t *hub_model(void)
{
    if (!s_inited) {
        hub_model_init();
    }
    return &s_m;
}

int hub_model_ok_count(void)
{
    int n = 0;
    for (int i = 0; i < HUB_PROTO_COUNT; i++) {
        if (s_m.protos[i].ok) {
            n++;
        }
    }
    return n;
}

const char *hub_model_room_name(int idx)
{
    if (idx < 0 || idx >= HUB_ROOM_COUNT) {
        return "?";
    }
    return s_m.settings.lang_zh ? s_room_names_zh[idx] : s_room_names_en[idx];
}

const char *hub_model_scene_label(const char *scene_id)
{
    if (!scene_id) {
        return "";
    }
    bool zh = s_m.settings.lang_zh;
    if (strcmp(scene_id, "home") == 0) {
        return zh ? "回家" : "Home";
    }
    if (strcmp(scene_id, "away") == 0) {
        return zh ? "离家" : "Away";
    }
    if (strcmp(scene_id, "movie") == 0) {
        return zh ? "观影" : "Movie";
    }
    if (strcmp(scene_id, "sleep") == 0) {
        return zh ? "睡眠" : "Sleep";
    }
    if (strcmp(scene_id, "guest") == 0) {
        return zh ? "会客" : "Guest";
    }
    if (strcmp(scene_id, "eco") == 0) {
        return zh ? "节能" : "Eco";
    }
    if (strcmp(scene_id, "morning") == 0) {
        return zh ? "晨起" : "Morning";
    }
    if (strcmp(scene_id, "alloff") == 0) {
        return zh ? "全关" : "All off";
    }
    return scene_id;
}

const char *hub_model_scene_sub(const char *scene_id)
{
    if (!scene_id) {
        return "";
    }
    bool zh = s_m.settings.lang_zh;
    if (strcmp(scene_id, "home") == 0) {
        return zh ? "灯 + 空调" : "Lights + AC";
    }
    if (strcmp(scene_id, "away") == 0) {
        return zh ? "全关 + 布防" : "All off + arm";
    }
    if (strcmp(scene_id, "movie") == 0) {
        return zh ? "暗光 + 关帘" : "Dim + close";
    }
    if (strcmp(scene_id, "sleep") == 0) {
        return zh ? "夜灯模式" : "Night light";
    }
    if (strcmp(scene_id, "guest") == 0) {
        return zh ? "客厅明亮" : "Bright living";
    }
    if (strcmp(scene_id, "eco") == 0) {
        return zh ? "降低负荷" : "Lower load";
    }
    if (strcmp(scene_id, "morning") == 0) {
        return zh ? "晨间唤醒" : "Morning wake";
    }
    if (strcmp(scene_id, "alloff") == 0) {
        return zh ? "全部关闭" : "Everything off";
    }
    return "";
}

bool hub_model_room_active(int idx)
{
    if (idx < 0 || idx >= HUB_ROOM_COUNT) {
        return false;
    }
    const hub_room_dev_t *d = &s_m.rooms[idx];
    return d->ceiling || d->strip > 0 || d->ac_on || d->heat_on || d->fan_on || d->plug_on;
}

void hub_model_set_room(int idx)
{
    if (idx >= 0 && idx < HUB_ROOM_COUNT) {
        s_m.room_idx = idx;
        s_m.room_subpage = 0;
    }
}

void hub_model_set_armed(bool armed)
{
    s_m.armed = armed;
}

void hub_model_toggle_proto(const char *id)
{
    if (!id) {
        return;
    }
    for (int i = 0; i < HUB_PROTO_COUNT; i++) {
        if (strcmp(s_m.protos[i].id, id) == 0) {
            s_m.protos[i].ok = !s_m.protos[i].ok;
            s_m.protos[i].health = s_m.protos[i].ok ? (i == 0 ? 98 : (i == 1 ? 86 : 72)) : 12;
            return;
        }
    }
}

void hub_model_apply_scene(const char *scene_id)
{
    if (!scene_id) {
        return;
    }
    strncpy(s_m.active_scene, scene_id, sizeof(s_m.active_scene) - 1);
    s_m.active_scene[sizeof(s_m.active_scene) - 1] = '\0';

    for (int r = 0; r < HUB_ROOM_COUNT; r++) {
        for (int i = 0; i < s_m.widget_count[r]; i++) {
            hub_widget_t *w = &s_m.widgets[r][i];
            if (!w->enabled) {
                continue;
            }
            if (strcmp(scene_id, "home") == 0 || strcmp(scene_id, "guest") == 0 ||
                strcmp(scene_id, "morning") == 0) {
                if (w->type == HUB_W_ONOFF || w->type == HUB_W_PLUG) {
                    w->on = true;
                }
                if (w->type == HUB_W_DIMMER) {
                    w->on = true;
                    w->level = (r == 0) ? 60 : 40;
                }
                if (w->type == HUB_W_CLIM) {
                    w->on = (r == 0);
                }
                if (w->type == HUB_W_CURTAIN && strcmp(scene_id, "morning") == 0) {
                    w->level = 90;
                }
            } else if (strcmp(scene_id, "away") == 0 || strcmp(scene_id, "alloff") == 0) {
                if (w->type == HUB_W_ONOFF || w->type == HUB_W_PLUG || w->type == HUB_W_FAN ||
                    w->type == HUB_W_CLIM || w->type == HUB_W_THERMO) {
                    w->on = false;
                }
                if (w->type == HUB_W_DIMMER) {
                    w->on = false;
                    w->level = 0;
                }
            } else if (strcmp(scene_id, "movie") == 0) {
                if (r == 0) {
                    if (w->type == HUB_W_ONOFF) {
                        w->on = false;
                    }
                    if (w->type == HUB_W_DIMMER) {
                        w->on = true;
                        w->level = 20;
                    }
                    if (w->type == HUB_W_CURTAIN) {
                        w->level = 10;
                    }
                    if (w->type == HUB_W_CLIM) {
                        w->on = true;
                        w->level = 24;
                    }
                }
            } else if (strcmp(scene_id, "sleep") == 0) {
                if (w->type == HUB_W_ONOFF || w->type == HUB_W_CLIM) {
                    w->on = false;
                }
                if (w->type == HUB_W_DIMMER) {
                    w->on = (r == 1);
                    w->level = (r == 1) ? 8 : 0;
                }
                if (w->type == HUB_W_THERMO && r == 1) {
                    w->on = true;
                }
            } else if (strcmp(scene_id, "eco") == 0) {
                if (w->type == HUB_W_DIMMER && w->level > 30) {
                    w->level = 30;
                }
                if (w->type == HUB_W_CLIM) {
                    w->level = 26;
                }
            }
        }
    }

    if (strcmp(scene_id, "away") == 0 || strcmp(scene_id, "alloff") == 0 ||
        strcmp(scene_id, "sleep") == 0) {
        s_m.armed = true;
    } else if (strcmp(scene_id, "home") == 0 || strcmp(scene_id, "guest") == 0 ||
               strcmp(scene_id, "morning") == 0) {
        s_m.armed = false;
    }

    hub_model_sync_legacy();
    hub_model_toast(s_m.settings.lang_zh ? "情景已应用" : "Scene applied");
}

void hub_model_toggle_zone(int cell)
{
    hub_room_dev_t *d = &s_m.rooms[0];
    /* Prefer widget slots when present */
    hub_widget_t *list = s_m.widgets[0];
    int n = s_m.widget_count[0];
    (void)n;
    switch (cell) {
    case 0:
        hub_model_toggle_widget(0, 0);
        break;
    case 1:
        hub_model_toggle_widget(0, 1);
        break;
    case 2:
        hub_model_toggle_widget(0, 4);
        break;
    case 3:
        hub_model_toggle_widget(0, 5);
        break;
    case 4:
        if (list[2].type == HUB_W_CURTAIN) {
            list[2].level = list[2].level > 50 ? 10 : 80;
        }
        break;
    case 5:
        if (list[3].type == HUB_W_SHUTTER) {
            list[3].level = list[3].level > 50 ? 0 : 100;
        }
        break;
    case 6:
        hub_model_toggle_widget(0, 6);
        break;
    case 7:
        d->pump_ok = !d->pump_ok;
        break;
    case 8:
        d->valve_ok = !d->valve_ok;
        break;
    default:
        break;
    }
    hub_model_sync_legacy();
}

void hub_model_step_ac(int delta)
{
    int r = s_m.room_idx;
    for (int i = 0; i < s_m.widget_count[r]; i++) {
        hub_widget_t *w = &s_m.widgets[r][i];
        if (w->enabled && w->type == HUB_W_CLIM) {
            w->on = true;
            w->level += delta;
            if (w->level < 16) {
                w->level = 16;
            }
            if (w->level > 30) {
                w->level = 30;
            }
            hub_model_sync_legacy();
            return;
        }
    }
}

void hub_model_step_curtain(int delta)
{
    int r = s_m.room_idx;
    for (int i = 0; i < s_m.widget_count[r]; i++) {
        hub_widget_t *w = &s_m.widgets[r][i];
        if (w->enabled && w->type == HUB_W_CURTAIN) {
            hub_model_step_widget(r, i, delta);
            return;
        }
    }
}

void hub_model_step_strip(int delta)
{
    int r = s_m.room_idx;
    for (int i = 0; i < s_m.widget_count[r]; i++) {
        hub_widget_t *w = &s_m.widgets[r][i];
        if (w->enabled && w->type == HUB_W_DIMMER) {
            hub_model_step_widget(r, i, delta);
            return;
        }
    }
}

void hub_model_toggle_ceiling(void)
{
    int r = s_m.room_idx;
    for (int i = 0; i < s_m.widget_count[r]; i++) {
        hub_widget_t *w = &s_m.widgets[r][i];
        if (w->enabled && w->type == HUB_W_ONOFF) {
            hub_model_toggle_widget(r, i);
            return;
        }
    }
}

int hub_model_enabled_widget_count(int room_idx)
{
    if (room_idx < 0 || room_idx >= HUB_ROOM_COUNT) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < s_m.widget_count[room_idx]; i++) {
        if (s_m.widgets[room_idx][i].enabled) {
            n++;
        }
    }
    return n;
}

hub_widget_t *hub_model_widget_at(int room_idx, int enabled_index)
{
    if (room_idx < 0 || room_idx >= HUB_ROOM_COUNT) {
        return NULL;
    }
    int seen = 0;
    for (int i = 0; i < s_m.widget_count[room_idx]; i++) {
        if (!s_m.widgets[room_idx][i].enabled) {
            continue;
        }
        if (seen == enabled_index) {
            return &s_m.widgets[room_idx][i];
        }
        seen++;
    }
    return NULL;
}

hub_widget_t *hub_model_widget_by_slot(int room_idx, int slot)
{
    if (room_idx < 0 || room_idx >= HUB_ROOM_COUNT || slot < 0 ||
        slot >= s_m.widget_count[room_idx]) {
        return NULL;
    }
    return &s_m.widgets[room_idx][slot];
}

void hub_model_toggle_widget(int room_idx, int slot)
{
    hub_widget_t *w = hub_model_widget_by_slot(room_idx, slot);
    if (!w || !w->enabled) {
        return;
    }
    if (w->type == HUB_W_DIMMER) {
        if (w->on && w->level > 0) {
            w->on = false;
        } else {
            w->on = true;
            if (w->level <= 0) {
                w->level = 55;
            }
        }
    } else if (w->type == HUB_W_CURTAIN || w->type == HUB_W_SHUTTER) {
        w->level = w->level > 50 ? 0 : 100;
    } else {
        w->on = !w->on;
    }
    hub_model_sync_legacy();
}

void hub_model_step_widget(int room_idx, int slot, int delta)
{
    hub_widget_t *w = hub_model_widget_by_slot(room_idx, slot);
    if (!w || !w->enabled) {
        return;
    }
    if (w->type == HUB_W_CLIM || w->type == HUB_W_THERMO) {
        w->on = true;
        w->level += delta;
        if (w->level < 16) {
            w->level = 16;
        }
        if (w->level > 30) {
            w->level = 30;
        }
    } else if (w->type == HUB_W_DIMMER || w->type == HUB_W_CURTAIN || w->type == HUB_W_SHUTTER) {
        w->on = true;
        w->level += delta;
        if (w->level < 0) {
            w->level = 0;
        }
        if (w->level > 100) {
            w->level = 100;
        }
        if (w->type == HUB_W_DIMMER && w->level == 0) {
            w->on = false;
        }
    }
    hub_model_sync_legacy();
}

void hub_model_set_widget_level(int room_idx, int slot, int level)
{
    hub_widget_t *w = hub_model_widget_by_slot(room_idx, slot);
    if (!w) {
        return;
    }
    if (level < 0) {
        level = 0;
    }
    if (level > 100) {
        level = 100;
    }
    w->level = level;
    if (w->type == HUB_W_DIMMER) {
        w->on = level > 0;
    }
    hub_model_sync_legacy();
}

void hub_model_set_widget_enabled(int room_idx, int slot, bool enabled)
{
    hub_widget_t *w = hub_model_widget_by_slot(room_idx, slot);
    if (!w) {
        return;
    }
    w->enabled = enabled;
    hub_model_sync_legacy();
}

bool hub_model_add_widget(int room_idx, hub_wtype_t type)
{
    if (type >= HUB_W_TYPE_COUNT || room_idx < 0 || room_idx >= HUB_ROOM_COUNT) {
        return false;
    }
    if (s_m.widget_count[room_idx] >= HUB_WIDGET_MAX) {
        return false;
    }
    int def_level = 50;
    bool on = true;
    if (type == HUB_W_CLIM || type == HUB_W_THERMO) {
        def_level = 24;
    }
    if (type == HUB_W_FAN) {
        on = false;
    }
    add_w(room_idx, type, s_wtype_defaults_zh[type], s_wtype_defaults_en[type], on, def_level);
    hub_model_sync_legacy();
    hub_model_toast(s_m.settings.lang_zh ? "已添加控件" : "Widget added");
    return true;
}

void hub_model_remove_widget(int room_idx, int slot)
{
    if (room_idx < 0 || room_idx >= HUB_ROOM_COUNT || slot < 0 ||
        slot >= s_m.widget_count[room_idx]) {
        return;
    }
    for (int i = slot; i < s_m.widget_count[room_idx] - 1; i++) {
        s_m.widgets[room_idx][i] = s_m.widgets[room_idx][i + 1];
    }
    s_m.widget_count[room_idx]--;
    hub_model_sync_legacy();
    hub_model_toast(s_m.settings.lang_zh ? "已删除控件" : "Widget removed");
}

const char *hub_model_wtype_label(hub_wtype_t type)
{
    if (type >= HUB_W_TYPE_COUNT) {
        return "?";
    }
    return s_m.settings.lang_zh ? s_wtype_labels_zh[type] : s_wtype_labels_en[type];
}

const char *hub_model_widget_label(const hub_widget_t *w)
{
    if (!w) {
        return "";
    }
    if (!s_m.settings.lang_zh && w->name_en[0]) {
        return w->name_en;
    }
    return w->name;
}

void hub_model_toast(const char *msg)
{
    if (!msg) {
        return;
    }
    strncpy(s_m.toast, msg, HUB_TOAST_MAX - 1);
    s_m.toast[HUB_TOAST_MAX - 1] = '\0';
    s_m.toast_pending = true;
}

bool hub_model_take_toast(char *out, size_t out_sz)
{
    if (!s_m.toast_pending || !out || out_sz == 0) {
        return false;
    }
    strncpy(out, s_m.toast, out_sz - 1);
    out[out_sz - 1] = '\0';
    s_m.toast_pending = false;
    return true;
}

const char *hub_model_sched_title(int idx)
{
    if (idx < 0 || idx >= HUB_SCHED_COUNT) {
        return "";
    }
    bool zh = s_m.settings.lang_zh;
    const char *id = s_m.schedules[idx].id;
    if (id && strcmp(id, "s1") == 0) {
        return zh ? "晨起唤醒" : "Morning wake";
    }
    if (id && strcmp(id, "s2") == 0) {
        return zh ? "夜间模式" : "Night mode";
    }
    if (id && strcmp(id, "s3") == 0) {
        return zh ? "离家布防" : "Away arm";
    }
    return s_m.schedules[idx].title;
}

const char *hub_model_sched_days(int idx)
{
    if (idx < 0 || idx >= HUB_SCHED_COUNT) {
        return "";
    }
    bool zh = s_m.settings.lang_zh;
    const char *id = s_m.schedules[idx].id;
    if (id && strcmp(id, "s1") == 0) {
        return zh ? "周一至周五" : "Mon–Fri";
    }
    if (id && strcmp(id, "s2") == 0) {
        return zh ? "每天" : "Daily";
    }
    if (id && strcmp(id, "s3") == 0) {
        return zh ? "工作日" : "Weekdays";
    }
    return s_m.schedules[idx].days;
}

const char *hub_model_sched_action(int idx)
{
    if (idx < 0 || idx >= HUB_SCHED_COUNT) {
        return "";
    }
    bool zh = s_m.settings.lang_zh;
    const char *sid = s_m.schedules[idx].scene_id;
    if (sid) {
        static char buf[HUB_SCHED_META_MAX];
        snprintf(buf, sizeof(buf), zh ? "情景 · %s" : "Scene · %s", hub_model_scene_label(sid));
        return buf;
    }
    return s_m.schedules[idx].action;
}

void hub_model_toggle_schedule(int idx)
{
    if (idx < 0 || idx >= HUB_SCHED_COUNT) {
        return;
    }
    s_m.schedules[idx].on = !s_m.schedules[idx].on;
    hub_model_toast(s_m.schedules[idx].on
                        ? (s_m.settings.lang_zh ? "日程已启用" : "Schedule on")
                        : (s_m.settings.lang_zh ? "日程已关闭" : "Schedule off"));
}

void hub_model_run_schedule(int idx)
{
    if (idx < 0 || idx >= HUB_SCHED_COUNT) {
        return;
    }
    hub_sched_t *s = &s_m.schedules[idx];
    if (!s->on) {
        hub_model_toast(s_m.settings.lang_zh ? "日程未启用" : "Schedule disabled");
        return;
    }
    if (s->scene_id) {
        hub_model_apply_scene(s->scene_id);
    }
}

int hub_model_sched_on_count(void)
{
    int n = 0;
    for (int i = 0; i < HUB_SCHED_COUNT; i++) {
        if (s_m.schedules[i].on) {
            n++;
        }
    }
    return n;
}

void hub_model_set_night_mode(bool on)
{
    s_m.settings.night_mode = on;
    hub_model_toast(on ? (s_m.settings.lang_zh ? "夜间模式 · 降亮" : "Night mode · dim")
                       : (s_m.settings.lang_zh ? "夜间模式关闭" : "Night mode off"));
}

void hub_model_set_click_sound(bool on)
{
    s_m.settings.click_sound = on;
    hub_model_toast(on ? (s_m.settings.lang_zh ? "触控音开启" : "Click sound on")
                       : (s_m.settings.lang_zh ? "触控音静音" : "Click sound mute"));
}

void hub_model_toggle_lang(void)
{
    s_m.settings.lang_zh = !s_m.settings.lang_zh;
    nvs_handle_t h;
    if (nvs_open("hub_ui", NVS_READWRITE, &h) == ESP_OK) {
        (void)nvs_set_u8(h, "lang_zh", s_m.settings.lang_zh ? 1 : 0);
        (void)nvs_commit(h);
        nvs_close(h);
    }
    hub_model_toast(s_m.settings.lang_zh ? "语言：中文" : "Language: EN");
}
