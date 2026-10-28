# UI theme packs (LVGL 8) — Customer SDK

Compile-time Hub themes live under `themes/`. The SquareLine `default` pack is **not** shipped.

| Item | Path / action |
|------|----------------|
| Theme packs | `themes/<id>/` (slate, sand, ink, …) |
| Select theme | Edit `APP_UI_THEME_ID` in `main/app_ui_theme_select.h`, then `idf.py reconfigure && idf.py build` |
| Icons | `../spiffs_image/icons/nt/*.png` (flash the **storage** partition) |

Board pins and panel timing are open source under `components/board_bsp/` — see [docs/BOARD_PINOUT.md](../docs/BOARD_PINOUT.md).
