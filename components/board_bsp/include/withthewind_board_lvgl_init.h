#pragma once

#include "board_io_expander.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lvgl_port.h"

/** LCD_RST_L on NCA9555 port 0 bit 2 (when CONFIG_BOARD_IO_EXPANDER_ENABLE). */
#define BOARD_LCD_RESET_EXP_PIN     BOARD_IO_EXP_LCD_RST

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t board_display_start_with_lvgl_cfg(const lvgl_port_cfg_t *lvgl_cfg);

/** Shared TP_SCL/TP_SDA bus (GT911 + NCA9555 + external I2C sensors). NULL before display init. */
i2c_master_bus_handle_t board_i2c_get_handle(void);

esp_err_t board_backlight_init(void);
/** 0..100; expander software-PWM path quantizes to band steps; MCU LEDC is 1% steps. */
esp_err_t board_backlight_set(int brightness_percent);
/** Nearest stable brightness band (see CONFIG_BOARD_BACKLIGHT_PWM_BAND_STEP_PCT). */
int board_backlight_quantize_percent(int brightness_percent);
/** Clamp UI percent: 0 stays off; otherwise >= CONFIG_BOARD_BACKLIGHT_MIN_PERCENT. */
int board_backlight_clamp_percent(int brightness_percent);
esp_err_t board_backlight_on(void);
esp_err_t board_backlight_off(void);

esp_err_t board_beep_set(int on);

/**
 * Push a frame buffer to RGB scan-out when flush did not already do so.
 * Normal direct_mode refresh: esp_lvgl_port flush (is_last) already calls draw_bitmap.
 */
void board_display_present_fb(const void *rendered_fb);

/** Same as board_display_present_fb(NULL). */
void board_display_present_frame(void);

#ifdef __cplusplus
}
#endif

