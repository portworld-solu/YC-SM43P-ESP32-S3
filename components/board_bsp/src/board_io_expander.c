#include "board_io_expander.h"

#include <inttypes.h>

#include "sdkconfig.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_io_expander.h"
#include "esp_io_expander_tca95xx_16bit.h"

/*
 * NCA9555 is register-compatible with PCA9555 / TCA9555, so the espressif
 * `esp_io_expander_tca95xx_16bit` component drives it directly (no fork needed):
 *   INPUT  0x00/0x01, OUTPUT 0x02/0x03, DIRECTION 0x06/0x07 (auto-increment).
 * 7-bit address with A2..A0 = GND is 0x20
 * (== ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000 in the component header).
 */

static const char *TAG = "board_io_exp";

#define BOARD_IO_EXPANDER_I2C_SCL_GPIO GPIO_NUM_7
#define BOARD_IO_EXPANDER_I2C_SDA_GPIO GPIO_NUM_15
#define BOARD_IO_EXPANDER_I2C_PORT     1

static i2c_master_bus_handle_t s_owned_i2c_bus = NULL;
static esp_io_expander_handle_t s_io_expander = NULL;

static esp_io_expander_pin_num_t board_exp_pin_from_cfg(int pin_index)
{
    return BOARD_IO_EXP_PIN_IDX(pin_index);
}

static esp_err_t board_io_expander_ensure_i2c(i2c_master_bus_handle_t *out_bus)
{
    if (*out_bus != NULL) {
        return ESP_OK;
    }
    if (s_owned_i2c_bus != NULL) {
        *out_bus = s_owned_i2c_bus;
        return ESP_OK;
    }

    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = BOARD_IO_EXPANDER_I2C_SDA_GPIO,
        .scl_io_num = BOARD_IO_EXPANDER_I2C_SCL_GPIO,
        .i2c_port = BOARD_IO_EXPANDER_I2C_PORT,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_owned_i2c_bus), TAG, "i2c_new_master_bus");
    *out_bus = s_owned_i2c_bus;
    return ESP_OK;
}

#if CONFIG_BOARD_IO_EXPANDER_ENABLE

static esp_err_t board_io_expander_configure_pins(void)
{
    /* Collect every push-pull output into one mask so the whole direction
     * register is programmed in a single I2C transaction (the tca95xx driver
     * caches state and only writes the changed bytes). Fewer transfers = less
     * exposure to the board's weak I2C pull-ups. */
    uint32_t outputs = BOARD_IO_EXP_LCD_CS | BOARD_IO_EXP_TP_RST | BOARD_IO_EXP_LCD_RST |
                       BOARD_IO_EXP_LCD_SDA | BOARD_IO_EXP_LCD_SCL | BOARD_IO_EXP_CH390_CS |
                       BOARD_IO_EXP_CH390_RST |
                       board_exp_pin_from_cfg(CONFIG_BOARD_GPIO_OUT_EXPANDER_PIN);

#if CONFIG_BOARD_BACKLIGHT_VIA_IO_EXPANDER
    const uint32_t bl = board_exp_pin_from_cfg(CONFIG_BOARD_BACKLIGHT_EXPANDER_PIN);
    outputs |= bl;
#endif
#if CONFIG_BOARD_BEEP_VIA_IO_EXPANDER
    outputs |= board_exp_pin_from_cfg(CONFIG_BOARD_BEEP_EXPANDER_PIN);
#endif

    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(s_io_expander, outputs, IO_EXPANDER_OUTPUT), TAG, "out dir");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(s_io_expander, BOARD_IO_EXP_RTC_INT, IO_EXPANDER_INPUT), TAG, "rtc in");

    /* Group control lines by idle level so all idle states are applied in just
     * two writes. SDA/SCL are intentionally left to the 3-wire SPI driver. */
    uint32_t high_pins = BOARD_IO_EXP_LCD_CS | BOARD_IO_EXP_CH390_CS | BOARD_IO_EXP_CH390_RST |
                         BOARD_IO_EXP_LCD_RST | BOARD_IO_EXP_TP_RST;
    uint32_t low_pins = 0;

#if CONFIG_BOARD_BACKLIGHT_VIA_IO_EXPANDER
#if CONFIG_BOARD_BACKLIGHT_ACTIVE_HIGH
    low_pins |= bl;  /* backlight off == low */
#else
    high_pins |= bl; /* backlight off == high */
#endif
#endif
#if CONFIG_BOARD_BEEP_VIA_IO_EXPANDER
    low_pins |= board_exp_pin_from_cfg(CONFIG_BOARD_BEEP_EXPANDER_PIN); /* beep off */
#endif

    if (high_pins != 0) {
        ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, high_pins, 1), TAG, "idle high");
    }
    if (low_pins != 0) {
        ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, low_pins, 0), TAG, "idle low");
    }

    /* Reliability check: the NCA9555 INPUT register always reflects the real pad
     * level, so reading it back proves the idle levels physically took effect
     * (catches weak pull-ups / a missing/half-driven I2C bus). */
    uint32_t rb = 0;
    if (esp_io_expander_get_level(s_io_expander, high_pins | low_pins, &rb) == ESP_OK) {
        if ((rb & high_pins) != high_pins || (rb & low_pins) != 0) {
            ESP_LOGW(TAG, "idle readback mismatch: rb=0x%04" PRIx32 " expect high=0x%04" PRIx32 " low=0x%04" PRIx32,
                     rb, high_pins, low_pins);
        }
    }

    return ESP_OK;
}

#endif /* CONFIG_BOARD_IO_EXPANDER_ENABLE */

esp_err_t board_io_expander_init(i2c_master_bus_handle_t i2c_bus)
{
#if !CONFIG_BOARD_IO_EXPANDER_ENABLE
    (void)i2c_bus;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_io_expander != NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(board_io_expander_ensure_i2c(&i2c_bus), TAG, "I2C bus");

    /* NCA9555/TCA9555/PCA9555 strap A2..A0 → 7-bit address 0x20..0x27. */
    if (CONFIG_BOARD_IO_EXPANDER_I2C_ADDRESS < ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000 ||
        CONFIG_BOARD_IO_EXPANDER_I2C_ADDRESS > ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_111) {
        ESP_LOGW(TAG, "addr 0x%02x outside NCA9555 range 0x20..0x27 (check A0/A1/A2 straps)",
                 CONFIG_BOARD_IO_EXPANDER_I2C_ADDRESS);
    }

    esp_err_t probe = i2c_master_probe(i2c_bus, CONFIG_BOARD_IO_EXPANDER_I2C_ADDRESS, 100);
    if (probe != ESP_OK) {
        ESP_LOGE(TAG, "NCA9555/TCA9555 not found at 0x%02x", CONFIG_BOARD_IO_EXPANDER_I2C_ADDRESS);
        return probe;
    }

    ESP_RETURN_ON_ERROR(
        esp_io_expander_new_i2c_tca95xx_16bit(i2c_bus, CONFIG_BOARD_IO_EXPANDER_I2C_ADDRESS, &s_io_expander),
        TAG, "new expander");

    ESP_RETURN_ON_ERROR(board_io_expander_configure_pins(), TAG, "configure pins");

#if CONFIG_BOARD_BACKLIGHT_VIA_IO_EXPANDER
    ESP_LOGI(TAG, "NCA9555 ready @0x%02x (P0: LCD/TP/CH390, P1.%d: BL PWM %dHz step=%d%%)",
             CONFIG_BOARD_IO_EXPANDER_I2C_ADDRESS,
             CONFIG_BOARD_BACKLIGHT_EXPANDER_PIN - 8,
             CONFIG_BOARD_BACKLIGHT_EXPANDER_PWM_FREQ_HZ,
             CONFIG_BOARD_BACKLIGHT_PWM_BAND_STEP_PCT);
#elif CONFIG_BOARD_BEEP_VIA_IO_EXPANDER
    ESP_LOGI(TAG, "NCA9555 ready @0x%02x (P0: LCD/TP/CH390, P1.%d: BEEP)",
             CONFIG_BOARD_IO_EXPANDER_I2C_ADDRESS,
             CONFIG_BOARD_BEEP_EXPANDER_PIN - 8);
#else
    ESP_LOGI(TAG, "NCA9555 ready @0x%02x (P0: LCD/TP/CH390)",
             CONFIG_BOARD_IO_EXPANDER_I2C_ADDRESS);
#endif
    return ESP_OK;
#endif
}

i2c_master_bus_handle_t board_io_expander_get_i2c_bus(void)
{
    return s_owned_i2c_bus;
}

#if CONFIG_BOARD_IO_EXPANDER_ENABLE && CONFIG_BOARD_BACKLIGHT_VIA_IO_EXPANDER

typedef enum {
    BL_PWM_PHASE_ON = 0,
    BL_PWM_PHASE_OFF = 1,
} bl_pwm_phase_t;

static esp_timer_handle_t s_bl_pwm_timer = NULL;
static bl_pwm_phase_t s_bl_pwm_phase = BL_PWM_PHASE_OFF;
static int s_bl_pwm_band_pct = 0;
static int s_bl_pwm_logged_band = -1;
static bool s_bl_pwm_active = false;
static uint32_t s_bl_period_off_us = 0;

static int board_bl_band_step_pct(void)
{
    int step = CONFIG_BOARD_BACKLIGHT_PWM_BAND_STEP_PCT;
    if (step < 5) {
        step = 5;
    } else if (step > 25) {
        step = 25;
    }
    /* 100 必须能整除步进，否则占空比无法对齐整周期。 */
    if ((100 % step) != 0) {
        if (step <= 10) {
            step = 10;
        } else if (step <= 20) {
            step = 20;
        } else {
            step = 25;
        }
    }
    return step;
}

static int board_bl_clamp_ui_percent(int brightness_percent)
{
    if (brightness_percent <= 0) {
        return 0;
    }
    if (brightness_percent > 100) {
        brightness_percent = 100;
    }
    if (brightness_percent < CONFIG_BOARD_BACKLIGHT_MIN_PERCENT) {
        brightness_percent = CONFIG_BOARD_BACKLIGHT_MIN_PERCENT;
    }
    return brightness_percent;
}

int board_io_expander_backlight_quantize(int brightness_percent)
{
    brightness_percent = board_bl_clamp_ui_percent(brightness_percent);
    const int step = board_bl_band_step_pct();

    if (brightness_percent <= 0) {
        return 0;
    }
    if (brightness_percent >= 100) {
        return 100;
    }
    return ((brightness_percent + step / 2) / step) * step;
}

static esp_io_expander_pin_num_t board_bl_exp_pin(void)
{
    return board_exp_pin_from_cfg(CONFIG_BOARD_BACKLIGHT_EXPANDER_PIN);
}

static uint32_t board_bl_pwm_period_us(void)
{
    const int hz = CONFIG_BOARD_BACKLIGHT_EXPANDER_PWM_FREQ_HZ;
    return (uint32_t)(1000000 / (hz > 0 ? hz : 200));
}

static uint8_t board_bl_physical_level(bool on)
{
#if CONFIG_BOARD_BACKLIGHT_ACTIVE_HIGH
    return on ? 1u : 0u;
#else
    return on ? 0u : 1u;
#endif
}

static esp_err_t board_bl_apply_level(bool on)
{
    if (s_io_expander == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_io_expander_set_level(s_io_expander, board_bl_exp_pin(), board_bl_physical_level(on));
}

static void board_bl_pwm_stop(void)
{
    if (s_bl_pwm_timer != NULL) {
        (void)esp_timer_stop(s_bl_pwm_timer);
    }
}

static void board_bl_pwm_timer_cb(void *arg);

static esp_err_t board_bl_pwm_timer_ensure(void)
{
    if (s_bl_pwm_timer != NULL) {
        return ESP_OK;
    }
    const esp_timer_create_args_t args = {
        .callback = board_bl_pwm_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "bl_pwm",
        /* 不可丢弃到期事件，否则 I2C PWM 相位错乱 → 肉眼可见闪烁。 */
        .skip_unhandled_events = false,
    };
    return esp_timer_create(&args, &s_bl_pwm_timer);
}

static void board_bl_pwm_arm_in(int64_t delay_us)
{
    if (s_bl_pwm_timer == NULL) {
        return;
    }
    const uint32_t period = board_bl_pwm_period_us();
    const int64_t min_us = (int64_t)(period / 40U);
    if (delay_us < min_us) {
        delay_us = min_us;
    }
    (void)esp_timer_stop(s_bl_pwm_timer);
    (void)esp_timer_start_once(s_bl_pwm_timer, delay_us);
}

/** 固定频率 + 档位百分比 → 整周期 ON/OFF 时间（无 dither、无任意占空比）。 */
static void board_bl_pwm_band_timing(int band_pct, uint32_t *on_us, uint32_t *off_us)
{
    const uint32_t period = board_bl_pwm_period_us();
    const int pct = board_io_expander_backlight_quantize(band_pct);

    if (pct <= 0) {
        *on_us = 0;
        *off_us = period;
        return;
    }
    if (pct >= 100) {
        *on_us = period;
        *off_us = 0;
        return;
    }

    *on_us = (period * (uint32_t)pct) / 100U;
    *off_us = period - *on_us;
}

static void board_bl_pwm_begin_period(void)
{
    const int band = s_bl_pwm_band_pct;
    uint32_t on_us = 0;
    uint32_t off_us = 0;

    board_bl_pwm_band_timing(band, &on_us, &off_us);
    s_bl_period_off_us = off_us;

    if (on_us == 0) {
        (void)board_bl_apply_level(false);
        s_bl_pwm_phase = BL_PWM_PHASE_OFF;
        board_bl_pwm_arm_in((int64_t)off_us);
        return;
    }

    (void)board_bl_apply_level(true);
    s_bl_pwm_phase = BL_PWM_PHASE_ON;
    board_bl_pwm_arm_in((int64_t)on_us);
}

static void board_bl_pwm_timer_cb(void *arg)
{
    (void)arg;

    const int band = s_bl_pwm_band_pct;
    if (band <= 0) {
        (void)board_bl_apply_level(false);
        s_bl_pwm_active = false;
        return;
    }
    if (band >= 100) {
        (void)board_bl_apply_level(true);
        s_bl_pwm_active = false;
        return;
    }

    if (s_bl_pwm_phase == BL_PWM_PHASE_ON) {
        (void)board_bl_apply_level(false);
        s_bl_pwm_phase = BL_PWM_PHASE_OFF;

        if (s_bl_period_off_us == 0) {
            board_bl_pwm_begin_period();
        } else {
            board_bl_pwm_arm_in((int64_t)s_bl_period_off_us);
        }
    } else {
        board_bl_pwm_begin_period();
    }
}

static esp_err_t board_bl_pwm_apply_percent(int brightness_percent)
{
    ESP_RETURN_ON_ERROR(board_bl_pwm_timer_ensure(), TAG, "bl pwm timer");

    const int band = board_io_expander_backlight_quantize(brightness_percent);
    const int prev_band = s_bl_pwm_band_pct;
    s_bl_pwm_band_pct = band;

    if (band <= 0) {
        board_bl_pwm_stop();
        s_bl_pwm_active = false;
        ESP_RETURN_ON_ERROR(board_bl_apply_level(false), TAG, "bl off");
        return ESP_OK;
    }
    if (band >= 100) {
        board_bl_pwm_stop();
        s_bl_pwm_active = false;
        ESP_RETURN_ON_ERROR(board_bl_apply_level(true), TAG, "bl full");
        return ESP_OK;
    }

    /* 同一档位内不重启 PWM，跨档时才重同步相位。 */
    if (s_bl_pwm_active && band == prev_band) {
        return ESP_OK;
    }

    s_bl_pwm_active = true;
    board_bl_pwm_stop();
    board_bl_pwm_begin_period();
    return ESP_OK;
}

#endif /* expander + BL */

esp_io_expander_handle_t board_io_expander_get(void)
{
    return s_io_expander;
}

esp_err_t board_io_expander_lcd_reset_pulse(void)
{
#if !CONFIG_BOARD_IO_EXPANDER_ENABLE
    return ESP_ERR_NOT_SUPPORTED;
#else
    ESP_RETURN_ON_FALSE(s_io_expander != NULL, ESP_ERR_INVALID_STATE, TAG, "not init");
    /* LCD_RST_L + TP_RST (active low) — same sequence as esp32_s3_touch_lcd_4 BSP. */
    const uint32_t rst_pins = BOARD_IO_EXP_LCD_RST | BOARD_IO_EXP_TP_RST;
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, rst_pins, 0), TAG, "lcd+tp rst assert");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, rst_pins, 1), TAG, "lcd+tp rst release");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
#endif
}

esp_err_t board_io_expander_lcd_reset_sequence(void)
{
#if !CONFIG_BOARD_IO_EXPANDER_ENABLE
    return ESP_ERR_NOT_SUPPORTED;
#else
    ESP_RETURN_ON_ERROR(board_io_expander_lcd_reset_pulse(), TAG, "lcd rst");
    ESP_RETURN_ON_FALSE(s_io_expander != NULL, ESP_ERR_INVALID_STATE, TAG, "not init");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, BOARD_IO_EXP_TP_RST, 0), TAG, "tp rst assert");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, BOARD_IO_EXP_TP_RST, 1), TAG, "tp rst release");
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
#endif
}

esp_err_t board_io_expander_touch_reset_pulse(void)
{
#if !CONFIG_BOARD_IO_EXPANDER_ENABLE
    return ESP_ERR_NOT_SUPPORTED;
#else
    ESP_RETURN_ON_FALSE(s_io_expander != NULL, ESP_ERR_INVALID_STATE, TAG, "not init");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, BOARD_IO_EXP_TP_RST, 0), TAG, "tp rst");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, BOARD_IO_EXP_TP_RST, 1), TAG, "tp run");
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
#endif
}

esp_err_t board_io_expander_spi_lines_selftest(void)
{
#if !CONFIG_BOARD_IO_EXPANDER_ENABLE
    return ESP_ERR_NOT_SUPPORTED;
#else
    ESP_RETURN_ON_FALSE(s_io_expander != NULL, ESP_ERR_INVALID_STATE, TAG, "not init");

    const uint32_t lines = BOARD_IO_EXP_LCD_CS | BOARD_IO_EXP_LCD_SCL | BOARD_IO_EXP_LCD_SDA;
    /* Make sure the three LCD SPI lines are outputs. */
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(s_io_expander, lines, IO_EXPANDER_OUTPUT), TAG, "spi dir");

    bool ok = true;
    /* PCA/TCA/NCA95xx INPUT register always reflects the actual pad level, even
     * for pins configured as outputs. So driving a known pattern and reading the
     * input back proves the NCA9555 output truly reaches the physical pin. */
    for (int phase = 0; phase < 2; phase++) {
        const uint8_t level = (phase == 0) ? 0 : 1;
        ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, lines, level), TAG, "spi set");
        esp_rom_delay_us(50);

        uint32_t rb = 0;
        ESP_RETURN_ON_ERROR(esp_io_expander_get_level(s_io_expander, lines, &rb), TAG, "spi readback");

        const int cs  = (rb & BOARD_IO_EXP_LCD_CS)  ? 1 : 0;
        const int scl = (rb & BOARD_IO_EXP_LCD_SCL) ? 1 : 0;
        const int sda = (rb & BOARD_IO_EXP_LCD_SDA) ? 1 : 0;
        const bool match = (cs == level) && (scl == level) && (sda == level);
        ok = ok && match;
        ESP_LOGI(TAG, "SPI selftest drive=%d  read CS(P0.0)=%d SCL(P0.4)=%d SDA(P0.3)=%d  %s",
                 level, cs, scl, sda, match ? "OK" : "MISMATCH!");
    }

    if (!ok) {
        ESP_LOGE(TAG, "SPI lines mismatch: pin not reaching panel (short/load/wrong pin/weak I2C). "
                      "Check NCA9555 P0.0/P0.3/P0.4 wiring to ST7701 CS/SDA/SCL.");
    }

    /* Restore idle: CS high (inactive), SCL/SDA low (SPI mode 0 idle). */
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, BOARD_IO_EXP_LCD_CS, 1), TAG, "cs idle");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, BOARD_IO_EXP_LCD_SCL | BOARD_IO_EXP_LCD_SDA, 0),
                        TAG, "scl/sda idle");
    return ok ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
#endif
}

esp_err_t board_io_expander_ch390_reset_pulse(void)
{
#if !CONFIG_BOARD_IO_EXPANDER_ENABLE
    return ESP_ERR_NOT_SUPPORTED;
#else
    ESP_RETURN_ON_FALSE(s_io_expander != NULL, ESP_ERR_INVALID_STATE, TAG, "not init");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, BOARD_IO_EXP_CH390_RST, 0), TAG, "rst");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, BOARD_IO_EXP_CH390_RST, 1), TAG, "run");
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
#endif
}

esp_err_t board_io_expander_ch390_cs_set(bool selected)
{
#if !CONFIG_BOARD_IO_EXPANDER_ENABLE
    (void)selected;
    return ESP_ERR_NOT_SUPPORTED;
#else
    ESP_RETURN_ON_FALSE(s_io_expander != NULL, ESP_ERR_INVALID_STATE, TAG, "not init");
    return esp_io_expander_set_level(s_io_expander, BOARD_IO_EXP_CH390_CS, selected ? 0 : 1);
#endif
}

esp_err_t board_io_expander_backlight_set(int brightness_percent)
{
#if !CONFIG_BOARD_IO_EXPANDER_ENABLE || !CONFIG_BOARD_BACKLIGHT_VIA_IO_EXPANDER
    (void)brightness_percent;
    return ESP_ERR_NOT_SUPPORTED;
#else
    ESP_RETURN_ON_FALSE(s_io_expander != NULL, ESP_ERR_INVALID_STATE, TAG, "not init");

    if (brightness_percent > 100) {
        brightness_percent = 100;
    } else if (brightness_percent < 0) {
        brightness_percent = 0;
    }

    brightness_percent = board_bl_clamp_ui_percent(brightness_percent);

#if CONFIG_BOARD_BACKLIGHT_INVERT_PERCENT
    brightness_percent = 100 - brightness_percent;
#endif

    esp_err_t err = board_bl_pwm_apply_percent(brightness_percent);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BL PWM failed pct=%d err=%s", brightness_percent, esp_err_to_name(err));
        return err;
    }

    if (brightness_percent != s_bl_pwm_logged_band) {
        const int band = board_io_expander_backlight_quantize(brightness_percent);
        s_bl_pwm_logged_band = band;
        uint32_t on_us = 0;
        uint32_t off_us = 0;
        board_bl_pwm_band_timing(band, &on_us, &off_us);
        ESP_LOGI(TAG, "BL P1.%d band=%d%% on=%luus off=%luus @%dHz step=%d%%",
                 CONFIG_BOARD_BACKLIGHT_EXPANDER_PIN - 8,
                 band,
                 (unsigned long)on_us,
                 (unsigned long)off_us,
                 CONFIG_BOARD_BACKLIGHT_EXPANDER_PWM_FREQ_HZ,
                 board_bl_band_step_pct());
    }
    return ESP_OK;
#endif
}

esp_err_t board_io_expander_gpio_out_set(bool high)
{
#if !CONFIG_BOARD_IO_EXPANDER_ENABLE
    (void)high;
    return ESP_ERR_NOT_SUPPORTED;
#else
    ESP_RETURN_ON_FALSE(s_io_expander != NULL, ESP_ERR_INVALID_STATE, TAG, "not init");
    const esp_io_expander_pin_num_t pin = board_exp_pin_from_cfg(CONFIG_BOARD_GPIO_OUT_EXPANDER_PIN);
    return esp_io_expander_set_level(s_io_expander, pin, high ? 1 : 0);
#endif
}

esp_err_t board_io_expander_beep_set(int on)
{
#if !CONFIG_BOARD_IO_EXPANDER_ENABLE || !CONFIG_BOARD_BEEP_VIA_IO_EXPANDER
    (void)on;
    return ESP_ERR_NOT_SUPPORTED;
#else
    ESP_RETURN_ON_FALSE(s_io_expander != NULL, ESP_ERR_INVALID_STATE, TAG, "not init");
    const esp_io_expander_pin_num_t beep = board_exp_pin_from_cfg(CONFIG_BOARD_BEEP_EXPANDER_PIN);
    return esp_io_expander_set_level(s_io_expander, beep, on ? 1 : 0);
#endif
}

bool board_io_expander_rtc_int_active(void)
{
#if !CONFIG_BOARD_IO_EXPANDER_ENABLE
    return false;
#else
    if (s_io_expander == NULL) {
        return false;
    }
    uint32_t levels = 0;
    if (esp_io_expander_get_level(s_io_expander, BOARD_IO_EXP_RTC_INT, &levels) != ESP_OK) {
        return false;
    }
    /* RTCIC_INT_L: active low */
    return (levels & BOARD_IO_EXP_RTC_INT) == 0;
#endif
}
