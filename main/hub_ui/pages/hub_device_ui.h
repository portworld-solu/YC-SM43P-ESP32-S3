#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Real Wi-Fi scan / join / disconnect (hub network page). */
void hub_build_network(lv_obj_t *parent);

/** Brightness slider + NVS — append into settings body. */
void hub_build_brightness_block(lv_obj_t *body);

/** Current stored brightness 0–100 (NVS). Night mode dims at apply time. */
int hub_device_brightness_get(void);
void hub_device_brightness_apply_effective(void);

/** Sync hub model wifi proto from real STA state. */
void hub_wifi_sync_model(void);

/** Recursively mark children for gesture bubbling; skips sliders / inputs. */
void hub_nav_enable_gesture_bubble(lv_obj_t *root);
/** True if obj (or ancestor) should suppress horizontal page swipe. */
bool hub_nav_obj_blocks_swipe(const lv_obj_t *obj);

#ifdef __cplusplus
}
#endif
