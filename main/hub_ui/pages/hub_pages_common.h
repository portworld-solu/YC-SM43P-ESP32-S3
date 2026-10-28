#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** FE-shared pages (theme-agnostic IA; chrome via palette + theme_card_chrome). */
void hub_build_scenes(lv_obj_t *parent);
void hub_build_energy(lv_obj_t *parent);
void hub_build_gateway(lv_obj_t *parent);
void hub_build_points(lv_obj_t *parent);
/** FE TplMinimalHome — standby clock + 2×2 scene shortcuts. */
void hub_build_standby(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
