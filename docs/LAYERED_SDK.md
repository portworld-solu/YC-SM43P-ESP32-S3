# SDK layers (fully open source)

## English

Older releases shipped `board_bsp` / `hub_core` / `cloud_wifi` / `bt_ctrl` as prebuilt `lib/esp32s3/*.a`.  
**Current tree uses source components** — readable, editable, rebuildable.

| Component | Sources | Contents |
|-----------|---------|----------|
| `board_bsp` | `components/board_bsp/src/` | RGB panel, touch, expander, Ethernet, backlight |
| `hub_core` | `components/hub_core/src/hub_model.c` | Rooms / controls model |
| `cloud_wifi` | `components/cloud_wifi/src/` | Wi‑Fi STA + Bemfa MQTT |
| `bt_ctrl` | `components/bt_ctrl/src/` | BLE / Mesh |

Pins & panel: [BOARD_PINOUT.md](./BOARD_PINOUT.md) · Architecture: [ARCHITECTURE_EN.md](./ARCHITECTURE_EN.md)

---

## 中文

历史上板级 / Wi‑Fi / BT / `hub_model` 曾以预编译 `.a` 交付。  
**当前版本全部改为源码组件**，可阅读、修改、重新编译。

| 组件 | 源码位置 | 内容 |
|------|----------|------|
| `board_bsp` | `components/board_bsp/src/` | RGB / 触摸 / 扩展器 / 以太网 / 背光 |
| `hub_core` | `components/hub_core/src/hub_model.c` | 房间与控件模型 |
| `cloud_wifi` | `components/cloud_wifi/src/` | Wi‑Fi + 巴法 MQTT |
| `bt_ctrl` | `components/bt_ctrl/src/` | BLE / Mesh |

引脚与屏参：[BOARD_PINOUT.md](./BOARD_PINOUT.md)
