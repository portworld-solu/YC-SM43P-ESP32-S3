#include "app_modbus_map.h"
#include "app_modbus_map_config.h"

#include "sdkconfig.h"
#include "esp_log.h"
#include "nvs.h"

#include <string.h>

static const char *TAG = "mb_map";

#define MB_NVS_NS "app_mbmap"
#define MB_NVS_KEY "tbl"
#define MB_SLOT_MAX (HUB_ROOM_COUNT * HUB_WIDGET_MAX)

static app_modbus_binding_t s_rt[HUB_ROOM_COUNT][HUB_WIDGET_MAX];
static app_modbus_binding_t s_flat[MB_SLOT_MAX];
static size_t s_flat_n;
static bool s_inited;

app_mb_encode_t app_modbus_map_encode_for_type(hub_wtype_t type)
{
    switch (type) {
    case HUB_W_ONOFF:
    case HUB_W_FAN:
    case HUB_W_PLUG:
        return APP_MB_ENC_ONOFF;
    default:
        return APP_MB_ENC_LEVEL;
    }
}

void app_modbus_map_suggest(int room, int slot, hub_wtype_t type, app_modbus_binding_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (room < 0 || room >= HUB_ROOM_COUNT || slot < 0 || slot >= HUB_WIDGET_MAX) {
        return;
    }
    out->enabled = true;
    out->room = (int8_t)room;
    out->slot = (int8_t)slot;
    out->type = type;
    out->slave = (uint8_t)(room + 1);
    out->wr_addr = (uint16_t)(room * 0x0010u + (uint16_t)slot);
    out->rd_addr = out->wr_addr;
    out->poll = true;
    out->encode = app_modbus_map_encode_for_type(type);
    out->on_val = 1;
    out->off_val = 0;
    out->scale_num = 1;
    out->scale_den = 1;
    out->offset = 0;
}

static void rebuild_flat(void)
{
    s_flat_n = 0;
    for (int r = 0; r < HUB_ROOM_COUNT; r++) {
        for (int s = 0; s < HUB_WIDGET_MAX; s++) {
            if (!s_rt[r][s].enabled && s_rt[r][s].slave == 0 && s_rt[r][s].wr_addr == 0) {
                continue;
            }
            /* keep disabled-but-configured rows so editor / NVS round-trip works */
            if (s_rt[r][s].slave == 0 && !s_rt[r][s].enabled) {
                continue;
            }
            s_flat[s_flat_n++] = s_rt[r][s];
        }
    }
}

static void clear_rt(void)
{
    memset(s_rt, 0, sizeof(s_rt));
}

static void apply_seed(void)
{
    size_t n = sizeof(s_app_modbus_map_seed) / sizeof(s_app_modbus_map_seed[0]);
    for (size_t i = 0; i < n; i++) {
        const app_modbus_binding_t *b = &s_app_modbus_map_seed[i];
        if (b->room < 0 || b->room >= HUB_ROOM_COUNT || b->slot < 0 || b->slot >= HUB_WIDGET_MAX) {
            continue;
        }
        s_rt[b->room][b->slot] = *b;
    }
}

static bool load_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(MB_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t need = sizeof(s_rt);
    esp_err_t err = nvs_get_blob(h, MB_NVS_KEY, s_rt, &need);
    nvs_close(h);
    if (err != ESP_OK || need != sizeof(s_rt)) {
        return false;
    }
    /* fix room/slot keys in case blob was hand-edited */
    for (int r = 0; r < HUB_ROOM_COUNT; r++) {
        for (int s = 0; s < HUB_WIDGET_MAX; s++) {
            if (s_rt[r][s].slave != 0 || s_rt[r][s].enabled) {
                s_rt[r][s].room = (int8_t)r;
                s_rt[r][s].slot = (int8_t)s;
            }
        }
    }
    return true;
}

esp_err_t app_modbus_map_save(void)
{
#if !CONFIG_APP_MODBUS_MAP_PERSIST_NVS
    /* Product builds: point map is compile-time only — never overwrite seed via NVS. */
    rebuild_flat();
    return ESP_OK;
#else
    nvs_handle_t h;
    if (nvs_open(MB_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return ESP_FAIL;
    }
    esp_err_t err = nvs_set_blob(h, MB_NVS_KEY, s_rt, sizeof(s_rt));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    rebuild_flat();
    return err;
#endif
}

void app_modbus_map_ensure(int room, int slot, hub_wtype_t type)
{
    if (room < 0 || room >= HUB_ROOM_COUNT || slot < 0 || slot >= HUB_WIDGET_MAX) {
        return;
    }
    app_modbus_binding_t *b = &s_rt[room][slot];
    bool empty = (b->slave == 0 && b->wr_addr == 0 && !b->enabled);
    if (empty) {
        app_modbus_map_suggest(room, slot, type, b);
        return;
    }
    b->room = (int8_t)room;
    b->slot = (int8_t)slot;
    if (b->type != type) {
        /* keep addresses; refresh encode for new type */
        b->type = type;
        b->encode = app_modbus_map_encode_for_type(type);
    }
}

void app_modbus_map_sync_model(void)
{
    hub_model_t *m = hub_model();
    if (!m) {
        return;
    }
    for (int r = 0; r < HUB_ROOM_COUNT; r++) {
        int n = m->widget_count[r];
        if (n < 0) {
            n = 0;
        }
        if (n > HUB_WIDGET_MAX) {
            n = HUB_WIDGET_MAX;
        }
        for (int s = 0; s < n; s++) {
            app_modbus_map_ensure(r, s, m->widgets[r][s].type);
        }
        for (int s = n; s < HUB_WIDGET_MAX; s++) {
            if (s_rt[r][s].enabled || s_rt[r][s].slave != 0) {
                /* keep NVS leftovers disabled so they don't poll ghost slots */
                s_rt[r][s].enabled = false;
            }
        }
    }
    rebuild_flat();
}

void app_modbus_map_on_widget_added(int room, int slot, hub_wtype_t type)
{
    /* RAM only — caller persists via async NVS (never block LVGL thread). */
    app_modbus_map_suggest(room, slot, type, &s_rt[room][slot]);
    rebuild_flat();
}

void app_modbus_map_on_widget_removed(int room, int slot)
{
    if (room < 0 || room >= HUB_ROOM_COUNT || slot < 0 || slot >= HUB_WIDGET_MAX) {
        return;
    }
    for (int s = slot; s < HUB_WIDGET_MAX - 1; s++) {
        s_rt[room][s] = s_rt[room][s + 1];
        s_rt[room][s].slot = (int8_t)s;
        s_rt[room][s].room = (int8_t)room;
    }
    memset(&s_rt[room][HUB_WIDGET_MAX - 1], 0, sizeof(s_rt[0][0]));
    rebuild_flat();
    /* Persist off UI thread — see mb_persist_async in hub_room_ui. */
}

esp_err_t app_modbus_map_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }
    clear_rt();
#if CONFIG_APP_MODBUS_MAP_PERSIST_NVS
    if (!load_nvs()) {
        apply_seed();
        ESP_LOGI(TAG, "seeded %u factory rows",
                 (unsigned)(sizeof(s_app_modbus_map_seed) / sizeof(s_app_modbus_map_seed[0])));
    } else {
        ESP_LOGI(TAG, "loaded NVS map");
    }
#else
    apply_seed();
    ESP_LOGI(TAG, "seed-only map (%u rows, NVS persist off)",
             (unsigned)(sizeof(s_app_modbus_map_seed) / sizeof(s_app_modbus_map_seed[0])));
#endif
    app_modbus_map_sync_model();
    s_inited = true;
    return ESP_OK;
}

const app_modbus_binding_t *app_modbus_map_table(size_t *out_count)
{
    if (!s_inited) {
        (void)app_modbus_map_init();
    }
    if (out_count) {
        *out_count = s_flat_n;
    }
    return s_flat;
}

const app_modbus_binding_t *app_modbus_map_find(int room, int slot)
{
    if (!s_inited) {
        (void)app_modbus_map_init();
    }
    if (room < 0 || room >= HUB_ROOM_COUNT || slot < 0 || slot >= HUB_WIDGET_MAX) {
        return NULL;
    }
    const app_modbus_binding_t *b = &s_rt[room][slot];
    if (!b->enabled) {
        return NULL;
    }
    return b;
}

app_modbus_binding_t *app_modbus_map_get(int room, int slot)
{
    if (!s_inited) {
        (void)app_modbus_map_init();
    }
    if (room < 0 || room >= HUB_ROOM_COUNT || slot < 0 || slot >= HUB_WIDGET_MAX) {
        return NULL;
    }
    return &s_rt[room][slot];
}

bool app_modbus_map_encode(const app_modbus_binding_t *b, const hub_widget_t *w, uint16_t *out_reg)
{
    if (!b || !w || !out_reg || !b->enabled) {
        return false;
    }
    if (w->type != b->type) {
        ESP_LOGW(TAG, "type mismatch room=%d slot=%d map=%d widget=%d",
                 (int)b->room, (int)b->slot, (int)b->type, (int)w->type);
    }

    switch (b->encode) {
    case APP_MB_ENC_ONOFF:
        *out_reg = w->on ? b->on_val : b->off_val;
        return true;
    case APP_MB_ENC_LEVEL: {
        int den = b->scale_den == 0 ? 1 : b->scale_den;
        int level = w->level;
        if (level < 0) {
            level = 0;
        }
        if (level > 100) {
            level = 100;
        }
        if (!w->on && (w->type == HUB_W_DIMMER || w->type == HUB_W_FAN)) {
            level = 0;
        }
        int v = level * (int)b->scale_num / den + (int)b->offset;
        if (v < 0) {
            v = 0;
        }
        if (v > 65535) {
            v = 65535;
        }
        *out_reg = (uint16_t)v;
        return true;
    }
    default:
        return false;
    }
}

bool app_modbus_map_apply_reg(const app_modbus_binding_t *b, uint16_t reg)
{
    if (!b || !b->enabled) {
        return false;
    }
    hub_widget_t *w = hub_model_widget_by_slot((int)b->room, (int)b->slot);
    if (!w || !w->enabled) {
        return false;
    }

    bool changed = false;
    switch (b->encode) {
    case APP_MB_ENC_ONOFF: {
        bool want = (reg == b->on_val);
        if (w->on != want) {
            hub_model_toggle_widget((int)b->room, (int)b->slot);
            changed = true;
        }
        break;
    }
    case APP_MB_ENC_LEVEL: {
        int den = b->scale_den == 0 ? 1 : b->scale_den;
        int level = ((int)reg - (int)b->offset) * den;
        if (b->scale_num != 0) {
            level /= (int)b->scale_num;
        }
        if (level < 0) {
            level = 0;
        }
        if (level > 100) {
            level = 100;
        }
        if (w->level != level) {
            hub_model_set_widget_level((int)b->room, (int)b->slot, level);
            changed = true;
            w = hub_model_widget_by_slot((int)b->room, (int)b->slot);
            if (!w) {
                break;
            }
        }
        if (w->type == HUB_W_DIMMER || w->type == HUB_W_FAN || w->type == HUB_W_PLUG ||
            w->type == HUB_W_ONOFF) {
            bool want_on = (level > 0);
            if (w->on != want_on) {
                hub_model_toggle_widget((int)b->room, (int)b->slot);
                changed = true;
            }
        }
        break;
    }
    default:
        break;
    }

    if (changed) {
        hub_model_sync_legacy();
    }
    return changed;
}
