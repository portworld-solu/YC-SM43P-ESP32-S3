#!/usr/bin/env python3
"""Convert white-on-black PNGs → spiffs_image/icons/nt/*.png (96×96 RGBA)."""
from __future__ import annotations

from pathlib import Path

from PIL import Image

SRC = Path(r"C:\Users\A\.cursor\projects\e-show-lvglframe-customer-sdk\assets")
DST = Path(r"e:\show\lvglframe\customer_sdk\spiffs_image\icons\nt")

# Bus page: RS485 + MQTT
MAPPING = [
    ("c__Users_A_AppData_Roaming_Cursor_User_workspaceStorage_ebc5c1869c037ac129fb502e76a2ec4f_images_RS485-8f51dd94-840a-4df5-8263-e87e97516779.png",
     ["rs485.png"]),
    ("c__Users_A_AppData_Roaming_Cursor_User_workspaceStorage_ebc5c1869c037ac129fb502e76a2ec4f_images_mqtt-100b1e24-ceb7-4f75-9e74-a6f56d265c13.png",
     ["mqtt.png", "link.png"]),
]


def to_icon(im: Image.Image, size: int = 96) -> Image.Image:
    im = im.convert("RGBA")
    im.thumbnail((size, size), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.paste(im, ((size - im.width) // 2, (size - im.height) // 2), im)
    pix = canvas.load()
    w, h = canvas.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = pix[x, y]
            lum = (r + g + b) / 3.0
            if lum < 28:
                pix[x, y] = (0, 0, 0, 0)
            else:
                pix[x, y] = (255, 255, 255, int(min(255, max(0, lum))))
    return canvas


def main() -> None:
    DST.mkdir(parents=True, exist_ok=True)
    for src_name, outs in MAPPING:
        sp = SRC / src_name
        if not sp.exists():
            print("MISSING", src_name)
            continue
        icon = to_icon(Image.open(sp))
        for out in outs:
            op = DST / out
            icon.save(op, optimize=True)
            print(f"OK -> {out} ({op.stat().st_size} B)")
    print("done")


if __name__ == "__main__":
    main()
