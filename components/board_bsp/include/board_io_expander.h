#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_io_expander.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * NCA9555 / TCA9555 pin map (matched to schematic net names)
 *
 * Port 0:
 *   P0.0 LCD_CS
 *   P0.1 TP_RST
 *   P0.2 LCD_RST_L
 *   P0.3 LCD_SDA
 *   P0.4 LCD_SCL
 *   P0.5 SCH390D_CS
 *   P0.6 RTCIC_INT_L (input, active low)
 *   P0.7 CH390D_RST
 *
 * Port 1:
 *   P1.0 GPIO_OUT
 *   P1.1 BEEP (active high, digital). Backlight moved to MCU GPIO4 LEDC.
 */
#define BOARD_IO_EXP_LCD_CS         IO_EXPANDER_PIN_NUM_0
#define BOARD_IO_EXP_TP_RST         IO_EXPANDER_PIN_NUM_1
#define BOARD_IO_EXP_LCD_RST        IO_EXPANDER_PIN_NUM_2
#define BOARD_IO_EXP_LCD_SDA        IO_EXPANDER_PIN_NUM_3
#define BOARD_IO_EXP_LCD_SCL        IO_EXPANDER_PIN_NUM_4
#define BOARD_IO_EXP_CH390_CS       IO_EXPANDER_PIN_NUM_5
#define BOARD_IO_EXP_RTC_INT        IO_EXPANDER_PIN_NUM_6
#define BOARD_IO_EXP_CH390_RST      IO_EXPANDER_PIN_NUM_7
#define BOARD_IO_EXP_GPIO_OUT       IO_EXPANDER_PIN_NUM_8
#define BOARD_IO_EXP_LCD_PWM        IO_EXPANDER_PIN_NUM_9  /* schematic net; now used as BEEP */
#define BOARD_IO_EXP_BEEP           BOARD_IO_EXP_LCD_PWM

/** Build pin mask from menuconfig pin index (0–15). */
#define BOARD_IO_EXP_PIN_IDX(n)     ((esp_io_expander_pin_num_t)(1ULL << (n)))

/**
 * @brief Init expander on TP_SCL/TP_SDA I2C bus (idempotent).
 * @param i2c_bus Shared bus handle, or NULL to create one on GPIO7/15.
 */
esp_err_t board_io_expander_init(i2c_master_bus_handle_t i2c_bus);

/**
 * @brief I2C bus owned/created by expander init(NULL), or NULL if display owns the bus.
 * Display init should adopt this when eth/other opened I2C1 first.
 */
i2c_master_bus_handle_t board_io_expander_get_i2c_bus(void);

esp_io_expander_handle_t board_io_expander_get(void);

/** LCD_RST_L only (NCA9555 P0.2): assert low then release. */
esp_err_t board_io_expander_lcd_reset_pulse(void);

/**
 * Drive LCD CS/SCL/SDA (P0.0/P0.4/P0.3) to a known pattern and read the pad
 * level back via the input register. Verifies the NCA9555 outputs physically
 * reach the ST7701. Returns ESP_OK only if every line matches the driven level.
 */
esp_err_t board_io_expander_spi_lines_selftest(void);

/** LCD_RST_L + TP_RST: assert low then release (before ST7701 / GT911). */
esp_err_t board_io_expander_lcd_reset_sequence(void);

/** Touch reset only (GT911) — optional extra pulse after LCD sequence. */
esp_err_t board_io_expander_touch_reset_pulse(void);

/** CH390D RST: low 20ms then high. */
esp_err_t board_io_expander_ch390_reset_pulse(void);

/** CH390 SPI chip-select, active when @p selected is true (CS low). */
esp_err_t board_io_expander_ch390_cs_set(bool selected);

/**
 * Legacy expander-software-PWM backlight (only if BOARD_BACKLIGHT_VIA_IO_EXPANDER).
 * New boards use MCU GPIO LEDC via board_backlight_set().
 */
esp_err_t board_io_expander_backlight_set(int brightness_percent);

/** Map arbitrary percent to nearest stable PWM band (same as backlight_set uses). */
int board_io_expander_backlight_quantize(int brightness_percent);

/** Port1 GPIO_OUT / misc output (index from Kconfig). */
esp_err_t board_io_expander_gpio_out_set(bool high);

/** Beep on expander pin (default P1.1) when CONFIG_BOARD_BEEP_VIA_IO_EXPANDER. */
esp_err_t board_io_expander_beep_set(int on);

/**
 * RTC interrupt line (RTCIC_INT_L): returns true when IRQ asserted (pin low).
 */
bool board_io_expander_rtc_int_active(void);

#ifdef __cplusplus
}
#endif
