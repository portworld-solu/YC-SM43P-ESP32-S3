# hub_ui — Hub 界面开放层

```text
hub_ui/
├── core/       路由 · 壳 · 左右滑 · 字体 · 图标 · i18n
├── pages/      各功能页构建（设置 / 网络 / 房间 / 日程 / 待机…）
├── features/   可独立演进的能力（如温湿度 UI 补偿）
└── stubs/      对接封闭库的运行时桩
```

主题实现仍在 `ui/themes/<id>/`，通过 `hub_theme_build()` 调用本目录的 `hub_build_*`。

头文件短名不变：`#include "hub_ui.h"`（见 `main/CMakeLists.txt` include 路径）。
