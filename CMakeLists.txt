cmake_minimum_required(VERSION 3.19)

# Customer SDK — fully open-source (board pins + screen profile included).
set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/config/sdkconfig.common.defaults"
    "${CMAKE_CURRENT_LIST_DIR}/config/sdkconfig.lvgl.defaults"
    "${CMAKE_CURRENT_LIST_DIR}/config/sdkconfig.board.395hsd.defaults"
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults"
)

# Re-pin managed esp_lvgl_port -> lvgl 8.4.* after component downloads (IDF 5.5).
# Wide ">=8,<10" makes component-manager evaluate LVGL9 optional deps and fail.
execute_process(
    COMMAND python "${CMAKE_CURRENT_LIST_DIR}/tools/pin_esp_lvgl_port_lvgl.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    RESULT_VARIABLE _pin_lvgl_rc
)
if(NOT _pin_lvgl_rc EQUAL 0)
    message(WARNING "pin_esp_lvgl_port_lvgl.py failed (rc=${_pin_lvgl_rc})")
endif()

# board_display_present_fb needs lvgl_port_disp_wait_rgb_trans_done (local LVGL8 patch).
execute_process(
    COMMAND python "${CMAKE_CURRENT_LIST_DIR}/tools/patch_esp_lvgl_port_wait_rgb.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    RESULT_VARIABLE _patch_wait_rgb_rc
)
if(NOT _patch_wait_rgb_rc EQUAL 0)
    message(WARNING "patch_esp_lvgl_port_wait_rgb.py failed (rc=${_patch_wait_rgb_rc})")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_options(-Wno-format)
project(lvgl_customer_sdk)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/main/app_ui_theme_select.h")

include("${CMAKE_CURRENT_LIST_DIR}/cmake/lvgl_psram_heap.cmake")
lvgl_psram_heap_force_link()

spiffs_create_partition_image(storage spiffs_image FLASH_IN_PROJECT)
