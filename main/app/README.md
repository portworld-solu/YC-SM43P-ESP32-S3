# README — main/app open APIs

| Header | Role | Doc |
|--------|------|-----|
| `app_api.h` | Umbrella | — |
| `app_rs485.h` | RS485 transport | `docs/api_guide/zh/10_gpio_rs485.md` |
| `app_modbus_rtu.h` | Modbus FC03/06 | `docs/api_guide/zh/11_modbus.md` |
| `app_modbus_map.h` | Per-widget encode/decode | edit `app_modbus_map_config.h` |
| `app_bus_bridge.h` | UI queue write + status poll | `guides/G01_modbus_rs485_lvgl.md` |
| `app_beep.h` | Beep pulses | `09_beep.md` |
| `app_coexist.h` | Radio/ETH coexist | `12_coexist.md` |
| `app_wifi_util.h` | Scan helpers | `04_wifi.md` |
| `app_gpio_out.h` | DE / GPIO_OUT | `10_gpio_rs485.md` |

**Secondary-dev start:** `docs/api_guide/zh/00_secondary_dev.md`  
**Modbus+LVGL recipe:** `docs/api_guide/zh/guides/G01_modbus_rs485_lvgl.md`  
**Point-table (edit this):** `app_modbus_map_config.h`  
**Minimal example (reference):** `examples/app_bus_bridge_example.c`
