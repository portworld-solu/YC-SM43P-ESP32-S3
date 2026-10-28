#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Hub low-power standby (FE 待机) — idle timeout → minimal clock screen + dim BL. */
void hub_standby_init(void);

bool hub_standby_enabled(void);
void hub_standby_set_enabled(bool on);

/** Allowed: 15 / 30 / 60 / 120 / 300 / 600 seconds. */
uint32_t hub_standby_idle_sec(void);
void hub_standby_set_idle_sec(uint32_t sec);

/** Human label for current timeout (zh/en via hub_tr). */
const char *hub_standby_idle_label(void);

/** Dropdown options string for lv_dropdown (newline-separated). */
const char *hub_standby_idle_options(void);
int hub_standby_idle_index(void);
void hub_standby_set_idle_index(int idx);

/** Call when leaving standby UI to restore backlight. */
void hub_standby_on_exit_route(void);

#ifdef __cplusplus
}
#endif
