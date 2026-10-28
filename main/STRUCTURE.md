# `main/` layout

Editable product code: `main/`, `components/*` (open source), `ui/themes/`, `spiffs_image/`, `docs/`, `config/`.

```text
main/
├── main.c                 # Boot: NVS → display → BT/network → hub_ui
├── app_ui_theme_select.h  # Hub theme ID
├── Kconfig.projbuild      # App + Board RGB/CH390 pin menuconfig
├── app/                   # RS485 / Modbus / map / bus_bridge / beep / coexist
│   ├── app_modbus_map_config.h  # ★ customer Modbus point map
│   ├── app_bus_bridge.*         # UI ↔ Modbus (started from main.c)
│   └── examples/                # minimal bridge reference (optional)
├── board/                 # AHT20 + aht20_calib.h
├── gui/                   # Background task (AHT20 poll, …)
├── fonts/
└── hub_ui/
    ├── core/ pages/ features/
    └── stubs/             # Headers + ui_runtime stubs
                           # SquareLine/font defs → components/cloud_wifi/src/
```

Board drivers live outside `main`:

- `components/board_bsp/` — panel / touch / NCA9555 / CH390 / backlight  
- `config/sdkconfig.board.395hsd.defaults` — default panel & pins  
- `docs/BOARD_PINOUT.md` — pin table  
- `docs/ARCHITECTURE_EN.md` — full architecture map  

## Find a file

| Task | Location |
|------|----------|
| RGB / touch / expander sources | `components/board_bsp/src/` |
| Pin defaults | `config/sdkconfig.board.395hsd.defaults` or menuconfig |
| Expander bit map | `components/board_bsp/include/board_io_expander.h` |
| Theme / home | `ui/themes/<id>/` |
| Wi‑Fi / MQTT | `components/cloud_wifi/src/` |
| Bluetooth | `components/bt_ctrl/src/` |
| Hub model | `components/hub_core/src/hub_model.c` |
| RS485 / Modbus / map | `main/app/app_rs485.*`, `app_modbus_*`, `app_modbus_map_config.h`, `app_bus_bridge.*` |
