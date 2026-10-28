#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Load offsets from NVS (safe to call multiple times). */
void hub_climate_offset_init(void);

/** Last sensor reading before UI offset (NaN / false if none yet). */
bool hub_climate_have_raw(void);
float hub_climate_raw_temp_c(void);
int hub_climate_raw_rh(void);

/**
 * True when indoor T/RH has a live sample (AHT20 ready).
 * When false, UI must still reserve the climate slot but show "--" (not demo numbers).
 */
bool hub_climate_ui_available(void);

/** Remember raw AHT20 (or other) sample before applying UI offset. */
void hub_climate_note_raw(float temp_c, int rh_pct);

float hub_climate_temp_offset_c(void);
int hub_climate_rh_offset(void);

/** Persist and clamp. Steps: temp 0.5 °C, RH 1 %. */
void hub_climate_set_temp_offset_c(float offset_c);
void hub_climate_set_rh_offset(int offset_rh);
void hub_climate_step_temp(int dir); /* dir = ±1 → ±0.5 °C */
void hub_climate_step_rh(int dir);   /* dir = ±1 → ±1 %RH */

/** raw + offset (RH clamped 0..100). */
float hub_climate_display_temp_c(void);
int hub_climate_display_rh(void);

/** Push display values into hub_model and refresh UI (LVGL thread). */
void hub_climate_apply_to_model(bool refresh_ui);

#ifdef __cplusplus
}
#endif
