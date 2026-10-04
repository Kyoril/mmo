# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Render an offline preview of every class talent tree the way TalentFrame.lua draws it
(backdrop, hub, path links in their accent colors, nodes, labels).

    py tools/talent_trees/preview.py [--learned 6,111,113]

Writes generated/talent_trees/preview_<class>.png. A quick way to iterate on the layout
without starting the dev stack; the real client stays the final check. ``--learned`` marks
talent ids as learned so lit links can be judged too.
"""

import argparse
import importlib.util
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import author_talent_trees as att  # noqa: E402
from make_backdrop import BACKDROP_RADIUS, render as render_backdrop  # noqa: E402

HALF_W, HALF_H = round(BACKDROP_RADIUS * att.STRETCH), BACKDROP_RADIUS

spec = importlib.util.spec_from_file_location(
    "htex_tool", ROOT / ".agents/skills/mmo-material-editor/scripts/htex_tool.py")
htex = importlib.util.module_from_spec(spec)
spec.loader.exec_module(htex)

NODE_SIZE, HUB_SIZE = 128, 190
CLASS_NAMES = {0: "mage", 1: "warrior", 2: "cleric", 3: "acolyte", 4: "scout"}
_icons = {}


def icon(path, size):
    key = (path, size)
    if key not in _icons:
        info = htex.read_htex(ROOT / "data/client" / path)
        mip = min(info["mips"], key=lambda m: abs(m["width"] - size))["index"]
        _icons[key] = htex.decode_mip(info, mip).convert("RGBA").resize((size, size), Image.LANCZOS)
    return _icons[key]


def argb(color, alpha=None):
    a = (color >> 24) & 0xFF if alpha is None else alpha
    return ((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, a)


def render(tab, talents, spells, learned):
    canvas = Image.new("RGBA", (tab.canvas_width, tab.canvas_height), (14, 17, 21, 255))
    backdrop = render_backdrop().resize((HALF_W * 2, HALF_H * 2), Image.LANCZOS)
    layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    layer.paste(backdrop, (tab.hub_x - HALF_W, tab.hub_y - HALF_H))
    canvas.alpha_composite(layer)

    by_id = {t.id: t for t in talents}
    glow = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    lines = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    glow_draw, line_draw = ImageDraw.Draw(glow), ImageDraw.Draw(lines)

    def link(start, end, color, lit, to_placeholder):
        if to_placeholder:
            line_draw.line([start, end], fill=(138, 143, 153, 0x50), width=3)
        elif lit:
            glow_draw.line([start, end], fill=argb(color, 0x45), width=22)
            line_draw.line([start, end], fill=argb(color), width=7)
        else:
            line_draw.line([start, end], fill=argb(color, 0x70), width=4)

    for t in talents:
        pos = (t.position_x, t.position_y)
        if not t.prerequisites:
            link((tab.hub_x, tab.hub_y), pos, t.accent_color, t.id in learned, t.placeholder)
        for p in t.prerequisites:
            src = by_id[p.talent_id]
            link((src.position_x, src.position_y), pos, t.accent_color,
                 src.id in learned and t.id in learned, t.placeholder)
    canvas.alpha_composite(glow)
    canvas.alpha_composite(lines)

    # Hub
    hub = icon(tab.icon, HUB_SIZE - 44)
    draw = ImageDraw.Draw(canvas)
    r = HUB_SIZE // 2
    draw.ellipse([tab.hub_x - r - 30, tab.hub_y - r - 30, tab.hub_x + r + 30, tab.hub_y + r + 30], fill=(255, 213, 138, 40))
    draw.rectangle([tab.hub_x - r, tab.hub_y - r, tab.hub_x + r, tab.hub_y + r], fill=(60, 50, 35, 255), outline=(217, 192, 138, 255), width=6)
    canvas.alpha_composite(hub, (tab.hub_x - hub.width // 2, tab.hub_y - hub.height // 2))

    font = ImageFont.truetype(str(ROOT / "data/client/Locales/Locale_enUS/Fonts/FRIZQT__.TTF"), 34)
    small = ImageFont.truetype(str(ROOT / "data/client/Locales/Locale_enUS/Fonts/FRIZQT__.TTF"), 20)
    for t in talents:
        size = int(NODE_SIZE * t.node_scale)
        x, y = t.position_x - size // 2, t.position_y - size // 2
        draw.rectangle([x, y, x + size, y + size], fill=(40, 34, 26, 255), outline=(170, 155, 128, 255), width=5)
        img = icon(spells[t.ranks[0]].icon, size - 32)
        if t.placeholder:
            img = Image.blend(img, Image.new("RGBA", img.size, (40, 42, 48, 255)), 0.7)
        canvas.alpha_composite(img, (x + 16, y + 16))
        if not t.placeholder:
            rank = len(t.ranks) if t.id in learned else 0
            draw.text((x + size - 8, y + size - 6), f"{rank}/{len(t.ranks)}", font=small, anchor="rb",
                      fill=(255, 209, 0) if rank else (160, 168, 178), stroke_width=2, stroke_fill=(0, 0, 0))

    for label in tab.labels:
        text = att.UI_STRINGS.get(label.text)
        name = next(texts[1] for cls in att.CLASSES for key, texts, _, _ in cls["paths"] if key == label.text)
        draw.text((label.x, label.y), name, font=font, anchor="mm", fill=argb(label.color), stroke_width=2, stroke_fill=(0, 0, 0))
    return canvas


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--learned", default="")
    args = parser.parse_args()
    learned = {int(x) for x in args.learned.split(",") if x}

    spells = {s.id: s for s in att.load_editor("spells").entry}
    talents = att.load_editor("talents").entry
    out = ROOT / "generated/talent_trees"
    out.mkdir(parents=True, exist_ok=True)
    for tab in att.load_editor("talent_tabs").entry:
        if not tab.hub_x:
            continue
        image = render(tab, [t for t in talents if t.tab == tab.id], spells, learned)
        path = out / f"preview_{CLASS_NAMES[tab.class_id]}.png"
        image.convert("RGB").save(path)
        print(path)


if __name__ == "__main__":
    main()
