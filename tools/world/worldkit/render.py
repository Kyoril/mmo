# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Top-down map rendering for review packets: relief, water, roads, props, zones, named places,
spawns, quest links, plus an optional before/after diff.

Orientation matches the in-game minimap (src/mmo_client/ui/minimap.cpp): north is up, and north is
*smaller* world Z. Image right = +X (east), image down = +Z (south).
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from .atlas import Atlas
from .constants import CELL_SIZE

ALL_LAYERS = ("relief", "water", "roads", "entities", "zones", "pois", "spawns", "quests")
LEGEND_WIDTH = 240
_NO_TERRAIN = (32, 32, 32)
_ZONE_TINTS = [(118, 160, 92), (160, 150, 96), (100, 140, 150), (150, 110, 140), (170, 130, 90), (110, 150, 120),
               (140, 140, 100), (120, 120, 160), (160, 120, 110), (100, 160, 140), (150, 150, 150), (130, 110, 90)]
_UNZONED = (140, 140, 140)
_LEVEL_COLORS = [(46, 204, 113), (163, 203, 56), (241, 196, 15), (230, 126, 34), (231, 76, 60), (192, 57, 43), (142, 68, 173)]
_STATUS_COLORS = {"placeholder": (255, 165, 0), "canon": (255, 255, 255), "note": (255, 64, 255)}
_ROAD_COLOR = (181, 140, 90)
_WATER_COLOR = (40, 90, 200)
_QUEST_COLOR = (255, 255, 120)


@dataclass
class RenderOptions:
    bbox: tuple[float, float, float, float] | None = None   # x0, z0, x1, z1
    px_per_m: float | None = None
    max_side: int = 2048
    layers: frozenset = field(default_factory=lambda: frozenset(ALL_LAYERS))
    color_by: str = "level"
    title: str = ""


def _font(size: int):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()


def world_to_pixel(bbox, scale: float, x: float, z: float) -> tuple[float, float]:
    x0, z0, _, _ = bbox
    return (x - x0) * scale, (z - z0) * scale


def level_color(level: int) -> tuple[int, int, int]:
    return _LEVEL_COLORS[min(max((level - 1) // 3, 0), len(_LEVEL_COLORS) - 1)]


def faction_color(faction: int) -> tuple[int, int, int]:
    return _ZONE_TINTS[faction % len(_ZONE_TINTS)]


def _hillshade(height: np.ndarray) -> np.ndarray:
    finite = np.isfinite(height)
    filled = np.where(finite, height, float(np.nanmean(height)) if finite.any() else 0.0)
    grad_z, grad_x = np.gradient(filled, CELL_SIZE)
    # The light comes from the image's upper left: north-west, i.e. from -x/-z.
    normal = np.stack([-grad_x, np.ones_like(filled), -grad_z], axis=-1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    light = np.array([-1.0, 1.4, -1.0])
    light /= np.linalg.norm(light)
    return np.clip(normal @ light, 0.0, 1.0)


def _label(draw: ImageDraw.ImageDraw, xy, text: str, color, font) -> None:
    draw.text(xy, text, fill=color, font=font, stroke_width=2, stroke_fill=(0, 0, 0))


def _raster(snapshot, bbox, scale, width, height, options, kinds) -> np.ndarray:
    x0, z0, _, _ = bbox
    ox, oz = snapshot.origin
    rows, cols = snapshot.height.shape
    xs = x0 + (np.arange(width) + 0.5) / scale
    zs = z0 + (np.arange(height) + 0.5) / scale
    cx = np.floor((xs - ox) / CELL_SIZE).astype(np.int64)
    cz = np.floor((zs - oz) / CELL_SIZE).astype(np.int64)
    in_x = (cx >= 0) & (cx < cols)
    in_z = (cz >= 0) & (cz < rows)
    CZ, CX = np.meshgrid(np.clip(cz, 0, rows - 1), np.clip(cx, 0, cols - 1), indexing="ij")
    valid = in_z[:, None] & in_x[None, :] & np.isfinite(snapshot.height[CZ, CX])

    area = snapshot.area[CZ, CX]
    tints = np.array(_ZONE_TINTS, np.float64)
    base = np.where((area > 0)[..., None], tints[area % len(_ZONE_TINTS)], np.array(_UNZONED, np.float64))
    if "relief" in options.layers:
        shade = _hillshade(snapshot.height)[CZ, CX]
        base = base * (0.35 + 0.65 * shade[..., None])
    if "water" in options.layers:
        depth = snapshot.water_depth[CZ, CX]
        alpha = np.where(depth > 0, np.minimum(1.0, 0.45 + depth / 4.0), 0.0)[..., None]
        base = base * (1 - alpha) + np.array(_WATER_COLOR) * alpha
    if "roads" in options.layers and kinds is not None:
        road = kinds.road_mask(snapshot)[CZ, CX]
        base = np.where(road[..., None], base * 0.3 + np.array(_ROAD_COLOR) * 0.7, base)
    base = np.where(snapshot.hole[CZ, CX][..., None], 0.0, base)
    if "zones" in options.layers:
        border = np.zeros(area.shape, bool)
        border[1:, :] |= area[1:, :] != area[:-1, :]
        border[:, 1:] |= area[:, 1:] != area[:, :-1]
        base = np.where(border[..., None], np.array((20, 20, 20)), base)
    base = np.where(valid[..., None], base, np.array(_NO_TERRAIN))
    return np.clip(base, 0, 255).astype(np.uint8)


def _legend(image: Image.Image, scale: float, options: RenderOptions, bbox) -> None:
    draw = ImageDraw.Draw(image)
    left = image.width - LEGEND_WIDTH
    draw.rectangle([left, 0, image.width, image.height], fill=(24, 24, 24))
    font, small = _font(14), _font(11)
    y = 8
    if options.title:
        draw.text((left + 10, y), options.title, fill=(255, 255, 255), font=font)
        y += 22
    draw.text((left + 10, y), "north up (-Z), east right (+X)", fill=(200, 200, 200), font=small)
    y += 16
    draw.text((left + 10, y), f"x {bbox[0]:.0f}..{bbox[2]:.0f}  z {bbox[1]:.0f}..{bbox[3]:.0f}", fill=(200, 200, 200), font=small)
    y += 22
    nice = [10, 25, 50, 100, 250, 500, 1000, 2500]
    metres = max([n for n in nice if n * scale <= LEGEND_WIDTH - 40] or [nice[0]])
    draw.line([left + 10, y, left + 10 + metres * scale, y], fill=(255, 255, 255), width=3)
    draw.text((left + 10, y + 4), f"{metres} m", fill=(255, 255, 255), font=small)
    y += 26
    if options.color_by == "level":
        draw.text((left + 10, y), "spawn level (max)", fill=(255, 255, 255), font=small)
        y += 16
        for index, color in enumerate(_LEVEL_COLORS):
            draw.ellipse([left + 12, y + 2, left + 20, y + 10], fill=color)
            label = f"{index * 3 + 1}-{index * 3 + 3}" if index < len(_LEVEL_COLORS) - 1 else f"{index * 3 + 1}+"
            draw.text((left + 26, y), label, fill=(220, 220, 220), font=small)
            y += 14
    else:
        draw.text((left + 10, y), "spawn colour = faction template", fill=(255, 255, 255), font=small)
        y += 16
    draw.text((left + 10, y), "white dot = world object", fill=(220, 220, 220), font=small)
    y += 14
    draw.text((left + 10, y), "hollow dot = inactive spawn", fill=(220, 220, 220), font=small)
    y += 20
    for status, color in _STATUS_COLORS.items():
        draw.ellipse([left + 12, y + 2, left + 20, y + 10], outline=color, width=2)
        draw.text((left + 26, y), f"place: {status}", fill=(220, 220, 220), font=small)
        y += 14
    for text, color in (("road / path", _ROAD_COLOR), ("water", _WATER_COLOR), ("quest giver -> objective", _QUEST_COLOR),
                        ("added (diff)", (60, 220, 60)), ("removed (diff)", (230, 40, 40)), ("prop / building", (90, 90, 90))):
        draw.rectangle([left + 12, y + 3, left + 20, y + 9], fill=color)
        draw.text((left + 26, y), text, fill=(220, 220, 220), font=small)
        y += 14


def dump_state(snapshot, spawns) -> dict:
    """Positions of spawns and entities, for a later --diff render."""
    return {
        "spawns": {s.key: [round(s.x, 1), round(s.z, 1)] for s in spawns},
        "entities": {str(e.unique_id): [round(e.position[0], 1), round(e.position[2], 1)] for e in snapshot.entities},
    }


def _cross(draw, px, py, color) -> None:
    draw.line([px - 5, py - 5, px + 5, py + 5], fill=color, width=2)
    draw.line([px - 5, py + 5, px + 5, py - 5], fill=color, width=2)


def render_map(snapshot, *, spawns, unit_levels, unit_factions, atlas: Atlas | None, kinds, quest_arrows,
               options: RenderOptions, diff_state: dict | None = None) -> Image.Image:
    bbox = options.bbox or snapshot.extent
    width_m, height_m = bbox[2] - bbox[0], bbox[3] - bbox[1]
    scale = options.px_per_m or min(options.max_side / width_m, options.max_side / height_m)
    width, height = max(1, int(math.ceil(width_m * scale))), max(1, int(math.ceil(height_m * scale)))

    pixels = _raster(snapshot, bbox, scale, width, height, options, kinds)
    image = Image.new("RGB", (width + LEGEND_WIDTH, height), (24, 24, 24))
    image.paste(Image.fromarray(pixels, "RGB"), (0, 0))
    draw = ImageDraw.Draw(image)
    font, small = _font(13), _font(11)

    def to_px(x, z):
        return world_to_pixel(bbox, scale, x, z)

    if "entities" in options.layers:
        for entity in snapshot.entities:
            px, py = to_px(entity.position[0], entity.position[2])
            if entity.kind == "wmo":
                draw.rectangle([px - 4, py - 4, px + 4, py + 4], outline=(60, 40, 20), width=2)
            else:
                draw.rectangle([px - 1.5, py - 1.5, px + 1.5, py + 1.5], fill=(90, 90, 90))

    if atlas is not None and "roads" in options.layers:
        for road in atlas.roads:
            points = [to_px(x, z) for x, z in road["points"]]
            color = _STATUS_COLORS.get(road.get("status"), (255, 255, 255))
            draw.line(points, fill=color, width=3)
            _label(draw, (points[0][0] + 4, points[0][1] + 4), road["name"], color, small)

    if atlas is not None and "pois" in options.layers:
        for poi in atlas.pois:
            color = _STATUS_COLORS.get(poi.get("status"), (255, 255, 255))
            px, py = to_px(*poi["center"])
            if "radius" in poi:
                r = max(4.0, poi["radius"] * scale)
                draw.ellipse([px - r, py - r, px + r, py + r], outline=color, width=2)
            else:
                draw.polygon([to_px(x, z) for x, z in poi["polygon"]], outline=color, width=2)
            text = poi["name"] + (" ?" if poi.get("ask") else "")
            _label(draw, (px + 6, py - 16), text, color, font)

    if "spawns" in options.layers:
        for spawn in spawns:
            if spawn.waypoints:
                draw.line([to_px(w[0], w[2]) for w in spawn.waypoints], fill=(200, 200, 255), width=1)
            px, py = to_px(spawn.x, spawn.z)
            if spawn.kind == "object":
                color = (240, 240, 240)
            elif options.color_by == "faction":
                color = faction_color(unit_factions.get(spawn.entry, 0))
            else:
                color = level_color(unit_levels.get(spawn.entry, (1, 1))[1])
            box = [px - 3, py - 3, px + 3, py + 3]
            if spawn.active:
                draw.ellipse(box, fill=color, outline=(0, 0, 0))
            else:
                draw.ellipse(box, outline=color, width=2)

    if "quests" in options.layers:
        for quest_id, giver, source in quest_arrows:
            gx, gy = to_px(*giver)
            sx, sy = to_px(*source)
            draw.line([gx, gy, sx, sy], fill=_QUEST_COLOR, width=2)
            angle = math.atan2(sy - gy, sx - gx)
            head = [(sx, sy), (sx - 9 * math.cos(angle - 0.4), sy - 9 * math.sin(angle - 0.4)),
                    (sx - 9 * math.cos(angle + 0.4), sy - 9 * math.sin(angle + 0.4))]
            draw.polygon(head, fill=_QUEST_COLOR)
            _label(draw, ((gx + sx) / 2, (gy + sy) / 2), f"Q{quest_id}", _QUEST_COLOR, small)

    if diff_state is not None:
        current = dump_state(snapshot, spawns)
        before_spawns = diff_state.get("spawns", {})
        for key, (x, z) in current["spawns"].items():
            if key not in before_spawns:
                px, py = to_px(x, z)
                draw.ellipse([px - 7, py - 7, px + 7, py + 7], outline=(60, 220, 60), width=2)
        for key, (x, z) in before_spawns.items():
            if key not in current["spawns"]:
                _cross(draw, *to_px(x, z), (230, 40, 40))
        before = diff_state.get("entities", {})
        for key, (x, z) in current["entities"].items():
            if key not in before:
                px, py = to_px(x, z)
                draw.rectangle([px - 6, py - 6, px + 6, py + 6], outline=(60, 220, 60), width=2)
            elif math.hypot(before[key][0] - x, before[key][1] - z) > 0.5:
                draw.line([*to_px(*before[key]), *to_px(x, z)], fill=(255, 140, 0), width=2)
        for key, (x, z) in before.items():
            if key not in current["entities"]:
                _cross(draw, *to_px(x, z), (230, 40, 40))

    _legend(image, scale, options, bbox)
    return image
