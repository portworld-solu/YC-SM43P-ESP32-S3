/** Slate - router + app_ui_start */
#include "theme_local.h"
#include "hub_ui.h"
#include "hub_room_ui.h"
#include "hub_device_ui.h"
#include "hub_pages_common.h"
#include "hub_pages_life.h"
#include "app_ui.h"

void hub_theme_build(lv_obj_t *parent, hub_route_t route)
{
    switch (route) {
    case HUB_ROUTE_HOME: build_home(parent); break;
    case HUB_ROUTE_ROOM: hub_build_room_page(parent); break;
    case HUB_ROUTE_ROOM_EDIT: hub_build_room_edit(parent); break;
    case HUB_ROUTE_ROOM_MB_EDIT: hub_build_room_mb_edit(parent); break;
    case HUB_ROUTE_APPLIANCES: hub_build_appliances(parent); break;
    case HUB_ROUTE_SCENES: hub_build_scenes(parent); break;
    case HUB_ROUTE_ENERGY: hub_build_energy(parent); break;
    case HUB_ROUTE_GATEWAY: hub_build_gateway(parent); break;
    case HUB_ROUTE_SETTINGS: hub_build_settings(parent); break;
    case HUB_ROUTE_NETWORK: hub_build_network(parent); break;
    case HUB_ROUTE_SECURITY: hub_build_security(parent); break;
    case HUB_ROUTE_SCHEDULE: hub_build_schedule(parent); break;
    case HUB_ROUTE_SENSOR_CAL: hub_build_sensor_cal(parent); break;
    case HUB_ROUTE_HVAC: hub_build_hvac(parent); break;
    case HUB_ROUTE_POINTS: hub_build_points(parent); break;
    case HUB_ROUTE_STANDBY: hub_build_standby(parent); break;
    default: build_home(parent); break;
    }
}

void app_ui_start(void)
{
    hub_ui_init();
}