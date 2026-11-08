# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Draw the Wayfinder UI ornaments and write mipmapped RGBA HTEX files."""
from math import cos, pi, sin
from pathlib import Path
import struct

from PIL import Image, ImageDraw

OUTPUT = Path(__file__).resolve().parents[2] / "data/client/Interface/GameUI/Themes/Wayfinder/Textures"
GOLD = (220, 187, 128, 255)
IVORY = (251, 222, 166, 255)
SHADE = (98, 79, 54, 255)


def save(name, image):
    width, height = image.size
    pixels = []
    while True:
        pixels.append(image.tobytes())
        if image.size == (1, 1):
            break
        image = image.resize((max(1, image.width // 2), max(1, image.height // 2)), Image.Resampling.LANCZOS)
    lengths = [len(mip) for mip in pixels]
    offsets = [142 + sum(lengths[:i]) for i in range(len(lengths))]
    padding = [0] * (16 - len(lengths))
    header = struct.pack("<4sIBBHH32I", b"HTEX", 0x0100, 1, 1, width, height, *offsets, *padding, *lengths, *padding)
    (OUTPUT / f"{name}.htex").write_bytes(header + b"".join(pixels))


def octagon(size, inset, cut):
    lo, hi = inset, size - 1 - inset
    return [(lo + cut, lo), (hi - cut, lo), (hi, lo + cut), (hi, hi - cut),
            (hi - cut, hi), (lo + cut, hi), (lo, hi - cut), (lo, lo + cut)]


def frame(name, colors, cut):
    image = Image.new("RGBA", (128, 128))
    draw = ImageDraw.Draw(image)
    for inset, color in enumerate(colors, 1):
        draw.polygon(octagon(128, inset, cut), fill=color)
    save(name, image)


def underline():
    image = Image.new("RGBA", (32, 32))
    ImageDraw.Draw(image).line((0, 29, 31, 29), fill=(204, 175, 127, 224), width=3)
    save("Underline", image)


def compass():
    image = Image.new("RGBA", (128, 128))
    draw = ImageDraw.Draw(image)
    for i in range(16):
        angle = i * pi / 8
        direction = (cos(angle), sin(angle))
        point = lambda radius: (64 + radius * direction[0], 64 + radius * direction[1])
        draw.line((point(38), point(60 if i % 4 == 0 else 45)), fill=SHADE, width=2)
    draw.line([(64, 17), (86, 64), (64, 111), (42, 64), (64, 17)], fill=GOLD, width=3)
    draw.polygon([(64, 26), (76, 64), (64, 76), (52, 64)], fill=SHADE)
    draw.polygon([(64, 26), (64, 67), (52, 64)], fill=IVORY)
    draw.polygon([(64, 102), (76, 64), (64, 67)], fill=GOLD)
    save("Compass", image)


def class_glyph(name):
    image = Image.new("RGBA", (64, 64))
    draw = ImageDraw.Draw(image)
    if name == "Warrior":
        for edge_x in (15, 49):
            grip_x = 64 - edge_x
            draw.line((edge_x, 14, grip_x, 50), fill=GOLD, width=6)
            draw.line((grip_x - 8, 42, grip_x + 8, 42), fill=IVORY, width=4)
    elif name == "Scout":
        draw.arc((9, 7, 53, 53), 315, 112, fill=GOLD, width=4)
        draw.line((15, 50, 50, 13), fill=IVORY, width=3)
        draw.line((15, 16, 48, 49), fill=GOLD, width=3)
        draw.line([(39, 49), (48, 49), (48, 40)], fill=GOLD, width=3)
    elif name == "Mage":
        for i in range(8):
            a = i * pi / 4
            draw.line((32, 32, 32 + 24 * cos(a), 32 + 24 * sin(a)), fill=GOLD, width=3)
        draw.polygon([(32, 22), (42, 32), (32, 42), (22, 32)], fill=IVORY)
    elif name == "Cleric":
        draw.line([(12, 16), (32, 10), (52, 16), (48, 39), (32, 54), (16, 39), (12, 16)], fill=GOLD, width=4)
        draw.line((32, 20, 32, 41), fill=IVORY, width=4)
        draw.line((22, 29, 42, 29), fill=IVORY, width=4)
    else:
        draw.polygon([(32, 6), (45, 23), (50, 38), (43, 51), (32, 57), (21, 51), (14, 38), (24, 25)], fill=GOLD)
        draw.polygon([(34, 26), (40, 40), (36, 49), (27, 47), (24, 41)], fill=IVORY)
    save(name, image)


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    frame("PanelRim", [(9, 10, 13, 230), (167, 140, 98, 255), (226, 194, 143, 255),
                       (100, 85, 63, 255), (26, 32, 38, 255)], 17)
    frame("SocketRim", [(9, 10, 13, 230), (104, 108, 110, 255), (164, 164, 150, 255),
                        (45, 48, 48, 255), (13, 20, 26, 246)], 20)
    frame("Selection", [(22, 19, 14, 175), (167, 128, 76, 255), (230, 192, 133, 230),
                        (95, 73, 45, 255), (70, 55, 34, 160)], 16)
    underline()
    ring = Image.new("RGBA", (512, 512))
    draw = ImageDraw.Draw(ring)
    draw.ellipse((3, 3, 508, 508), outline=(10, 12, 14, 244), width=24)
    draw.ellipse((6, 6, 505, 505), outline=GOLD, width=3)
    draw.ellipse((17, 17, 494, 494), outline=SHADE, width=4)
    save("MapRing", ring)
    compass()
    ticks = Image.new("RGBA", (256, 8))
    draw = ImageDraw.Draw(ticks)
    for x in range(32, 256, 32):
        draw.line((x, 0, x, 7), fill=(0, 0, 0, 130))
        draw.line((x + 1, 0, x + 1, 7), fill=(255, 233, 196, 70))
    save("MeterTicks", ticks)
    shade = Image.new("RGBA", (4, 32))
    draw = ImageDraw.Draw(shade)
    for y in range(32):
        draw.line((0, y, 3, y), fill=(4, 6, 8, 184 * y // 31))
    save("ShortcutShade", shade)
    for name in ("Warrior", "Scout", "Mage", "Cleric", "Acolyte"):
        class_glyph(name)
    for name, color in (("Online", (125, 208, 151, 255)), ("Offline", (157, 165, 169, 255))):
        image = Image.new("RGBA", (32, 32))
        draw = ImageDraw.Draw(image)
        draw.ellipse((3, 3, 28, 28), fill=color if name == "Online" else None, outline=color, width=4)
        save(name, image)


if __name__ == "__main__":
    main()
