/**
 * Compile-time UI theme selection - edit ONLY APP_UI_THEME_ID below.
 *
 * Customer SDK ships hub themes only (no SquareLine default pack).
 * Valid: SLATE SAND INK FOREST DUSK OCEAN ZEN PULSE BLOOM METRO
 *
 * Then: idf.py reconfigure && idf.py build
 */
#pragma once

#define APP_UI_THEME_SLATE    1
#define APP_UI_THEME_SAND     2
#define APP_UI_THEME_INK      3
#define APP_UI_THEME_FOREST   4
#define APP_UI_THEME_DUSK     5
#define APP_UI_THEME_OCEAN    6
#define APP_UI_THEME_ZEN      7
#define APP_UI_THEME_PULSE    8
#define APP_UI_THEME_BLOOM    9
#define APP_UI_THEME_METRO    10

/* ===== select theme here (one of APP_UI_THEME_* above) ===== */
#define APP_UI_THEME_ID APP_UI_THEME_SLATE 

#define APP_UI_THEME_IS_HUB 1