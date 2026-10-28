#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** AdaptiveGrid room page (FE HubRoomPage parity). */
void hub_build_room_page(lv_obj_t *parent);

/** Room widget editor (FE HubRoomEditPage). */
void hub_build_room_edit(lv_obj_t *parent);

/** Edit Modbus binding for one room widget slot. */
void hub_build_room_mb_edit(lv_obj_t *parent);

/** Select slot before navigating to HUB_ROUTE_ROOM_MB_EDIT. */
void hub_room_mb_edit_set_slot(int slot);

/**
 * Close Modbus overlay if open (no full-page rebuild).
 * @return true if an overlay was closed (caller should skip hub_ui_back rebuild).
 */
bool hub_room_mb_overlay_try_close(void);

/** Force-drop MB / type-picker overlays before route rebuild (prevents residual ghosts). */
void hub_room_mb_overlay_discard(void);

/** Appliance switches (FE TplApplianceGrid / Ink PUMP·VALVE target). */
void hub_build_appliances(lv_obj_t *parent);

/** Security with PIN-gated disarm. */
void hub_build_security(lv_obj_t *parent);

/** Show pending toast if any (call after page build). */
void hub_toast_present(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
