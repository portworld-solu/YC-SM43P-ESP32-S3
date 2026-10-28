# Agent notes

Public Hub UI SDK for **YC-SM43P** ([portworld-solu](https://github.com/portworld-solu)).

- Architecture: [docs/ARCHITECTURE_EN.md](docs/ARCHITECTURE_EN.md)
- Pins / schematics: [docs/BOARD_PINOUT.md](docs/BOARD_PINOUT.md)
- Code layout: [main/STRUCTURE.md](main/STRUCTURE.md)

Do not block RS485/Modbus inside LVGL callbacks. Prefer `ui/themes/` and `main/app` for product changes.
