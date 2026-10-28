#include "wifi_ui_font.h"

/*
 * Self-contained font stub (no hub_font / main dependency).
 * With hub themes, SquareLine SSID widgets stay NULL so this is rarely used;
 * keep a real LVGL font so the symbol always resolves inside libcloud_wifi.a.
 */
void wifi_ui_font_init(void) {}

const lv_font_t *wifi_ui_font_ssid(void)
{
    return &lv_font_montserrat_14;
}
