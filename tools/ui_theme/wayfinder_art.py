# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Draw the Wayfinder UI ornaments and write mipmapped RGBA HTEX files."""
from math import cos, exp, pi, sin
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
    if name == "PanelRim":
        base = colors[-1]
        for y in range(128):
            for x in range(128):
                if image.getpixel((x, y)) == base:
                    sheen = round(5 * (1 - y / 127) + 1.1 * sin(x * 0.19 + y * 0.32))
                    image.putpixel((x, y), tuple(v + sheen for v in base[:3]) + (255,))
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


def interface_glyphs():
    paths = {
        "Character": [[(12, 31), (14, 18), (22, 10), (32, 6), (42, 10), (50, 18), (52, 31), (45, 49), (32, 56), (19, 49), (12, 31)],
                      [(14, 24), (32, 32), (50, 24)], [(32, 11), (32, 51)], [(20, 40), (26, 43)], [(38, 43), (44, 40)]],
        "Book": [[(32, 17), (24, 12), (9, 12), (9, 46), (24, 46), (32, 52), (40, 46), (55, 46), (55, 12), (40, 12), (32, 17), (32, 52)],
                 [(16, 23), (25, 25)], [(16, 32), (25, 34)], [(39, 25), (48, 23)], [(39, 34), (48, 32)]],
        "Craft": [[(10, 19), (53, 19), (47, 28), (37, 32), (35, 42), (46, 49), (18, 49), (28, 42), (26, 33), (15, 30), (10, 19)],
                  [(17, 13), (44, 13)]],
        "Emotes": [[(12, 14), (52, 14), (55, 19), (55, 39), (50, 44), (30, 44), (16, 54), (18, 44), (9, 44), (9, 19), (12, 14)],
                   [(21, 28), (24, 28)], [(31, 28), (34, 28)], [(41, 28), (44, 28)]],
        "Bag": [[(16, 24), (48, 24), (53, 43), (51, 53), (13, 53), (11, 43), (16, 24)], [(22, 24), (22, 15), (32, 10), (42, 15), (42, 24)],
                [(14, 33), (28, 39), (36, 39), (50, 33)], [(29, 36), (29, 43), (36, 43), (36, 36)]],
        "Quests": [[(15, 14), (49, 14), (43, 21), (43, 48), (48, 52), (16, 52), (21, 46), (21, 21), (15, 14)],
                   [(29, 24), (36, 24)], [(29, 32), (36, 32)], [(29, 40), (36, 40)]],
        "Talents": [[(32, 45), (32, 21)], [(32, 38), (17, 29), (14, 23)], [(32, 38), (47, 29), (50, 23)],
                    [(32, 9), (38, 15), (32, 21), (26, 15), (32, 9)], [(32, 45), (39, 52), (32, 59), (25, 52), (32, 45)],
                    [(14, 11), (20, 17), (14, 23), (8, 17), (14, 11)], [(50, 11), (56, 17), (50, 23), (44, 17), (50, 11)]],
        "Report": [[(21, 24), (32, 17), (43, 24), (45, 43), (32, 53), (19, 43), (21, 24)], [(32, 26), (32, 48)],
                   [(22, 25), (12, 16)], [(42, 25), (52, 16)], [(19, 33), (8, 32)], [(45, 33), (56, 32)],
                   [(20, 42), (11, 48)], [(44, 42), (53, 48)]],
        "Close": [[(18, 18), (46, 46)], [(46, 18), (18, 46)]],
        "Chevron": [[(15, 25), (32, 41), (49, 25)]],
        "Plus": [[(16, 32), (48, 32)], [(32, 16), (32, 48)]],
        "Minus": [[(16, 32), (48, 32)]],
        "Moon": [[(42, 11), (32, 17), (25, 28), (26, 39), (34, 47), (46, 48), (37, 54), (24, 54), (13, 46), (9, 34), (13, 22), (23, 13), (33, 9), (42, 11)]],
    }
    for name, strokes in paths.items():
        image = Image.new("RGBA", (64, 64))
        draw = ImageDraw.Draw(image)
        for stroke in strokes:
            draw.line(stroke, fill=IVORY, width=4, joint="curve")
        save(f"Glyph{name}", image)

    for name, steps in (("Settings", 8), ("Sun", 8)):
        image = Image.new("RGBA", (64, 64))
        draw = ImageDraw.Draw(image)
        draw.ellipse((19, 19, 45, 45), outline=IVORY, width=4)
        for i in range(steps):
            angle = i * 2 * pi / steps
            p = lambda r: (32 + r * cos(angle), 32 + r * sin(angle))
            draw.line((p(16), p(26)), fill=IVORY, width=5 if name == "Settings" else 3)
        save(f"Glyph{name}", image)


def map_ring():
    ring = Image.new("RGBA", (512, 512))
    draw = ImageDraw.Draw(ring)
    draw.ellipse((3, 3, 508, 508), outline=(12, 17, 22, 255), width=21)
    draw.ellipse((6, 6, 505, 505), outline=GOLD, width=3)
    draw.ellipse((17, 17, 494, 494), outline=SHADE, width=3)
    for i in range(24):
        a = i * pi / 12
        p = lambda r: (255.5 + r * cos(a), 255.5 + r * sin(a))
        draw.line((p(242), p(247)), fill=GOLD, width=3 if i % 3 == 0 else 2)
    for a in (0, pi / 2, pi, 3 * pi / 2):
        p = lambda r, d: (255.5 + r * cos(a) - d * sin(a), 255.5 + r * sin(a) + d * cos(a))
        points = [p(254, 0), p(238, 14), p(221, 0), p(238, -14)]
        draw.polygon(points, fill=(16, 22, 27, 255))
        draw.line(points + points[:1], fill=GOLD, width=2)
    save("MapRing", ring)


def meter_ornaments():
    def silhouette(inset):
        far = 127 - inset
        shoulder = 45 + inset
        return [(inset, 63), (shoulder, inset), (127 - shoulder, inset),
                (far, 63), (127 - shoulder, far), (shoulder, far)]

    metals = [(8, 11, 15, 255), (99, 74, 47, 255), (229, 194, 135, 255),
              (204, 165, 107, 255), (71, 57, 41, 255), (25, 24, 22, 255)]
    for name, inner in (("MeterWell", (14, 19, 23, 255)), ("MeterRim", (0, 0, 0, 0))):
        rail = Image.new("RGBA", (128, 128))
        draw = ImageDraw.Draw(rail)
        for inset, color in zip((1, 3, 5, 8, 10, 12), metals):
            draw.polygon(silhouette(inset), fill=color)
        draw.polygon(silhouette(15), fill=inner)
        save(name, rail)

    fill = Image.new("RGBA", (128, 128))
    mask = Image.new("L", fill.size)
    ImageDraw.Draw(mask).polygon(silhouette(1), fill=255)
    draw = ImageDraw.Draw(fill)
    stops = [(0, 100), (8, 220), (18, 253), (45, 221), (83, 152), (111, 103), (125, 64), (127, 43)]
    for (y0, light0), (y1, light1) in zip(stops, stops[1:]):
        for y in range(y0, y1 + 1):
            light = round(light0 + (light1 - light0) * (y - y0) / (y1 - y0))
            draw.line((0, y, 127, y), fill=(light, light, light, 255))
    fill.putalpha(mask)
    save("MeterFill", fill)
    shadow = Image.new("RGBA", (128, 32))
    for y in range(32):
        for x in range(128):
            dx, dy = abs(x - 63.5) / 63.5, abs(y - 15.5) / 15.5
            if dx < 0.60 and dy < 0.98:
                alpha = round(204 * exp(-12 * dx**4 - 1.5 * dy**4) * (1 - (dx / 0.60)**3) * (1 - dy**3))
                shadow.putpixel((x, y), (8, 11, 15, alpha))
    save("MeterLabelShade", shadow)


def quest_tracker_art():
    ink = Image.new("RGBA", (256, 128))
    for y in range(128):
        v = min(1, y / 8, (127 - y) / 14)
        for x in range(256):
            u = x / 255
            alpha = round(198 * v * min(1, x / 12) * (1 - u**2.2))
            ink.putpixel((x, y), (9, 14, 19, alpha))
    save("TrackerInk", ink)
    trail = Image.new("RGBA", (4, 64))
    draw = ImageDraw.Draw(trail)
    for y in range(64):
        draw.line((0, y, 3, y), fill=(185, 149, 93, round(142 * (1 - y / 63))))
    save("TrackerTrail", trail)

    for name in ("Quest", "QuestDone", "QuestFailed"):
        glyph = Image.new("RGBA", (64, 64))
        draw = ImageDraw.Draw(glyph)
        points = [(32, 8), (55, 32), (32, 56), (9, 32)]
        draw.polygon(points, fill=(12, 17, 23, 230))
        draw.line(points + points[:1], fill=IVORY, width=4)
        if name == "QuestDone":
            draw.line([(20, 32), (28, 40), (44, 24)], fill=IVORY, width=4)
        elif name == "QuestFailed":
            draw.line((32, 20, 32, 35), fill=IVORY, width=5)
            draw.ellipse((30, 40, 34, 44), fill=IVORY)
        else:
            draw.polygon([(32, 23), (41, 32), (32, 41), (23, 32)], fill=IVORY)
        save("Glyph" + name, glyph)


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    frame("PanelRim", [(9, 10, 13, 230), (167, 140, 98, 255), (226, 194, 143, 255),
                       (85, 72, 54, 255), (16, 22, 27, 255)], 17)
    frame("SocketRim", [(9, 10, 13, 230), (104, 108, 110, 255), (164, 164, 150, 255),
                        (45, 48, 48, 255), (13, 20, 26, 246)], 20)
    frame("Selection", [(22, 19, 14, 175), (167, 128, 76, 255), (230, 192, 133, 230),
                        (95, 73, 45, 255), (70, 55, 34, 160)], 16)
    underline()
    map_ring()
    meter_ornaments()
    quest_tracker_art()
    interface_glyphs()
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
