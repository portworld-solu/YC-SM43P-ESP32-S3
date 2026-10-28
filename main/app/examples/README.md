# Bus bridge example (Modbus + LVGL)

**Product path (preferred):** the integrated bridge + point table are already in
`main/app/app_bus_bridge.*` and `app_modbus_map_config.h`. Edit the map, rebuild,
and use `app_bus_bridge_notify_widget(room, slot)` from UI callbacks.

This `*_example.*` pair is a **minimal reference** (write queue only, no map/poll).
Do not copy over the product files unless you intentionally replace them.

Guide: `docs/api_guide/zh/guides/G01_modbus_rs485_lvgl.md`  
User note: `docs/user_notes/SM43P-ESP32/zh/07_用屏控RS485设备.md`
