# Open-source Wi-Fi station + Bemfa MQTT client.
# Headers: main/hub_ui/stubs (ui.h, wifi_ui_font.h, ui_runtime.h).
# Definitions for SquareLine/font hooks MUST be in this component — see
# src/ui_compat_stubs.c (link order: main.a is scanned before cloud_wifi pulls
# wifi_management.o, so stubs in libmain.a never get extracted).
get_filename_component(CUSTOMER_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

idf_component_register(
    SRCS
        "src/wifi_management.c"
        "src/wifi_bemfa_client.c"
        "src/ui_compat_stubs.c"
        "src/wifi_ui_font_stub.c"
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "${CUSTOMER_SDK_ROOT}/main/hub_ui/stubs"
        "${CUSTOMER_SDK_ROOT}/main/app"
        "${CUSTOMER_SDK_ROOT}/main/gui"
    REQUIRES
        esp_wifi
        esp_coex
        esp_netif
        esp_event
        nvs_flash
        mqtt
        freertos
        lvgl__lvgl
        gui_task
        bt_ctrl
        esp_timer
)
