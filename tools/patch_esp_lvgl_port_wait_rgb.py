# -*- coding: utf-8 -*-
"""Ensure lvgl_port_disp_wait_rgb_trans_done exists in managed esp_lvgl_port (LVGL8).

Re-run after component-manager re-downloads. Safe / idempotent.
"""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HDR = ROOT / "managed_components" / "espressif__esp_lvgl_port" / "include" / "esp_lvgl_port_disp.h"
SRC = (
    ROOT
    / "managed_components"
    / "espressif__esp_lvgl_port"
    / "src"
    / "lvgl8"
    / "esp_lvgl_port_disp.c"
)

DECL = """
/**
 * @brief Wait until RGB/DPI frame transfer completes (vsync / frame_buf_complete)
 *
 * Used after a manual `esp_lcd_panel_draw_bitmap()` outside the LVGL flush path
 * (e.g. board_display_present_fb). Safe when no transfer semaphore exists
 * (single FB / no avoid_tearing): returns ESP_ERR_NOT_SUPPORTED.
 *
 * @param disp           LVGL display
 * @param ticks_to_wait  FreeRTOS ticks (portMAX_DELAY to block)
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG
 *      - ESP_ERR_NOT_SUPPORTED if no transfer semaphore
 *      - ESP_ERR_TIMEOUT
 */
esp_err_t lvgl_port_disp_wait_rgb_trans_done(lv_display_t *disp, uint32_t ticks_to_wait);
"""

IMPL = r"""
esp_err_t lvgl_port_disp_wait_rgb_trans_done(lv_disp_t *disp, uint32_t ticks_to_wait)
{
    if (disp == NULL || disp->driver == NULL || disp->driver->user_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lvgl_port_display_ctx_t *disp_ctx = (lvgl_port_display_ctx_t *)disp->driver->user_data;
    if (disp_ctx->trans_sem == NULL) {
        /* Single-FB / no avoid_tearing path — nothing to wait on. */
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Drop a stale count, then wait for the next vsync/frame-complete give. */
    (void)xSemaphoreTake(disp_ctx->trans_sem, 0);
    if (xSemaphoreTake(disp_ctx->trans_sem, ticks_to_wait) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
"""


def patch_header() -> str:
    if not HDR.is_file():
        return f"skip: {HDR} missing"
    text = HDR.read_text(encoding="utf-8")
    if "lvgl_port_disp_wait_rgb_trans_done" in text:
        return "ok: header already patched"
    needle = "esp_err_t lvgl_port_remove_disp(lv_display_t *disp);"
    if needle not in text:
        return "skip: unexpected header"
    text = text.replace(needle, needle + "\n" + DECL, 1)
    HDR.write_text(text, encoding="utf-8")
    return "patched: header"


def patch_src() -> str:
    if not SRC.is_file():
        return f"skip: {SRC} missing"
    text = SRC.read_text(encoding="utf-8")
    msgs = []
    if "lvgl_port_disp_wait_rgb_trans_done" not in text:
        anchor = "void lvgl_port_flush_ready(lv_disp_t *disp)\n{\n    assert(disp);\n    assert(disp->driver);\n    lv_disp_flush_ready(disp->driver);\n}\n"
        if anchor not in text:
            return "skip: unexpected flush_ready block"
        text = text.replace(
            anchor,
            anchor + "\n" + IMPL.lstrip("\n"),
            1,
        )
        msgs.append("impl")
    old = (
        "                /* Waiting for the last frame buffer to complete transmission */\n"
        "                xSemaphoreTake(disp_ctx->trans_sem, 0);\n"
        "                xSemaphoreTake(disp_ctx->trans_sem, portMAX_DELAY);"
    )
    new = (
        "                /* Waiting for the last frame buffer to complete transmission */\n"
        "                if (disp_ctx->trans_sem) {\n"
        "                    xSemaphoreTake(disp_ctx->trans_sem, 0);\n"
        "                    xSemaphoreTake(disp_ctx->trans_sem, portMAX_DELAY);\n"
        "                }"
    )
    if old in text:
        text = text.replace(old, new, 1)
        msgs.append("null-check")
    if msgs:
        SRC.write_text(text, encoding="utf-8")
        return "patched: src (" + ",".join(msgs) + ")"
    return "ok: src already patched"


def main() -> int:
    print(patch_header())
    print(patch_src())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
