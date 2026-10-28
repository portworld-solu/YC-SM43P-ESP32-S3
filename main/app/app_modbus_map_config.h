/**
 * @file app_modbus_map_config.h
 * @brief ★ 控件类型 + Modbus 点表 — 出厂/客户固件的唯一推荐配置入口
 *
 * ## 产品约定（Customer SDK）
 *
 * - **默认**：屏上不提供「类型 / MB」编辑（`CONFIG_HUB_UI_ENGINEER_EDIT=n`）。
 * - 工程师在本文件改 **房间 · 槽位 · 控件类型 · 从站 · 寄存器**，重新编译烧录。
 * - 房间内控件实例（名称等）由 `hub_model` 出厂表提供（`components/hub_core` 源码）；
 *   本表 `(room,slot,type)` 必须与之一致，否则总线编解码会对错控件。
 * - 默认不写 NVS（`CONFIG_APP_MODBUS_MAP_PERSIST_NVS=n`），避免旧屏上改动盖住本文件。
 *
 * ## 如何改一盏灯 / 一个点
 *
 * 1. 找到对应 `APP_MB_ROW(room, slot, type, encode)` 行（或新增一行）。
 * 2. 需要自定义从站/地址时，不要用宏，改为手写 `app_modbus_binding_t` 初始化
 *    （见下方「手写示例」注释）。
 * 3. `idf.py build flash`；上电后点表即生效。
 *
 * ## 地址约定（宏默认，可改）
 *
 *   slave = room + 1
 *   addr  = room * 0x0010 + slot
 *   开关类 ONOFF；调光/帘/温控 LEVEL
 *
 * ## 默认布局（与 hub_model 出厂名对齐）
 *
 *   客厅0: 主灯 灯带 窗帘 卷帘 空调 地暖 风扇 插座  (8 → 房间页翻页)
 *   主卧1: 床头灯 夜灯
 *   厨房2: 厨灯 台面灯 排风扇
 *   书房3: 顶灯 桌插
 *
 * ## 工程师屏上调试（可选）
 *
 * menuconfig → App features → 打开 `HUB_UI_ENGINEER_EDIT` 与
 * `APP_MODBUS_MAP_PERSIST_NVS`，才可在「房间编辑」里改类型/MB 并写入 NVS。
 */

#pragma once

#include "app_modbus_map.h"

/* helper macro: room, slot, type, encode — slave/addr 按约定自动填 */
#define APP_MB_ROW(_room, _slot, _type, _enc)                                  \
    {                                                                          \
        .enabled = true,                                                       \
        .room = (_room),                                                       \
        .slot = (_slot),                                                       \
        .type = (_type),                                                       \
        .slave = (uint8_t)((_room) + 1),                                       \
        .wr_addr = (uint16_t)((_room) * 0x0010u + (uint16_t)(_slot)),          \
        .rd_addr = (uint16_t)((_room) * 0x0010u + (uint16_t)(_slot)),          \
        .poll = true,                                                          \
        .encode = (_enc),                                                      \
        .on_val = 1,                                                           \
        .off_val = 0,                                                          \
        .scale_num = 1,                                                        \
        .scale_den = 1,                                                        \
        .offset = 0,                                                           \
    }

/*
 * 手写示例（替换某一行宏即可）：
 *
 *  { .enabled = true, .room = 0, .slot = 0, .type = HUB_W_ONOFF,
 *    .slave = 1, .wr_addr = 0x0000, .rd_addr = 0x0000, .poll = true,
 *    .encode = APP_MB_ENC_ONOFF, .on_val = 1, .off_val = 0,
 *    .scale_num = 1, .scale_den = 1, .offset = 0 },
 */

static const app_modbus_binding_t s_app_modbus_map_seed[] = {
    /* —— 客厅 room0 (8) —— */
    APP_MB_ROW(0, 0, HUB_W_ONOFF, APP_MB_ENC_ONOFF),   /* 主灯 Ceiling */
    APP_MB_ROW(0, 1, HUB_W_DIMMER, APP_MB_ENC_LEVEL),  /* 灯带 Strip */
    APP_MB_ROW(0, 2, HUB_W_CURTAIN, APP_MB_ENC_LEVEL), /* 窗帘 */
    APP_MB_ROW(0, 3, HUB_W_SHUTTER, APP_MB_ENC_LEVEL), /* 卷帘 */
    APP_MB_ROW(0, 4, HUB_W_CLIM, APP_MB_ENC_LEVEL),    /* 空调 */
    APP_MB_ROW(0, 5, HUB_W_THERMO, APP_MB_ENC_LEVEL),  /* 地暖 */
    APP_MB_ROW(0, 6, HUB_W_FAN, APP_MB_ENC_ONOFF),     /* 风扇 */
    APP_MB_ROW(0, 7, HUB_W_PLUG, APP_MB_ENC_ONOFF),    /* 插座 */

    /* —— 主卧 room1 —— */
    APP_MB_ROW(1, 0, HUB_W_ONOFF, APP_MB_ENC_ONOFF),  /* 床头灯 Bedside */
    APP_MB_ROW(1, 1, HUB_W_DIMMER, APP_MB_ENC_LEVEL), /* 夜灯 Night */

    /* —— 厨房 room2 —— */
    APP_MB_ROW(2, 0, HUB_W_ONOFF, APP_MB_ENC_ONOFF),  /* 厨灯 */
    APP_MB_ROW(2, 1, HUB_W_DIMMER, APP_MB_ENC_LEVEL), /* 台面灯 Counter */
    APP_MB_ROW(2, 2, HUB_W_FAN, APP_MB_ENC_ONOFF),    /* 排风扇 Exhaust */

    /* —— 书房 room3 —— */
    APP_MB_ROW(3, 0, HUB_W_ONOFF, APP_MB_ENC_ONOFF), /* 顶灯 */
    APP_MB_ROW(3, 1, HUB_W_PLUG, APP_MB_ENC_ONOFF),  /* 桌插 Desk */
};

#undef APP_MB_ROW
