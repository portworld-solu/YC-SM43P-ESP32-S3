#pragma once

/**
 * Customer SDK ui_runtime surface.
 * Hub themes use stubs in ui_runtime_stubs.c (no SquareLine ui_runtime).
 */

#include <stdbool.h>
#include <stdint.h>

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

#ifdef __cplusplus
extern "C" {
#endif

void ui_runtime_apply(void);
void ui_runtime_disable_btn_grow_everywhere(void);
void ui_runtime_disable_btn_grow_on(lv_obj_t *root);
bool ui_runtime_screen_switch_busy(void);
void ui_runtime_rgb_commit_active_screen(void);
void ui_runtime_rgb_commit_full_screen(void);
void ui_runtime_set_network_info(const char *mac, const char *ip);
void ui_runtime_step_screen3_temp(int delta);
void ui_runtime_step_screen5_temp(int delta);

typedef struct {
    uint8_t i1;
    uint8_t i2;
    uint8_t i3;
    int16_t t3;
    int16_t t5;
    uint8_t e3;
    uint8_t e5;
    uint8_t m;
} ui_runtime_cloud_snapshot_t;

void ui_runtime_fill_cloud_snapshot(ui_runtime_cloud_snapshot_t *out);
bool ui_runtime_allow_mqtt_temp_item(uint8_t item_id);
void ui_runtime_screen10_pick_and_home(uint8_t pick);
void ui_runtime_screen_change_by_obj(lv_obj_t *scr);

/** Apply AHT20 (or other) indoor climate into hub_model and refresh UI (LVGL thread). */
void ui_runtime_indoor_apply_climate(float temp_c, int rh_pct);
void ui_runtime_indoor_apply_temp(int temp_c);

#ifdef __cplusplus
}
#endif
