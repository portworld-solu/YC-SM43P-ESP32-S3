#pragma once

#include "lvgl.h"

/* 14px 思源常用字 + SimSun CJK fallback，用于 WiFi SSID 下拉与状态行（UTF-8）。 */
void wifi_ui_font_init(void);
const lv_font_t *wifi_ui_font_ssid(void);
