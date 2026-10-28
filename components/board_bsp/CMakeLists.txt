# Open-source board BSP: RGB LCD / GT911 / NCA9555 / CH390 / backlight / beep.
# ethernet .c references ui_runtime / wifi_bemfa / app_features (stubs in main).
get_filename_component(CUSTOMER_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

idf_component_register(
    SRCS
        "src/lv_mem_psram_custom.c"
        "src/board_io_expander.c"
        "src/withthewind_board_lvgl_init.c"
        "src/board_ethernet_ch390.c"
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "${CUSTOMER_SDK_ROOT}/main/hub_ui/stubs"
        "${CUSTOMER_SDK_ROOT}/main/app"
    REQUIRES
        esp_driver_i2c
        esp_driver_gpio
        esp_driver_spi
        esp_driver_ledc
        esp_eth
        esp_netif
        esp_event
        espressif__esp_io_expander
        espressif__esp_io_expander_tca95xx_16bit
        espressif__esp_lvgl_port
        espressif__esp_lcd_panel_io_additions
        espressif__esp_lcd_st7701
        espressif__esp_lcd_touch_gt911
        espressif__ch390
        lvgl__lvgl
        heap
        esp_mm
        cloud_wifi
)
