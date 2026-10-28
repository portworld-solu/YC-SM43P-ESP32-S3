#pragma once

#include "lvgl.h"
#include "hub_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HUB_ROUTE_HOME = 0,
    HUB_ROUTE_ROOM,
    HUB_ROUTE_SCENES,
    HUB_ROUTE_ENERGY,
    HUB_ROUTE_GATEWAY,
    HUB_ROUTE_SETTINGS,
    HUB_ROUTE_NETWORK,
    HUB_ROUTE_SECURITY,
    HUB_ROUTE_SCHEDULE,
    HUB_ROUTE_HVAC,
    HUB_ROUTE_POINTS,
    HUB_ROUTE_ROOM_EDIT,
    HUB_ROUTE_ROOM_MB_EDIT, /**< per-widget Modbus binding editor */
    HUB_ROUTE_APPLIANCES,
    HUB_ROUTE_SENSOR_CAL, /**< T/RH sensor offset (settings → System) */
    HUB_ROUTE_STANDBY,
    HUB_ROUTE_COUNT
} hub_route_t;

void hub_ui_init(void);
void hub_ui_go(hub_route_t route);
/** Pop navigation history (settings detail → settings → …). */
void hub_ui_back(void);
/** Rebuild current route (after model mutation). Keeps scroll position. */
void hub_ui_refresh(void);
/**
 * Soft notify for bus/health: show toast without tearing edit pages.
 * On ROOM_EDIT / ROOM_MB_EDIT only overlays toast; otherwise full refresh.
 */
void hub_ui_notify_soft(void);
hub_route_t hub_ui_route(void);
/** Screen root (for overlays that must not tear the page underneath). */
lv_obj_t *hub_ui_root(void);
/** Change route id without rebuilding (overlay open/close). */
void hub_ui_set_route(hub_route_t route);
/** True while widget / Modbus editors are open (avoid full-tree rebuild). */
bool hub_ui_route_is_editing(void);
void hub_nav_attach(lv_obj_t *root);

/**
 * Per-theme pack owns ALL pages (no shared hub_pages).
 * Implemented under ui/themes/<id>/ as a multi-file pack per theme.
 */
void hub_theme_build(lv_obj_t *parent, hub_route_t route);

lv_obj_t *hub_shell_create(lv_obj_t *parent, const char *title, bool show_back);
lv_obj_t *hub_shell_body(lv_obj_t *shell);
void hub_shell_set_dots(lv_obj_t *shell, int count, int active);

#ifdef __cplusplus
}
#endif
