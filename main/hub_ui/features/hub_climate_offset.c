#include "hub_climate_offset.h"

#include "hub_model.h"
#include "hub_ui.h"
#include "ui_bg_task.h"

#include "nvs.h"

#include <stdint.h>

#define HUB_CLIM_NS "hub_clim"
#define HUB_CLIM_KEY_T "t_off10" /* int16: offset °C * 10 */
#define HUB_CLIM_KEY_RH "rh_off" /* int8 */

#define TEMP_OFF_MIN_C (-10.0f)
#define TEMP_OFF_MAX_C (10.0f)
#define TEMP_STEP_C 0.5f
#define RH_OFF_MIN (-20)
#define RH_OFF_MAX (20)

static bool s_inited;
static bool s_have_raw;
static float s_raw_c;
static int s_raw_rh;
static float s_temp_off_c;
static int s_rh_off;

static float clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static int clampi(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static void persist(void)
{
    nvs_handle_t h;
    if (nvs_open(HUB_CLIM_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    int16_t t10 = (int16_t)(s_temp_off_c * 10.0f + (s_temp_off_c >= 0.0f ? 0.5f : -0.5f));
    int8_t rh = (int8_t)s_rh_off;
    (void)nvs_set_i16(h, HUB_CLIM_KEY_T, t10);
    (void)nvs_set_i8(h, HUB_CLIM_KEY_RH, rh);
    (void)nvs_commit(h);
    nvs_close(h);
}

void hub_climate_offset_init(void)
{
    if (s_inited) {
        return;
    }
    s_inited = true;
    s_temp_off_c = 0.0f;
    s_rh_off = 0;
    nvs_handle_t h;
    if (nvs_open(HUB_CLIM_NS, NVS_READONLY, &h) == ESP_OK) {
        int16_t t10 = 0;
        int8_t rh = 0;
        if (nvs_get_i16(h, HUB_CLIM_KEY_T, &t10) == ESP_OK) {
            s_temp_off_c = clampf((float)t10 / 10.0f, TEMP_OFF_MIN_C, TEMP_OFF_MAX_C);
        }
        if (nvs_get_i8(h, HUB_CLIM_KEY_RH, &rh) == ESP_OK) {
            s_rh_off = clampi((int)rh, RH_OFF_MIN, RH_OFF_MAX);
        }
        nvs_close(h);
    }
}

bool hub_climate_have_raw(void)
{
    return s_have_raw;
}

bool hub_climate_ui_available(void)
{
    return ui_bg_task_indoor_ready() && s_have_raw;
}

float hub_climate_raw_temp_c(void)
{
    return s_raw_c;
}

int hub_climate_raw_rh(void)
{
    return s_raw_rh;
}

void hub_climate_note_raw(float temp_c, int rh_pct)
{
    s_raw_c = temp_c;
    s_raw_rh = clampi(rh_pct, 0, 100);
    s_have_raw = true;
}

float hub_climate_temp_offset_c(void)
{
    hub_climate_offset_init();
    return s_temp_off_c;
}

int hub_climate_rh_offset(void)
{
    hub_climate_offset_init();
    return s_rh_off;
}

void hub_climate_set_temp_offset_c(float offset_c)
{
    hub_climate_offset_init();
    s_temp_off_c = clampf(offset_c, TEMP_OFF_MIN_C, TEMP_OFF_MAX_C);
    /* Snap to 0.5 °C */
    {
        float x2 = s_temp_off_c * 2.0f;
        int n = (int)(x2 + (x2 >= 0.0f ? 0.5f : -0.5f));
        s_temp_off_c = (float)n / 2.0f;
    }
    persist();
}

void hub_climate_set_rh_offset(int offset_rh)
{
    hub_climate_offset_init();
    s_rh_off = clampi(offset_rh, RH_OFF_MIN, RH_OFF_MAX);
    persist();
}

void hub_climate_step_temp(int dir)
{
    if (dir == 0) {
        return;
    }
    hub_climate_set_temp_offset_c(s_temp_off_c + (dir > 0 ? TEMP_STEP_C : -TEMP_STEP_C));
}

void hub_climate_step_rh(int dir)
{
    if (dir == 0) {
        return;
    }
    hub_climate_set_rh_offset(s_rh_off + (dir > 0 ? 1 : -1));
}

float hub_climate_display_temp_c(void)
{
    hub_climate_offset_init();
    if (!s_have_raw) {
        hub_model_t *m = hub_model();
        return m ? (m->indoor_c) : 0.0f;
    }
    return s_raw_c + s_temp_off_c;
}

int hub_climate_display_rh(void)
{
    hub_climate_offset_init();
    int rh;
    if (!s_have_raw) {
        hub_model_t *m = hub_model();
        rh = m ? m->rh : 0;
    } else {
        rh = s_raw_rh + s_rh_off;
    }
    return clampi(rh, 0, 100);
}

void hub_climate_apply_to_model(bool refresh_ui)
{
    hub_model_t *m = hub_model();
    if (!m || !s_have_raw) {
        /* No real sensor sample — do not invent values for UI/model. */
        return;
    }
    float t = hub_climate_display_temp_c();
    int rh = hub_climate_display_rh();
    const int t10 = (int)(t * 10.0f + (t >= 0.0f ? 0.5f : -0.5f));
    const int old_t10 = (int)(m->indoor_c * 10.0f + (m->indoor_c >= 0.0f ? 0.5f : -0.5f));
    const bool changed = (t10 != old_t10) || (m->rh != rh);
    m->indoor_c = t;
    m->rh = rh;
    if (changed && refresh_ui) {
        /* Soft: skip full rebuild on edit routes (avoids tear). */
        hub_ui_notify_soft();
    }
}
