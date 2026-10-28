# Architecture (English)

Mental model of the Customer SDK after the open-source conversion.

## Layers

```text
┌─────────────────────────────────────────────────────────────┐
│  Product UI                                                  │
│  ui/themes/<id>/   ·   main/hub_ui/   ·   spiffs_image/      │
└─────────────────────────────▲───────────────────────────────┘
                              │ hub_model_* / hub_ui_refresh
┌─────────────────────────────┴───────────────────────────────┐
│  App / open peripherals                                      │
│  main/app (RS485, Modbus, beep, coexist) · main/board (AHT20)│
│  main/gui (background poll) · main/main.c                    │
└─────────────────────────────▲───────────────────────────────┘
                              │ public headers
┌─────────────────────────────┴───────────────────────────────┐
│  Source components (fully editable)                          │
│  board_bsp · hub_core · cloud_wifi · bt_ctrl                 │
└─────────────────────────────▲───────────────────────────────┘
                              │
                    ESP-IDF · LVGL · managed_components
```

Historically `board_bsp` / `hub_core` / `cloud_wifi` / `bt_ctrl` were sealed `.a` files.  
They are **source components** now. Prefer changing themes and `main/app` for product work; edit BSP only when bringing up hardware.

## Runtime data flow

```text
Phone / Bemfa MQTT ──► cloud_wifi ──┐
BLE / Mesh          ──► bt_ctrl    ──┤
Touch UI            ──► hub_model ◄─┘──► hub_ui refresh / toast
                              │
                    optional bus-bridge task
                              ▼
              app_rs485 / app_modbus_* ──► field devices

AHT20 ── ui_bg_task ──► hub_model.indoor_c / rh ──► theme home cards
```

## Where to edit

| Goal | Location |
|------|----------|
| Look & feel / pages | `ui/themes/<id>/` |
| Nav, fonts, shared chrome | `main/hub_ui/` |
| RS485 / Modbus / GPIO | `main/app/` |
| T/H calibration | `main/board/aht20_calib.h` |
| Display / touch / pins | `components/board_bsp/` + `config/` + [BOARD_PINOUT.md](./BOARD_PINOUT.md) |
| Wi‑Fi / MQTT | `components/cloud_wifi/src/` |
| BLE / Mesh | `components/bt_ctrl/src/` |
| Room / device model | `components/hub_core/src/hub_model.c` |

## Link-order note (Wi‑Fi UI stubs)

`wifi_management.c` references SquareLine symbols (`ui_Dropdown1`, …) and `wifi_ui_font_ssid()`.  
Those definitions live in **`components/cloud_wifi/src/*_stub*.c`**, not in `main`, so the linker can resolve them when `libcloud_wifi.a` is extracted (no `--start-group` on the final link line).

Headers remain under `main/hub_ui/stubs/` for includes.
