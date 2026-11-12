# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Draw the flat Glue UI primitives (panel fill/line, fades, glow, dot) and write mipmapped RGBA HTEX files.

Every texture is white with an alpha shape: the XML tints it, so one file serves every state colour.
Run from the repository root: py -3 tools/ui_theme/glue_art.py
"""
from math import sqrt
from pathlib import Path
import struct

from PIL import Image, ImageDraw

OUTPUT = Path(__file__).resolve().parents[2] / "data/client/Interface/GlueUI/Art"
SUPERSAMPLE = 4


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
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / f"{name}.htex").write_bytes(header + b"".join(pixels))


def octagon(size, inset, cut):
    lo, hi = inset, size - inset
    return [(lo + cut, lo), (hi - cut, lo), (hi, lo + cut), (hi, hi - cut),
            (hi - cut, hi), (lo + cut, hi), (lo, hi - cut), (lo, lo + cut)]


def white_mask(mask):
    image = Image.new("RGBA", mask.size, (255, 255, 255, 0))
    image.putalpha(mask)
    return image


def panel(size=128, cut=14, line=3):
    """Chamfered panel as two layers: a solid fill and a hairline outline drawn on top of it.

    Use with borderSize="24": the 14 px chamfer stays inside the 9-slice corners.
    """
    big = size * SUPERSAMPLE
    fill = Image.new("L", (big, big), 0)
    ImageDraw.Draw(fill).polygon(octagon(big, 0, cut * SUPERSAMPLE), fill=255)
    save("PanelFill", white_mask(fill.resize((size, size), Image.Resampling.LANCZOS)))

    outline = Image.new("L", (big, big), 0)
    draw = ImageDraw.Draw(outline)
    draw.polygon(octagon(big, 0, cut * SUPERSAMPLE), fill=255)
    inner = line * SUPERSAMPLE
    draw.polygon(octagon(big, inner, cut * SUPERSAMPLE - inner * 0.41), fill=0)
    save("PanelLine", white_mask(outline.resize((size, size), Image.Resampling.LANCZOS)))


def smoothstep(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def fades():
    """FadeH: opaque at the left, gone at the right. FadeV: gone at the top, opaque at the bottom.
    FadeVTop is FadeV upside down (opaque at the top)."""
    width = 256
    horizontal = Image.new("L", (width, 8))
    vertical = Image.new("L", (8, width))
    for i in range(width):
        value = round(255 * (1 - smoothstep(i / (width - 1))))
        for j in range(8):
            horizontal.putpixel((i, j), value)
            vertical.putpixel((j, width - 1 - i), value)
    save("FadeH", white_mask(horizontal))
    save("FadeV", white_mask(vertical))
    save("FadeVTop", white_mask(vertical.transpose(Image.Transpose.FLIP_TOP_BOTTOM)))


def glow(size=256):
    """Soft radial light, for the halo behind a selected card or the character."""
    mask = Image.new("L", (size, size))
    centre = (size - 1) / 2
    for y in range(size):
        for x in range(size):
            d = sqrt((x - centre) ** 2 + (y - centre) ** 2) / centre
            mask.putpixel((x, y), round(255 * (1 - smoothstep(d))))
    save("Glow", white_mask(mask))


def dot(size=64):
    big = size * SUPERSAMPLE
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).ellipse((SUPERSAMPLE, SUPERSAMPLE, big - SUPERSAMPLE, big - SUPERSAMPLE), fill=255)
    save("Dot", white_mask(mask.resize((size, size), Image.Resampling.LANCZOS)))


def plus(size=64, line=5, arm=20):
    big = size * SUPERSAMPLE
    mask = Image.new("L", (big, big), 0)
    draw = ImageDraw.Draw(mask)
    c = big / 2
    half = line * SUPERSAMPLE / 2
    reach = arm * SUPERSAMPLE
    draw.rectangle((c - reach, c - half, c + reach, c + half), fill=255)
    draw.rectangle((c - half, c - reach, c + half, c + reach), fill=255)
    save("Plus", white_mask(mask.resize((size, size), Image.Resampling.LANCZOS)))


def chevron(name, size=64, line=5, flip=False, rotate=0):
    big = size * SUPERSAMPLE
    mask = Image.new("L", (big, big), 0)
    draw = ImageDraw.Draw(mask)
    s = SUPERSAMPLE
    points = [(24 * s, 14 * s), (42 * s, 32 * s), (24 * s, 50 * s)]
    if flip:
        points = [(big - x, y) for x, y in points]
    draw.line(points, fill=255, width=line * s, joint="curve")
    if rotate:
        mask = mask.rotate(rotate)
    save(name, white_mask(mask.resize((size, size), Image.Resampling.LANCZOS)))


def main():
    panel()
    fades()
    glow()
    dot()
    plus()
    chevron("ChevronRight")
    chevron("ChevronLeft", flip=True)
    chevron("ChevronUp", rotate=90)
    chevron("ChevronDown", rotate=-90)


if __name__ == "__main__":
    main()
