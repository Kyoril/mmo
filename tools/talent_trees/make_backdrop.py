# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Render the talent constellation backdrop (tier rings, path dividers, star dust) to HTEX.

    py tools/talent_trees/make_backdrop.py

The texture is centered on the talent tab hub and covers BACKDROP_RADIUS * STRETCH canvas
units to the left and right and BACKDROP_RADIUS up and down; TalentFrame.lua places it with
the same constants. The ring radii, path angles and stretch come from author_talent_trees.py,
so the rings run exactly through the talent nodes. The art is neutral (white / warm gold) so
every class can share it; path colors come from the links.

The texture is 2:1 while the covered area is STRETCH:1, so the image is drawn in canvas space
with separate x/y pixel scales: dots and line widths are compensated and look round in game.
"""

import math
import random
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from author_talent_trees import PATH_ANGLES, SLOTS, STRETCH  # noqa: E402

BACKDROP_RADIUS = 860          # canvas units covered vertically from the hub to the edge
WIDTH, HEIGHT = 2048, 1024     # final texture size in pixels
SUPERSAMPLE = 3
OUTPUT = ROOT / "data/client/Interface/GameUI/TalentConstellation.htex"
IMPORTER = ROOT / ".agents/skills/mmo-spell-designer/scripts/import_spell_icon.py"


def render():
    big_w, big_h = WIDTH * SUPERSAMPLE, HEIGHT * SUPERSAMPLE
    scale_x = big_w / (2.0 * BACKDROP_RADIUS * STRETCH)   # pixels per canvas unit
    scale_y = big_h / (2.0 * BACKDROP_RADIUS)
    cx, cy = big_w / 2.0, big_h / 2.0

    def point(radius, degrees):
        rad = math.radians(degrees)
        return cx + math.cos(rad) * radius * STRETCH * scale_x, cy + math.sin(rad) * radius * scale_y

    def ellipse_box(radius):
        rx, ry = radius * STRETCH * scale_x, radius * scale_y
        return [cx - rx, cy - ry, cx + rx, cy + ry]

    image = Image.new("RGBA", (big_w, big_h), (0, 0, 0, 0))

    # Soft light pooling around the hub and fading towards the outer ring.
    glow = Image.new("RGBA", (big_w, big_h), (0, 0, 0, 0))
    glow_draw = ImageDraw.Draw(glow)
    for step in range(60, 0, -1):
        alpha = int(26 * (1.0 - step / 60) ** 1.6)
        glow_draw.ellipse(ellipse_box(760 * step / 60), fill=(255, 226, 170, alpha))
    image = Image.alpha_composite(image, glow.filter(ImageFilter.GaussianBlur(8 * SUPERSAMPLE)))

    draw = ImageDraw.Draw(image)

    # Star dust: deterministic, sparser towards the center so the hub stays clean.
    rng = random.Random(4711)
    for _ in range(1400):
        distance = BACKDROP_RADIUS * math.sqrt(rng.random()) * 0.98
        if distance < 130:
            continue
        x, y = point(distance, rng.random() * 360.0)
        size = rng.choice((1.6, 1.6, 2.2, 3.0))     # canvas units
        rx, ry = size * scale_x, size * scale_y
        draw.ellipse([x - rx, y - ry, x + rx, y + ry], fill=(255, 244, 220, rng.randint(40, 120)))

    # Dividers between the paths, dashed, from just outside the hub to the outer ring.
    for path_angle in PATH_ANGLES:
        for start in range(160, 760, 32):
            draw.line([point(start, path_angle + 60), point(start + 17, path_angle + 60)],
                      fill=(220, 200, 160, 70), width=int(2.4 * scale_y))

    # Tier rings through the talent nodes: a faint wide band under a crisp thin line.
    ring_radii = sorted({slot[0] for slot in SLOTS.values()})
    for index, ring in enumerate(ring_radii):
        draw.ellipse(ellipse_box(ring), outline=(255, 230, 180, 22), width=int(16 * scale_y))
        draw.ellipse(ellipse_box(ring), outline=(240, 214, 160, 120 - index * 14), width=int(2.6 * scale_y))

    # Small diamonds where each path crosses each ring.
    for path_angle in PATH_ANGLES:
        for ring in ring_radii:
            x, y = point(ring, path_angle)
            dx, dy = 9 * scale_x, 9 * scale_y
            draw.polygon([(x, y - dy), (x + dx, y), (x, y + dy), (x - dx, y)], fill=(255, 236, 190, 90))

    return image.resize((WIDTH, HEIGHT), Image.LANCZOS)


def main():
    image = render()
    with tempfile.TemporaryDirectory(prefix="talent_backdrop_") as tmp:
        png = Path(tmp) / "TalentConstellation.png"
        image.save(png)
        preview = ROOT / "generated/talent_trees/TalentConstellation.png"
        preview.parent.mkdir(parents=True, exist_ok=True)
        image.save(preview)
        subprocess.run([sys.executable, str(IMPORTER), "--format", "dxt5", str(png), str(OUTPUT)], check=True)
    print(f"wrote {OUTPUT}")


if __name__ == "__main__":
    main()
