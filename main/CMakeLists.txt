include("${CMAKE_CURRENT_LIST_DIR}/cmake/resolve_ui_theme.cmake")

set(UI_THEMES_ROOT "${CMAKE_CURRENT_LIST_DIR}/../ui/themes")
set(UI_DIR "${UI_THEMES_ROOT}/${UI_THEME_ID}")
if(NOT EXISTS "${UI_DIR}")
    message(FATAL_ERROR
        "UI theme directory missing: ${UI_DIR}\n"
        "Customer SDK ships hub themes under ui/themes/{slate,sand,...}.\n"
        "Set APP_UI_THEME_ID to a hub theme in main/app_ui_theme_select.h "
        "(APP_UI_THEME_DEFAULT is not supported here).")
endif()
message(STATUS "UI theme dir: ${UI_DIR}")

set(MAIN_REQUIRES
        esp_wifi
        esp_coex
        esp_netif
        esp_eth
        esp_event
        esp_pm
        esp_http_client
        esp_timer
        esp_driver_spi
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_uart
        espressif__ch390
        espressif__esp_lvgl_port
        espressif__esp_lcd_panel_io_additions
        espressif__esp_lcd_st7701
        espressif__esp_lcd_touch_gt911
        espressif__esp_io_expander
        board_bsp
        hub_core
        cloud_wifi
        bt_ctrl
        lvgl__lvgl
        mqtt
        nvs_flash
        bt
        spiffs
        gui_task
        app_health
)

# --- Open-layer sources by concern ---
set(MAIN_SRCS
        main.c
        # Peripherals / bus / system helpers
        app/app_low_power.c
        app/ui_assets_warm.c
        app/app_beep.c
        app/app_coexist.c
        app/app_gpio_out.c
        app/app_wifi_util.c
        app/app_rs485.c
        app/app_rs485_bench.c
        app/app_modbus_rtu.c
        app/app_modbus_map.c
        app/app_bus_bridge.c
        # Board sensors (customer-editable calib)
        board/aht20.c
        # Background I/O (AHT20 poll, NVS) — keeps LVGL task free
        gui/ui_bg_task.c
        # Fonts
        "fonts/lv_font_source_han_14_cjk3500.c"
        "fonts/lv_font_source_han_ambient_time_150.c"
        "fonts/lv_font_source_han_screen1_time_100.c"
        "fonts/lv_font_hub_cjk_extra.c"
)

set(MAIN_INCLUDE_DIRS
        .
        board
        app
        gui
        fonts
)

if(NOT UI_THEME_IS_HUB)
    message(FATAL_ERROR "Customer SDK ships hub themes only. Set APP_UI_THEME_* hub id.")
endif()

# Hub UI: physical layout under hub_ui/{core,pages,features,stubs}
# Include dirs keep historical #include "hub_xxx.h" working.
list(APPEND MAIN_SRCS
        hub_ui/core/hub_ui.c
        hub_ui/core/hub_shell.c
        hub_ui/core/hub_nav.c
        hub_ui/core/hub_font.c
        hub_ui/core/hub_icons.c
        hub_ui/core/hub_i18n.c
        hub_ui/pages/hub_room_ui.c
        hub_ui/pages/hub_device_ui.c
        hub_ui/pages/hub_pages_common.c
        hub_ui/pages/hub_pages_life.c
        hub_ui/pages/hub_standby.c
        hub_ui/features/hub_climate_offset.c
        hub_ui/stubs/ui_runtime_stubs.c
        # ui_compat / wifi_ui_font stubs live in cloud_wifi (same .a as refs; see that CMakeLists)
        # Active theme pack (self-contained pages)
        "${UI_DIR}/palette.c"
        "${UI_DIR}/theme_local.c"
        "${UI_DIR}/home.c"
        "${UI_DIR}/pages_room.c"
        "${UI_DIR}/pages_scenes.c"
        "${UI_DIR}/pages_ops.c"
        "${UI_DIR}/pages_life.c"
        "${UI_DIR}/boot.c"
)
if(CONFIG_APP_USE_SPIFFS_UI_ASSETS)
    list(APPEND MAIN_SRCS "app/app_spiffs.c")
endif()
list(APPEND MAIN_INCLUDE_DIRS
        hub_ui
        hub_ui/core
        hub_ui/pages
        hub_ui/features
        hub_ui/stubs
        "${UI_DIR}"
)

idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS ${MAIN_INCLUDE_DIRS}
    REQUIRES ${MAIN_REQUIRES}
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE APP_UI_THEME_HUB=1)

idf_component_get_property(LVGL_DIR lvgl__lvgl COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${LVGL_DIR}")

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
idf_component_get_property(BOARD_BSP_DIR board_bsp COMPONENT_DIR)
target_include_directories(${LVGL_LIB} PRIVATE "${BOARD_BSP_DIR}/include")
target_compile_definitions(
    ${LVGL_LIB}
    PRIVATE
        LV_LVGL_H_INCLUDE_SIMPLE
        LV_MEM_CUSTOM=1
        LV_MEM_CUSTOM_INCLUDE="lv_mem_psram_custom.h"
        LV_MEM_CUSTOM_ALLOC=lv_mem_psram_malloc
        LV_MEM_CUSTOM_FREE=lv_mem_psram_free
        LV_MEM_CUSTOM_REALLOC=lv_mem_psram_realloc
)
target_compile_definitions(lvgl_port_lib PRIVATE LV_MEM_CUSTOM=1)
