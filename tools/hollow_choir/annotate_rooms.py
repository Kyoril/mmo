# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Labels the rooms of the Hollow Choir on the survey image from survey_layout.py and draws a 21-unit
aggro circle as a scale bar. Coordinates are world units read off the survey; north (-Z) is up.

    py -3 tools/hollow_choir/survey_layout.py
    py -3 tools/hollow_choir/annotate_rooms.py
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
SURVEY = ROOT / "generated/hollow_choir/layout.png"
OUT = ROOT / "generated/hollow_choir/layout_rooms.png"

# Must match survey_layout.py's full-map framing (bounds -130..17 / -48..49, 4 units margin, 8 px/unit).
X0, Z0, PPU = -134.0, -52.0, 8

ROOMS = [
    ("Eingang", 15, 0),
    ("01 Vorhalle (y 1.2)", -5, -1),
    ("02 Totenwache (y 0.2)", -5, -28),
    ("? Suedhof, keine Waende", -6, 20),
    ("G1", -31, 0),
    ("03 Kirchenschiff (y 0.2)", -62, -9),
    ("Sakristei?", -37, -20),
    ("Nischen", -46, 22),
    ("Treppe (kein Navmesh)", -82, 11),
    ("06 Apsis? (y 5.2)", -103, -14),
    ("04 Kreuzgang (y 5.2)", -98, 32),
    ("Gang", -96, 45),
    ("Treppe runter (y -2.8)", -132, 23),
]

AGGRO = 21.0


def main():
    image = Image.open(SURVEY).convert("RGBA")
    overlay = Image.new("RGBA", image.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    font = ImageFont.load_default()

    def px(x, z):
        return (x - X0) * PPU, (z - Z0) * PPU

    for label, x, z in ROOMS:
        cx, cy = px(x, z)
        box = draw.textbbox((cx, cy), label, font=font, anchor="mm")
        draw.rectangle([box[0] - 3, box[1] - 2, box[2] + 3, box[3] + 2], fill=(0, 0, 0, 190))
        draw.text((cx, cy), label, fill=(255, 255, 255, 255), font=font, anchor="mm")

    # Scale bar: one aggro circle in the nave.
    cx, cy = px(-62, 2)
    r = AGGRO * PPU
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(255, 70, 70, 230), width=2)
    draw.text((cx, cy + r + 8), f"Aggro-Radius {AGGRO:g}", fill=(255, 120, 120, 255), font=font, anchor="mm")

    Image.alpha_composite(image, overlay).convert("RGB").save(OUT)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
