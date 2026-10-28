# Hub UI stubs

| File | Role |
|------|------|
| `ui.h` / `ui_runtime.h` / `wifi_ui_font.h` | Headers included by `cloud_wifi` / BT |
| `ui_runtime_stubs.c` | Built by **main** — Hub implementations / no-ops |
| ~~`ui_compat_stubs.c` / `wifi_ui_font_stub.c`~~ | **Moved** to `components/cloud_wifi/src/` (must link with `wifi_management.c`) |

Do not re-add SquareLine object definitions here — see [docs/ARCHITECTURE_EN.md](../../../docs/ARCHITECTURE_EN.md).
