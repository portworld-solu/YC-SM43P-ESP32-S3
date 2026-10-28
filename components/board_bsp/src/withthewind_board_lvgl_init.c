#include "withthewind_board_lvgl_init.h"
#include "board_io_expander.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_lvgl_port_disp.h"

#include "sdkconfig.h"

/* The original BSP provides CONFIG_BSP_* symbols via menuconfig.
 * Since we removed the BSP dependency, add safe fallbacks to keep this file
 * compiling even when some CONFIG_BSP_* are absent. */
#ifndef CONFIG_BSP_LCD_RGB_BUFFER_NUMS
#define CONFIG_BSP_LCD_RGB_BUFFER_NUMS 2
#endif
#ifndef CONFIG_BSP_LCD_RGB_BOUNCE_BUFFER_HEIGHT
/* 行数必须整除 v_res：IDF 要求 frame_bytes % bounce_bytes == 0（480×480 时 h∈{20,24,30,40,...}）。 */
#define CONFIG_BSP_LCD_RGB_BOUNCE_BUFFER_HEIGHT 20
#endif
#ifndef CONFIG_BSP_DISPLAY_LVGL_BUF_HEIGHT
#define CONFIG_BSP_DISPLAY_LVGL_BUF_HEIGHT 100
#endif
#ifndef CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR
/* 2+ RGB frame buffers: draw into panel FB (full 480×480). Partial 100-line buffer only paints ~1/5 strip. */
#define CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR 1
#endif
#ifndef CONFIG_BOARD_TOUCH_MIRROR_X
#define CONFIG_BOARD_TOUCH_MIRROR_X 0
#endif
#ifndef CONFIG_BOARD_TOUCH_MIRROR_Y
#define CONFIG_BOARD_TOUCH_MIRROR_Y 0
#endif
#ifndef CONFIG_BOARD_TOUCH_SWAP_XY
#define CONFIG_BOARD_TOUCH_SWAP_XY 0
#endif
#ifndef CONFIG_BOARD_LCD_RGB_ELEMENT_BGR
#define CONFIG_BOARD_LCD_RGB_ELEMENT_BGR 0
#endif

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"

#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_cache.h"
#include "esp_lcd_st7701.h"
#include "esp_lcd_touch_gt911.h"

#include "lvgl.h"

/* -------------------------------
 * Board electrical configuration
 * ------------------------------- */

/* I2C pins for GT911 */
#define BOARD_I2C_NUM (1)
#define BOARD_I2C_SCL_GPIO (GPIO_NUM_7)
#define BOARD_I2C_SDA_GPIO (GPIO_NUM_15)
#define BOARD_I2C_CLK_HZ (400000)

/* Display geometry — 3.95" HSD 480×480 IPS + ST7701S */
#define BOARD_LCD_H_RES (480)
#define BOARD_LCD_V_RES (480)
#define BOARD_LVGL_BUFFER_HEIGHT (CONFIG_BSP_DISPLAY_LVGL_BUF_HEIGHT)

/*
 * RGB sync porches from vendor file "3.95HSD(480x480-ips)+ST7701S+RGB18.txt":
 *   HSPW=8  HBPD=80  HFPD=80
 *   VSPW=2  VBPD=30  VFPD=18
 * pclk ≈ 60 Hz × 648 × 530 ≈ 20.6 MHz
 */
#define BOARD_LCD_395HSD_RGB_TIMING()                     \
    {                                                     \
        .pclk_hz = 20600000,                              \
        .h_res = BOARD_LCD_H_RES,                         \
        .v_res = BOARD_LCD_V_RES,                         \
        .hsync_pulse_width = 8,                           \
        .hsync_back_porch = 80,                           \
        .hsync_front_porch = 80,                          \
        .vsync_pulse_width = 2,                           \
        .vsync_back_porch = 30,                           \
        .vsync_front_porch = 18,                          \
        .flags.pclk_active_neg = false,                   \
    }

/* ESP32 RGB bus = 16 lines (RGB565). ST7701 register COLMOD=0x66 (RGB18) per vendor txt. */
#define BOARD_LCD_RGB_BUS_BPP       (16)
#define BOARD_LCD_PANEL_BPP         (18)
#define BOARD_LCD_RGB_BOUNCE_BUFFER_HEIGHT (CONFIG_BSP_LCD_RGB_BOUNCE_BUFFER_HEIGHT)

/*
 * LCD reset (LCD_RST_L)
 * - NCA9555 P0.2 → BOARD_IO_EXP_LCD_RST (see board_io_expander.h)
 * - No MCU GPIO; ST7701 panel_config.reset_gpio_num stays GPIO_NUM_NC
 */
#if CONFIG_BOARD_IO_EXPANDER_ENABLE
#define BOARD_LCD_RESET_VIA_IO_EXPANDER  1
#define BOARD_LCD_RESET_EXP_PIN          BOARD_IO_EXP_LCD_RST  /* P0.2, index 2 */
#define BOARD_LCD_RESET_GPIO             GPIO_NUM_NC
#else
#define BOARD_LCD_RESET_VIA_IO_EXPANDER  0
#define BOARD_LCD_RESET_EXP_PIN          0
#ifndef BOARD_LCD_RESET_GPIO
#define BOARD_LCD_RESET_GPIO             BOARD_IO_EXP_LCD_RST  /* legacy: direct MCU reset if wired */
#endif
#endif

#if defined(CONFIG_BOARD_BACKLIGHT_PWM_GPIO_NUM) && (CONFIG_BOARD_BACKLIGHT_PWM_GPIO_NUM) >= 0
#define BOARD_BACKLIGHT_HAS_LEDC 1
#ifndef BOARD_BACKLIGHT_PWM_GPIO
#define BOARD_BACKLIGHT_PWM_GPIO ((gpio_num_t)CONFIG_BOARD_BACKLIGHT_PWM_GPIO_NUM)
#endif
#else
#define BOARD_BACKLIGHT_HAS_LEDC 0
#ifndef BOARD_BACKLIGHT_PWM_GPIO
#define BOARD_BACKLIGHT_PWM_GPIO GPIO_NUM_NC
#endif
#endif

/* ESP32-S3 LEDC has only low-speed mode. */
#if BOARD_BACKLIGHT_HAS_LEDC
#define BOARD_BACKLIGHT_LEDC_SPEED_MODE LEDC_LOW_SPEED_MODE
#define BOARD_BACKLIGHT_LEDC_TIMER_NUM LEDC_TIMER_0
#define BOARD_BACKLIGHT_LEDC_CHANNEL_NUM LEDC_CHANNEL_0
#define BOARD_BACKLIGHT_LEDC_DUTY_RES LEDC_TIMER_8_BIT
#if defined(CONFIG_BOARD_BACKLIGHT_LEDC_FREQ_HZ)
#define BOARD_BACKLIGHT_LEDC_FREQ_HZ (CONFIG_BOARD_BACKLIGHT_LEDC_FREQ_HZ)
#else
#define BOARD_BACKLIGHT_LEDC_FREQ_HZ (5000)
#endif
#endif

static const char *TAG = "board_init";

/* -------------------------------
 * LCD RGB pins — from menuconfig (Board: RGB LCD pin mapping).
 * Adjust via "idf.py menuconfig" without touching source code.
 * ------------------------------- */
#define BSP_LCD_DATA0  (CONFIG_BOARD_LCD_DATA0_GPIO)
#define BSP_LCD_DATA1  (CONFIG_BOARD_LCD_DATA1_GPIO)
#define BSP_LCD_DATA2  (CONFIG_BOARD_LCD_DATA2_GPIO)
#define BSP_LCD_DATA3  (CONFIG_BOARD_LCD_DATA3_GPIO)
#define BSP_LCD_DATA4  (CONFIG_BOARD_LCD_DATA4_GPIO)
#define BSP_LCD_DATA5  (CONFIG_BOARD_LCD_DATA5_GPIO)
#define BSP_LCD_DATA6  (CONFIG_BOARD_LCD_DATA6_GPIO)
#define BSP_LCD_DATA7  (CONFIG_BOARD_LCD_DATA7_GPIO)
#define BSP_LCD_DATA8  (CONFIG_BOARD_LCD_DATA8_GPIO)
#define BSP_LCD_DATA9  (CONFIG_BOARD_LCD_DATA9_GPIO)
#define BSP_LCD_DATA10 (CONFIG_BOARD_LCD_DATA10_GPIO)
#define BSP_LCD_DATA11 (CONFIG_BOARD_LCD_DATA11_GPIO)
#define BSP_LCD_DATA12 (CONFIG_BOARD_LCD_DATA12_GPIO)
#define BSP_LCD_DATA13 (CONFIG_BOARD_LCD_DATA13_GPIO)
#define BSP_LCD_DATA14 (CONFIG_BOARD_LCD_DATA14_GPIO)
#define BSP_LCD_DATA15 (CONFIG_BOARD_LCD_DATA15_GPIO)

#define BSP_LCD_VSYNC  (CONFIG_BOARD_LCD_VSYNC_GPIO)
#define BSP_LCD_HSYNC  (CONFIG_BOARD_LCD_HSYNC_GPIO)
#define BSP_LCD_DE     (CONFIG_BOARD_LCD_DE_GPIO)
#define BSP_LCD_PCLK   (CONFIG_BOARD_LCD_PCLK_GPIO)
/* -1 from Kconfig maps to GPIO_NUM_NC (-1) */
#define BSP_LCD_DISP   (CONFIG_BOARD_LCD_DISP_GPIO)

/* ST7701 3-wire SPI: on NCA9555 port 0 when BOARD_IO_EXPANDER_ENABLE */

/* -------------------------------
 * ST7701S init — vendor "3.95HSD(480x480-ips)+ST7701S+RGB18.txt".
 * ESP32: 0x11 Sleep Out FIRST (vendor puts it last → white screen).
 * Do NOT use Waveshare BSP extras (0xCD / 0x21 / MADCTL=0x00) on this panel — colors go wrong.
 * MADCTL=0x00 (RGB scan); do not combine with rgb_ele_order BGR (double swap → wrong colors).
 * ------------------------------- */
__attribute__((unused))
static const st7701_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t[]){0x3B, 0x00}, 2, 0},
    {0xC1, (uint8_t[]){0x0D, 0x02}, 2, 0},
    {0xC2, (uint8_t[]){0x21, 0x08}, 2, 0},
    {0xB0, (uint8_t[]){0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08, 0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18}, 16, 0},
    {0xB1, (uint8_t[]){0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18}, 16, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t[]){0x60}, 1, 0},
    {0xB1, (uint8_t[]){0x30}, 1, 0},
    {0xB2, (uint8_t[]){0x87}, 1, 0},
    {0xB3, (uint8_t[]){0x80}, 1, 0},
    {0xB5, (uint8_t[]){0x49}, 1, 0},
    {0xB7, (uint8_t[]){0x85}, 1, 0},
    {0xB8, (uint8_t[]){0x21}, 1, 0},
    {0xC1, (uint8_t[]){0x78}, 1, 0},
    {0xC2, (uint8_t[]){0x78}, 1, 20},
    {0xE0, (uint8_t[]){0x00, 0x1B, 0x02}, 3, 0},
    {0xE1, (uint8_t[]){0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44}, 11, 0},
    {0xE2, (uint8_t[]){0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00}, 12, 0},
    {0xE3, (uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE4, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE5, (uint8_t[]){0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0, 0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0}, 16, 0},
    {0xE6, (uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE7, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE8, (uint8_t[]){0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0, 0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0}, 16, 0},
    {0xEB, (uint8_t[]){0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40}, 7, 0},
    {0xEC, (uint8_t[]){0x3C, 0x00}, 2, 0},
    {0xED, (uint8_t[]){0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA}, 16, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},
    {0x3A, (uint8_t[]){0x66}, 1, 0},
    {0x29, (uint8_t[]){0x00}, 0, 50},
};

/* -------------------------------
 * Runtime handles
 * ------------------------------- */
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static esp_lcd_panel_io_handle_t s_lcd_io_handle = NULL;
static esp_lcd_panel_handle_t s_lcd_panel_handle = NULL;

/* -------------------------------
 * I2C init
 * ------------------------------- */
static esp_err_t board_i2c_init(void)
{
    if (s_i2c_bus != NULL) return ESP_OK;

#if CONFIG_BOARD_IO_EXPANDER_ENABLE
    /* Eth / other may have opened I2C1 via board_io_expander_init(NULL) already. */
    i2c_master_bus_handle_t shared = board_io_expander_get_i2c_bus();
    if (shared != NULL) {
        s_i2c_bus = shared;
        ESP_LOGI(TAG, "I2C bus adopted from IO expander");
        return ESP_OK;
    }
#endif

    i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .i2c_port = BOARD_I2C_NUM,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
    esp_err_t ret = i2c_new_master_bus(&i2c_bus_conf, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

i2c_master_bus_handle_t board_i2c_get_handle(void)
{
    return s_i2c_bus;
}

/* -------------------------------
 * Touch init (GT911)
 * ------------------------------- */
static esp_err_t board_touch_new_i2c_gt911(esp_lcd_touch_handle_t *out_touch)
{
    ESP_RETURN_ON_FALSE(out_touch != NULL, ESP_ERR_INVALID_ARG, TAG, "out_touch is NULL");

    ESP_RETURN_ON_ERROR(board_i2c_init(), TAG, "i2c init failed");

#if CONFIG_BOARD_IO_EXPANDER_ENABLE
    ESP_RETURN_ON_ERROR(board_io_expander_init(s_i2c_bus), TAG, "expander init");
    /* GT911 TP_RST (P0.1) is also pulsed during LCD init, but ~1 s elapses
     * before we reach this point (ST7701 init). A dedicated reset pulse
     * right before the I2C probe guarantees a fresh state. */
    ESP_RETURN_ON_ERROR(board_io_expander_touch_reset_pulse(), TAG, "tp reset");
#endif

    /* Probe GT911 address (primary or backup) */
    esp_err_t probe_primary = i2c_master_probe(s_i2c_bus, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, 100);
    esp_err_t probe_backup = i2c_master_probe(s_i2c_bus, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP, 100);

    if (probe_primary != ESP_OK && probe_backup != ESP_OK) {
        ESP_LOGW(TAG, "Touch not found (0x%02x / 0x%02x)",
                 ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
                 ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP);
        return ESP_ERR_NOT_FOUND;
    }

    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    if (probe_primary == ESP_OK) {
        ESP_LOGI(TAG, "GT911 at 0x%02x", ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS);
    } else {
        tp_io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
        ESP_LOGI(TAG, "GT911 at 0x%02x (backup)", ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP);
    }
    tp_io_config.scl_speed_hz = BOARD_I2C_CLK_HZ;

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_config, &tp_io_handle),
                         TAG, "tp io init failed");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC, /* new board may not wire touch reset directly */
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
#if CONFIG_BOARD_TOUCH_SWAP_XY
            .swap_xy = 1,
#else
            .swap_xy = 0,
#endif
#if CONFIG_BOARD_TOUCH_MIRROR_X
            .mirror_x = 1,
#else
            .mirror_x = 0,
#endif
#if CONFIG_BOARD_TOUCH_MIRROR_Y
            .mirror_y = 1,
#else
            .mirror_y = 0,
#endif
        },
    };

    return esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, out_touch);
}

static void board_lcd_sync_fb_to_psram(const void *fb, size_t nbytes)
{
    if (fb == NULL || nbytes == 0) {
        return;
    }
    esp_cache_msync((void *)fb, nbytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

static void (*s_lvgl_port_flush_cb)(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map);

/** PSRAM FB + bounce buffer: flush CPU cache before esp_lvgl_port calls draw_bitmap. */
static void board_lvgl_flush_sync_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
#if CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR && CONFIG_BSP_LCD_RGB_BUFFER_NUMS >= 2
    if (drv != NULL && drv->draw_buf != NULL) {
        const uint32_t full_px = (uint32_t)drv->hor_res * (uint32_t)drv->ver_res;
        const bool full_frame = drv->draw_buf->size >= full_px;
        if (full_frame && (drv->full_refresh || lv_disp_flush_is_last(drv))) {
            const size_t fb_bytes = (size_t)full_px * sizeof(lv_color_t);
            board_lcd_sync_fb_to_psram(color_map, fb_bytes);
        }
    }
#else
    (void)drv;
    (void)area;
    (void)color_map;
#endif
    if (s_lvgl_port_flush_cb != NULL) {
        s_lvgl_port_flush_cb(drv, area, color_map);
    }
}

/** Fill all RGB frame buffers with one RGB565 color (PSRAM defaults to 0xFF = white). */
static esp_err_t board_lcd_fill_frame_buffers(uint16_t color565, void **out_fb0)
{
    const int num_fbs = CONFIG_BSP_LCD_RGB_BUFFER_NUMS;
    void *fbs[3] = {NULL, NULL, NULL};
    esp_err_t err = ESP_ERR_INVALID_ARG;

    if (num_fbs >= 3) {
        err = esp_lcd_rgb_panel_get_frame_buffer(s_lcd_panel_handle, 3, &fbs[0], &fbs[1], &fbs[2]);
    } else if (num_fbs == 2) {
        err = esp_lcd_rgb_panel_get_frame_buffer(s_lcd_panel_handle, 2, &fbs[0], &fbs[1]);
    } else {
        err = esp_lcd_rgb_panel_get_frame_buffer(s_lcd_panel_handle, 1, &fbs[0]);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fill_frame_buffers(%d fbs) failed: %s", num_fbs, esp_err_to_name(err));
        return err;
    }

    const int total_px = BOARD_LCD_H_RES * BOARD_LCD_V_RES;
    const size_t fb_bytes = (size_t)total_px * sizeof(uint16_t);
    for (int b = 0; b < num_fbs && fbs[b] != NULL; b++) {
        if (color565 == 0) {
            memset(fbs[b], 0, fb_bytes);
        } else {
            uint16_t *p = (uint16_t *)fbs[b];
            for (int i = 0; i < total_px; i++) {
                p[i] = color565;
            }
        }
        board_lcd_sync_fb_to_psram(fbs[b], fb_bytes);
    }
    if (out_fb0 != NULL) {
        *out_fb0 = fbs[0];
    }
    return ESP_OK;
}

#if CONFIG_BOARD_LCD_RGB_SOLID_TEST
/* Bring-up self-test: write solid colors straight into the RGB frame buffers,
 * fully bypassing LVGL, to verify the RGB output path + ST7701 init. */
static void board_lcd_rgb_solid_test(void)
{
    const int num_fbs = CONFIG_BSP_LCD_RGB_BUFFER_NUMS;
    void *fbs[3] = {NULL, NULL, NULL};
    esp_err_t err = ESP_ERR_INVALID_ARG;

    if (num_fbs >= 3) {
        err = esp_lcd_rgb_panel_get_frame_buffer(s_lcd_panel_handle, 3, &fbs[0], &fbs[1], &fbs[2]);
    } else if (num_fbs == 2) {
        err = esp_lcd_rgb_panel_get_frame_buffer(s_lcd_panel_handle, 2, &fbs[0], &fbs[1]);
    } else {
        err = esp_lcd_rgb_panel_get_frame_buffer(s_lcd_panel_handle, 1, &fbs[0]);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RGB solid test: get_frame_buffer(%d) failed (%s)", num_fbs, esp_err_to_name(err));
        return;
    }

    const int total_px = BOARD_LCD_H_RES * BOARD_LCD_V_RES;
    const uint16_t colors[3] = {0xF800 /*R*/, 0x07E0 /*G*/, 0x001F /*B*/};
    const char *names[3] = {"RED", "GREEN", "BLUE"};

    for (int c = 0; c < 3; c++) {
        for (int b = 0; b < num_fbs && fbs[b] != NULL; b++) {
            uint16_t *p = (uint16_t *)fbs[b];
            for (int i = 0; i < total_px; i++) {
                p[i] = colors[c];
            }
        }
        /* Push to the panel so the scanned buffer shows this color. */
        esp_lcd_panel_draw_bitmap(s_lcd_panel_handle, 0, 0, BOARD_LCD_H_RES, BOARD_LCD_V_RES, fbs[0]);
        ESP_LOGI(TAG, "RGB solid test: %s (0x%04X) — screen should be solid %s",
                 names[c], colors[c], names[c]);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_BOARD_LCD_RGB_SOLID_TEST_SECONDS * 1000));
    }
    ESP_LOGI(TAG, "RGB solid test done. If you saw R/G/B, RGB+ST7701 OK → debug LVGL/UI. "
                  "If always black → ST7701 not initialized (3-wire SPI) or RGB data pins wrong.");
}
#endif

/* -------------------------------
 * Display init (ST7701)
 * ------------------------------- */
static esp_err_t board_lcd_init(void)
{
    ESP_LOGI(TAG, "RGB pin map: PCLK=%d DE=%d VSYNC=%d HSYNC=%d DISP=%d",
             BSP_LCD_PCLK, BSP_LCD_DE, BSP_LCD_VSYNC, BSP_LCD_HSYNC, BSP_LCD_DISP);
    ESP_LOGI(TAG, "RGB data:  D0-7 =%d %d %d %d %d %d %d %d",
             BSP_LCD_DATA0, BSP_LCD_DATA1, BSP_LCD_DATA2, BSP_LCD_DATA3,
             BSP_LCD_DATA4, BSP_LCD_DATA5, BSP_LCD_DATA6, BSP_LCD_DATA7);
    ESP_LOGI(TAG, "RGB data: D8-15 =%d %d %d %d %d %d %d %d",
             BSP_LCD_DATA8, BSP_LCD_DATA9, BSP_LCD_DATA10, BSP_LCD_DATA11,
             BSP_LCD_DATA12, BSP_LCD_DATA13, BSP_LCD_DATA14, BSP_LCD_DATA15);

    esp_io_expander_handle_t io_exp = NULL;
#if CONFIG_BOARD_IO_EXPANDER_ENABLE
    ESP_RETURN_ON_ERROR(board_i2c_init(), TAG, "i2c init failed");
    esp_err_t exp_err = board_io_expander_init(s_i2c_bus);
    if (exp_err != ESP_OK) {
        ESP_LOGE(TAG, "IO expander init failed: %s", esp_err_to_name(exp_err));
        return exp_err;
    }
    io_exp = board_io_expander_get();
    /* Verify NCA9555 CS/SCL/SDA truly reach the panel before relying on bit-banged SPI. */
    (void)board_io_expander_spi_lines_selftest();
    /* NCA9555 P0.2 (LCD_RST_L) — must run before ST7701 3-wire init */
    ESP_RETURN_ON_ERROR(board_io_expander_lcd_reset_pulse(), TAG, "LCD_RST P0.2 reset failed");
#endif

    spi_line_config_t line_config = {0};
#if CONFIG_BOARD_IO_EXPANDER_ENABLE
    line_config.cs_io_type = IO_TYPE_EXPANDER;
    line_config.cs_expander_pin = BOARD_IO_EXP_LCD_CS;
    line_config.scl_io_type = IO_TYPE_EXPANDER;
    line_config.sda_io_type = IO_TYPE_EXPANDER;
#if CONFIG_BOARD_LCD_3WIRE_SWAP_SDA_SCL
    /* Diagnostic: swap SDA/SCL to test for a reversed wiring (self-test can't detect it). */
    line_config.scl_expander_pin = BOARD_IO_EXP_LCD_SDA;
    line_config.sda_expander_pin = BOARD_IO_EXP_LCD_SCL;
    ESP_LOGW(TAG, "3-wire SPI: SDA/SCL SWAPPED (diagnostic) SCL<-P0.3 SDA<-P0.4");
#else
    line_config.scl_expander_pin = BOARD_IO_EXP_LCD_SCL;
    line_config.sda_expander_pin = BOARD_IO_EXP_LCD_SDA;
#endif
    line_config.io_expander = io_exp;
#else
    line_config.cs_io_type = IO_TYPE_GPIO;
    line_config.cs_gpio_num = GPIO_NUM_42;
    line_config.scl_io_type = IO_TYPE_GPIO;
    line_config.scl_gpio_num = GPIO_NUM_2;
    line_config.sda_io_type = IO_TYPE_GPIO;
    line_config.sda_gpio_num = GPIO_NUM_1;
#endif

    esp_lcd_panel_io_3wire_spi_config_t io_config = ST7701_PANEL_IO_3WIRE_SPI_CONFIG(line_config, 0);
    esp_err_t ret = esp_lcd_new_panel_io_3wire_spi(&io_config, &s_lcd_io_handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "esp_lcd_new_panel_io_3wire_spi failed");

    /* RGB panel config (used by ST7701 vendor driver) */
    esp_lcd_rgb_panel_config_t rgb_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .psram_trans_align = 64,
        .data_width = BOARD_LCD_RGB_BUS_BPP,
        .bits_per_pixel = BOARD_LCD_RGB_BUS_BPP,
        .de_gpio_num = BSP_LCD_DE,
        .pclk_gpio_num = BSP_LCD_PCLK,
        .vsync_gpio_num = BSP_LCD_VSYNC,
        .hsync_gpio_num = BSP_LCD_HSYNC,
        .disp_gpio_num = BSP_LCD_DISP,
        .data_gpio_nums = {
            BSP_LCD_DATA0,
            BSP_LCD_DATA1,
            BSP_LCD_DATA2,
            BSP_LCD_DATA3,
            BSP_LCD_DATA4,
            BSP_LCD_DATA5,
            BSP_LCD_DATA6,
            BSP_LCD_DATA7,
            BSP_LCD_DATA8,
            BSP_LCD_DATA9,
            BSP_LCD_DATA10,
            BSP_LCD_DATA11,
            BSP_LCD_DATA12,
            BSP_LCD_DATA13,
            BSP_LCD_DATA14,
            BSP_LCD_DATA15,
        },
        .timings = BOARD_LCD_395HSD_RGB_TIMING(),
        .flags.fb_in_psram = 1,
        .num_fbs = CONFIG_BSP_LCD_RGB_BUFFER_NUMS,
        .bounce_buffer_size_px = BOARD_LCD_H_RES * BOARD_LCD_RGB_BOUNCE_BUFFER_HEIGHT,
    };
    rgb_config.timings.h_res = BOARD_LCD_H_RES;
    rgb_config.timings.v_res = BOARD_LCD_V_RES;
#if CONFIG_BOARD_LCD_USE_GENERIC_RGB_TIMING
    rgb_config.timings = ST7701_480_480_PANEL_60HZ_RGB_TIMING();
    rgb_config.timings.h_res = BOARD_LCD_H_RES;
    rgb_config.timings.v_res = BOARD_LCD_V_RES;
    ESP_LOGW(TAG, "RGB: using generic ST7701 timing (diagnostic)");
#endif
#if CONFIG_BOARD_LCD_PCLK_ACTIVE_NEG
    /* Diagnostic: latch RGB data on PCLK falling edge (some panel modules need this). */
    rgb_config.timings.flags.pclk_active_neg = true;
    ESP_LOGW(TAG, "RGB timing: pclk_active_neg = 1 (falling-edge latch, diagnostic)");
#endif

    st7701_vendor_config_t vendor_config = {
        .rgb_config = &rgb_config,
#if CONFIG_BOARD_LCD_ST7701_DEFAULT_INIT
        /* Diagnostic: use the ST7701 driver's built-in default init instead of ours. */
        .init_cmds = NULL,
        .init_cmds_size = 0,
#else
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
#endif
        .flags = {
            .auto_del_panel_io = 0,
            .mirror_by_cmd = 1,
        },
    };
#if CONFIG_BOARD_LCD_ST7701_DEFAULT_INIT
    ESP_LOGW(TAG, "ST7701: using driver built-in default init (diagnostic)");
#endif

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BOARD_LCD_RESET_GPIO,
#if CONFIG_BOARD_LCD_RGB_ELEMENT_BGR
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#endif
        .bits_per_pixel = BOARD_LCD_PANEL_BPP,
        .vendor_config = &vendor_config,
    };

    const int64_t init_t0 = esp_timer_get_time();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7701(s_lcd_io_handle, &panel_config, &s_lcd_panel_handle),
                         TAG, "esp_lcd_new_panel_st7701 failed");

    esp_lcd_panel_reset(s_lcd_panel_handle);
    esp_lcd_panel_init(s_lcd_panel_handle);
    const int init_ms = (int)((esp_timer_get_time() - init_t0) / 1000);
    /* PSRAM frame buffers default to 0xFF (white). Clear all fbs then push one frame. */
    void *fb0 = NULL;
    if (board_lcd_fill_frame_buffers(0x0000, &fb0) == ESP_OK && fb0 != NULL) {
        esp_lcd_panel_draw_bitmap(s_lcd_panel_handle, 0, 0, BOARD_LCD_H_RES, BOARD_LCD_V_RES, fb0);
    }
    esp_lcd_panel_disp_on_off(s_lcd_panel_handle, true);

    ESP_LOGI(TAG, "ST7701S init %dms: MADCTL=0x00 COLMOD=0x66 rgb_order=%s pclk=%dHz H(%d+%d+%d) V(%d+%d+%d)",
             init_ms,
#if CONFIG_BOARD_LCD_RGB_ELEMENT_BGR
             "BGR",
#else
             "RGB",
#endif
             (int)rgb_config.timings.pclk_hz,
             rgb_config.timings.hsync_pulse_width, rgb_config.timings.hsync_back_porch,
             rgb_config.timings.hsync_front_porch,
             rgb_config.timings.vsync_pulse_width, rgb_config.timings.vsync_back_porch,
             rgb_config.timings.vsync_front_porch);

#if CONFIG_BOARD_LCD_RGB_SOLID_TEST
    board_lcd_rgb_solid_test();
#endif

    return ESP_OK;
}

esp_err_t board_display_start_with_lvgl_cfg(const lvgl_port_cfg_t *lvgl_cfg)
{
    ESP_RETURN_ON_FALSE(lvgl_cfg != NULL, ESP_ERR_INVALID_ARG, TAG, "lvgl_cfg is NULL");

#if CONFIG_BOARD_IO_EXPANDER_ENABLE
    ESP_RETURN_ON_ERROR(board_i2c_init(), TAG, "i2c for expander");
    ESP_RETURN_ON_ERROR(board_io_expander_init(s_i2c_bus), TAG, "expander early init");
#endif

    /* 1) init LVGL port task/tick */
    ESP_RETURN_ON_ERROR(lvgl_port_init(lvgl_cfg), TAG, "lvgl_port_init failed");

    /* 2) init LCD hardware + register to LVGL */
    ESP_RETURN_ON_ERROR(board_lcd_init(), TAG, "board_lcd_init failed");

    uint32_t buffer_size_px = 0;
#if CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR && CONFIG_BSP_LCD_RGB_BUFFER_NUMS >= 2
    buffer_size_px = BOARD_LCD_H_RES * BOARD_LCD_V_RES;
#else
    buffer_size_px = BOARD_LCD_H_RES * BOARD_LVGL_BUFFER_HEIGHT;
#endif

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_lcd_io_handle,
        .panel_handle = s_lcd_panel_handle,
        .control_handle = NULL,

        .buffer_size = buffer_size_px,
        .trans_size = 0,

        .monochrome = false,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,

        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },

        .flags = {
            .sw_rotate = false,
            .buff_dma = false,
            .buff_spiram = false,
#if CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR && CONFIG_BSP_LCD_RGB_BUFFER_NUMS >= 2
            /* Espressif RGB avoid-tear: direct_mode + panel FBs + bounce (stable on PSRAM). */
            .full_refresh = 0,
            .direct_mode = 1,
#else
            .full_refresh = 0,
            .direct_mode = 0,
#endif
        },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
#if CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR && CONFIG_BSP_LCD_RGB_BUFFER_NUMS >= 2
            .bb_mode = 1,
            .avoid_tearing = true,
#else
            .bb_mode = 0,
            .avoid_tearing = false,
#endif
        },
    };

    /* Hold LVGL refr until app finishes ui_init — otherwise taskLVGL may paint the
     * default screen (white) into the RGB frame buffers before main locks. */
    lvgl_port_lock(0);
    lv_disp_t *disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (disp != NULL) {
        if (disp->driver != NULL && disp->driver->flush_cb != NULL) {
            s_lvgl_port_flush_cb = disp->driver->flush_cb;
            disp->driver->flush_cb = board_lvgl_flush_sync_cb;
        }
        lv_disp_enable_invalidation(disp, false);
        lv_timer_t *refr = _lv_disp_get_refr_timer(disp);
        if (refr != NULL) {
            lv_timer_pause(refr);
        }
        (void)board_lcd_fill_frame_buffers(0x0000, NULL);
    }
    lvgl_port_unlock();
    ESP_RETURN_ON_FALSE(disp != NULL, ESP_FAIL, TAG, "lvgl_port_add_disp_rgb failed");

    ESP_LOGI(TAG, "LVGL RGB: buf_px=%u avoid_tear=%d direct=%d fbs=%d bounce_h=%d",
             (unsigned)buffer_size_px,
#if CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR && CONFIG_BSP_LCD_RGB_BUFFER_NUMS >= 2
             1, disp_cfg.flags.direct_mode,
#else
             0, disp_cfg.flags.direct_mode,
#endif
             CONFIG_BSP_LCD_RGB_BUFFER_NUMS,
             CONFIG_BSP_LCD_RGB_BOUNCE_BUFFER_HEIGHT);

    /* 3) init touch (optional) */
    esp_lcd_touch_handle_t tp = NULL;
    esp_err_t touch_ret = board_touch_new_i2c_gt911(&tp);
    if (touch_ret == ESP_OK && tp != NULL) {
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = disp,
            .handle = tp,
            .scale = {1.0f, 1.0f},
        };
        lv_indev_t *indev = lvgl_port_add_touch(&touch_cfg);
        if (indev == NULL) {
            ESP_LOGE(TAG, "lvgl_port_add_touch failed");
        } else {
            ESP_LOGI(TAG, "GT911 touch registered (mirror_x=%d mirror_y=%d swap_xy=%d)",
                     (int)(CONFIG_BOARD_TOUCH_MIRROR_X),
                     (int)(CONFIG_BOARD_TOUCH_MIRROR_Y),
                     (int)(CONFIG_BOARD_TOUCH_SWAP_XY));
        }
    } else {
        ESP_LOGW(TAG, "Touch init skipped: %s", esp_err_to_name(touch_ret));
    }

    return ESP_OK;
}

/* -------------------------------
 * Backlight (optional)
 * ------------------------------- */
esp_err_t board_backlight_init(void)
{
#if CONFIG_BOARD_IO_EXPANDER_ENABLE && CONFIG_BOARD_BACKLIGHT_VIA_IO_EXPANDER
    ESP_RETURN_ON_ERROR(board_io_expander_init(s_i2c_bus), TAG, "expander for BL");
    /* Stay dark until app_lvgl_first_refr_then_backlight() in main.c. */
    return board_io_expander_backlight_set(0);
#elif !BOARD_BACKLIGHT_HAS_LEDC
    return ESP_OK;
#else
    static bool s_backlight_inited = false;
    if (s_backlight_inited) {
        return ESP_OK;
    }

    ledc_timer_config_t timer_conf = {
        .speed_mode = BOARD_BACKLIGHT_LEDC_SPEED_MODE,
        .timer_num = BOARD_BACKLIGHT_LEDC_TIMER_NUM,
        .duty_resolution = BOARD_BACKLIGHT_LEDC_DUTY_RES,
        .freq_hz = BOARD_BACKLIGHT_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_conf), TAG, "ledc_timer_config failed");

    ledc_channel_config_t chan_conf = {
        .gpio_num = BOARD_BACKLIGHT_PWM_GPIO,
        .speed_mode = BOARD_BACKLIGHT_LEDC_SPEED_MODE,
        .channel = BOARD_BACKLIGHT_LEDC_CHANNEL_NUM,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BOARD_BACKLIGHT_LEDC_TIMER_NUM,
        /* Start dark: app enables backlight after first LVGL frame (avoids white flash during PNG init). */
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&chan_conf), TAG, "ledc_channel_config failed");

    s_backlight_inited = true;
    ESP_LOGI(TAG, "backlight LEDC GPIO%d %dHz max_duty=%d%% (start 0%%)",
             (int)BOARD_BACKLIGHT_PWM_GPIO, BOARD_BACKLIGHT_LEDC_FREQ_HZ,
#if defined(CONFIG_BOARD_BACKLIGHT_MAX_DUTY_PERCENT)
             CONFIG_BOARD_BACKLIGHT_MAX_DUTY_PERCENT
#else
             100
#endif
    );
    return ESP_OK;
#endif
}

esp_err_t board_backlight_set(int brightness_percent)
{
#if CONFIG_BOARD_IO_EXPANDER_ENABLE && CONFIG_BOARD_BACKLIGHT_VIA_IO_EXPANDER
    return board_io_expander_backlight_set(brightness_percent);
#elif !BOARD_BACKLIGHT_HAS_LEDC
    (void)brightness_percent;
    return ESP_OK;
#else
    ESP_RETURN_ON_ERROR(board_backlight_init(), TAG, "backlight init");
    brightness_percent = board_backlight_clamp_percent(brightness_percent);

    uint32_t duty_full = (1u << BOARD_BACKLIGHT_LEDC_DUTY_RES) - 1u;
#if defined(CONFIG_BOARD_BACKLIGHT_MAX_DUTY_PERCENT) && (CONFIG_BOARD_BACKLIGHT_MAX_DUTY_PERCENT) < 100
    /* UI 100% → 硬件满幅的 MAX_DUTY_PERCENT%（光强 512→400 时默认 78） */
    uint32_t duty_max = duty_full * (uint32_t)CONFIG_BOARD_BACKLIGHT_MAX_DUTY_PERCENT / 100u;
#else
    uint32_t duty_max = duty_full;
#endif
    uint32_t duty = (uint32_t)brightness_percent * duty_max / 100u;

    ESP_RETURN_ON_ERROR(ledc_set_duty(BOARD_BACKLIGHT_LEDC_SPEED_MODE, BOARD_BACKLIGHT_LEDC_CHANNEL_NUM, duty),
                         TAG, "ledc_set_duty failed");
    ESP_RETURN_ON_ERROR(ledc_update_duty(BOARD_BACKLIGHT_LEDC_SPEED_MODE, BOARD_BACKLIGHT_LEDC_CHANNEL_NUM),
                         TAG, "ledc_update_duty failed");
    return ESP_OK;
#endif
}

int board_backlight_clamp_percent(int brightness_percent)
{
    if (brightness_percent <= 0) {
        return 0;
    }
    if (brightness_percent > 100) {
        brightness_percent = 100;
    }
#if defined(CONFIG_BOARD_BACKLIGHT_MIN_PERCENT) && CONFIG_BOARD_BACKLIGHT_MIN_PERCENT > 0
    if (brightness_percent < CONFIG_BOARD_BACKLIGHT_MIN_PERCENT) {
        brightness_percent = CONFIG_BOARD_BACKLIGHT_MIN_PERCENT;
    }
#endif
    return brightness_percent;
}

int board_backlight_quantize_percent(int brightness_percent)
{
#if CONFIG_BOARD_IO_EXPANDER_ENABLE && CONFIG_BOARD_BACKLIGHT_VIA_IO_EXPANDER
    return board_io_expander_backlight_quantize(brightness_percent);
#else
    brightness_percent = board_backlight_clamp_percent(brightness_percent);
    if (brightness_percent <= 0) {
        return 0;
    }
    if (brightness_percent >= 100) {
        return 100;
    }
    return brightness_percent;
#endif
}

esp_err_t board_backlight_on(void)
{
    return board_backlight_set(100);
}

esp_err_t board_backlight_off(void)
{
    return board_backlight_set(0);
}

void board_display_present_fb(const void *rendered_fb)
{
    if (s_lcd_panel_handle == NULL) {
        return;
    }

    lv_disp_t *disp = lv_disp_get_default();
    if (disp == NULL || disp->driver == NULL || disp->driver->draw_buf == NULL) {
        return;
    }

    lv_disp_draw_buf_t *db = disp->driver->draw_buf;
    void *drawn = (void *)rendered_fb;
    if (drawn == NULL) {
        drawn = db->buf1;
        if (db->buf2 != NULL) {
            /* After lv_refr_now(), buf_act toggles — completed frame is in the other buffer. */
            drawn = (db->buf_act == db->buf1) ? db->buf2 : db->buf1;
        }
    }
    if (drawn == NULL) {
        return;
    }

    const size_t fb_bytes = (size_t)BOARD_LCD_H_RES * (size_t)BOARD_LCD_V_RES * sizeof(uint16_t);
    board_lcd_sync_fb_to_psram(drawn, fb_bytes);

    esp_err_t err = esp_lcd_panel_draw_bitmap(s_lcd_panel_handle, 0, 0,
                                              BOARD_LCD_H_RES, BOARD_LCD_V_RES, drawn);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "present_frame draw_bitmap: %s", esp_err_to_name(err));
        return;
    }

    err = lvgl_port_disp_wait_rgb_trans_done(disp, portMAX_DELAY);
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "present_frame wait: %s", esp_err_to_name(err));
    }
}

void board_display_present_frame(void)
{
    board_display_present_fb(NULL);
}

/* -------------------------------
 * Beep (optional)
 * ------------------------------- */
esp_err_t board_beep_set(int on)
{
#if CONFIG_BOARD_IO_EXPANDER_ENABLE && CONFIG_BOARD_BEEP_VIA_IO_EXPANDER
    ESP_RETURN_ON_ERROR(board_io_expander_init(s_i2c_bus), TAG, "expander for beep");
    return board_io_expander_beep_set(on);
#elif defined(CONFIG_BOARD_BEEP_GPIO_NUM) && (CONFIG_BOARD_BEEP_GPIO_NUM) >= 0
    const gpio_num_t beep_gpio = (gpio_num_t)CONFIG_BOARD_BEEP_GPIO_NUM;
    static bool s_inited = false;
    if (!s_inited) {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ull << beep_gpio,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = 0,
            .pull_down_en = 0,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        gpio_set_level(beep_gpio, 0);
        s_inited = true;
    }
    gpio_set_level(beep_gpio, on ? 1 : 0);
    return ESP_OK;
#else
    (void)on;
    return ESP_OK;
#endif
}

