# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Site templates (tools/world/templates/<name>.json) and the placer that fits one onto a place.

A template lists roles. Each role queries assets by tag, wants a count, keeps a spacing to its own
items, says where its items go (rule) and whether they are props (wobj) or trees/plants (hfol).
Rules: at_anchor, ring, scatter, cluster, against_cliff, along_path, near_role. The placer is
deterministic for a seed, tries several spots per item, and keeps the first one that passes every
placement error check; unfillable roles become gaps (kind "asset": nothing matches the query;
kind "space": no valid spot), never silent drops.
"""

from __future__ import annotations

import json
import math
import random
from dataclasses import dataclass, replace
from pathlib import Path

import numpy as np

from .assets import select_assets
from .geometry import quat_from_yaw_tilt, quat_to_matrix
from .paths import templates_dir
from .prop_lint import PlacedProp, PropContext, lint_prop
from .terrain_kinds import ROAD_KINDS

RULE_TYPES = {"at_anchor", "ring", "scatter", "cluster", "against_cliff", "along_path", "near_role"}
ATTEMPTS = 40
GRID_STEP = 2.0
_DIRECTIONS = [(math.cos(a), math.sin(a)) for a in (i * math.pi / 4.0 for i in range(8))]


@dataclass(frozen=True)
class Role:
    name: str
    tags: tuple[str, ...]
    exclude: tuple[str, ...]
    size: str | None
    assets: tuple[str, ...] | None
    count: tuple[int, int]
    spacing: float
    store: str
    rule: dict
    scale: tuple[float, float] | None
    yaw: tuple[float, float] | None
    sink: float | None = None    # metres; overrides the asset tag's sink for placement depth only


@dataclass(frozen=True)
class Template:
    name: str
    description: str
    entry: str                   # "anchor" or a role name (its first item)
    clear: tuple[float, ...]     # radii around the anchor that stay empty
    notes: tuple[str, ...]       # copied into the review packet (e.g. "awaiting water")
    roles: tuple[Role, ...]


@dataclass
class PlacementResult:
    items: list[dict]
    gaps: list[dict]
    entry: tuple[float, float]


def load_template(name: str, directory: Path | None = None) -> Template:
    path = Path(directory or templates_dir()) / f"{name}.json"
    doc = json.loads(path.read_text(encoding="utf-8"))
    roles = []
    seen: set[str] = set()
    for raw in doc["roles"]:
        rule = raw["rule"]
        if raw["name"] in seen:
            raise ValueError(f"{path}: duplicate role name {raw['name']!r}")
        if rule.get("type") not in RULE_TYPES:
            raise ValueError(f"{path}: role {raw['name']!r} has unknown rule {rule.get('type')!r}")
        if raw.get("store", "wobj") not in ("wobj", "hfol"):
            raise ValueError(f"{path}: role {raw['name']!r} store must be wobj or hfol")
        if rule["type"] == "near_role" and rule.get("role") not in seen:
            raise ValueError(f"{path}: role {raw['name']!r} is near_role of {rule.get('role')!r}, which is not defined earlier")
        if rule["type"] == "ring" and ("r1" not in rule or "r2" not in rule):
            raise ValueError(f"{path}: ring role {raw['name']!r} needs r1 and r2")
        sink = raw.get("sink")
        if sink is not None:
            if isinstance(sink, bool) or not isinstance(sink, (int, float)) or sink < 0:
                raise ValueError(f"{path}: role {raw['name']!r} sink must be a number >= 0")
            sink = float(sink)
        seen.add(raw["name"])
        query = raw.get("query", {})
        roles.append(Role(raw["name"], tuple(query.get("tags", [])), tuple(query.get("exclude", [])), query.get("size"),
                          tuple(query["assets"]) if "assets" in query else None, tuple(raw["count"]),
                          float(raw.get("spacing", 0.0)), raw.get("store", "wobj"), rule,
                          tuple(raw["scale"]) if "scale" in raw else None, tuple(raw["yaw"]) if "yaw" in raw else None, sink))
    entry = doc.get("entry", "anchor")
    if entry != "anchor" and entry not in seen:
        raise ValueError(f"{path}: entry {entry!r} is not a role")
    return Template(doc["name"], doc.get("description", ""), entry, tuple(doc.get("clear", [])),
                    tuple(doc.get("notes", [])), tuple(roles))


def _local_rect(info, tags, scale):
    x0, z0, x1, z1 = info.footprint
    fs = tags.footprint_scale * scale
    return [(x0 * fs, z0 * fs), (x1 * fs, z0 * fs), (x1 * fs, z1 * fs), (x0 * fs, z1 * fs)]


def settle(info, tags, query, x: float, z: float, yaw: float, scale: float):
    """Height and tilt that put the asset on the ground: aligned assets follow the terrain normal; others
    stand upright with their lowest footprint corner on the ground. Both sink by half the tag's allowance."""
    ground = query.height_at(x, z)
    if ground is None:
        return None
    pitch = roll = 0.0
    if tags.align_to_slope:
        samples = [query.height_at(x + 1.0, z), query.height_at(x - 1.0, z), query.height_at(x, z + 1.0), query.height_at(x, z - 1.0)]
        if None in samples:
            return None
        normal = np.array([-(samples[0] - samples[1]) / 2.0, 1.0, -(samples[2] - samples[3]) / 2.0])
        normal /= np.linalg.norm(normal)
        roll = -math.degrees(math.asin(float(normal[0])))
        pitch = math.degrees(math.atan2(float(normal[2]), float(normal[1])))
        rotation = quat_to_matrix(quat_from_yaw_tilt(yaw, pitch, roll))
        y = ground - float((rotation @ np.array([0.0, info.bounds_min[1] * scale, 0.0]))[1]) - tags.sink * 0.5
    else:
        rotation = quat_to_matrix(quat_from_yaw_tilt(yaw))
        grounds = [ground]
        for lx, lz in _local_rect(info, tags, scale):
            wx, _, wz = rotation @ np.array([lx, 0.0, lz])
            h = query.height_at(x + float(wx), z + float(wz))
            if h is None:
                return None
            grounds.append(h)
        y = min(grounds) - tags.sink * 0.5 - info.bounds_min[1] * scale
    return round(y, 3), round(pitch, 2), round(roll, 2)


def item_rotation(item: dict):
    pitch, roll = item.get("tilt", [0.0, 0.0])
    return quat_from_yaw_tilt(float(item["yaw"]), float(pitch), float(roll))


def item_to_prop(item: dict, key: str) -> PlacedProp:
    return PlacedProp(key, item["asset"], item["store"], tuple(item["position"]), item_rotation(item),
                      float(item["scale"]), bool(item["collides"]))


def _in_disc(rng: random.Random, cx: float, cz: float, radius: float) -> tuple[float, float]:
    angle = rng.uniform(0.0, 2.0 * math.pi)
    distance = radius * math.sqrt(rng.random())
    return cx + math.cos(angle) * distance, cz + math.sin(angle) * distance


def _grid(cx, cz, radius):
    steps = int(radius // GRID_STEP)
    for j in range(-steps, steps + 1):
        for i in range(-steps, steps + 1):
            x, z = cx + i * GRID_STEP, cz + j * GRID_STEP
            if math.hypot(x - cx, z - cz) <= radius:
                yield x, z


def _cliff_spots(query, cx, cz, radius, min_slope, offset):
    spots = []
    for x, z in _grid(cx, cz, radius):
        own = query.slope_at(x, z)
        if own is None or own > 35.0:
            continue
        for dx, dz in _DIRECTIONS:
            nx, nz = x + dx * offset, z + dz * offset
            slope = query.slope_at(nx, nz)
            if slope is not None and slope >= min_slope:
                spots.append((x, z, math.degrees(math.atan2(x - nx, z - nz))))   # facing away from the cliff
                break
    return spots


def _path_spots(query, roads, cx, cz, radius):
    spots = [(x, z) for x, z in _grid(cx, cz, radius) if query.terrain_kind_at(x, z) in ROAD_KINDS]
    for road in roads:
        for (ax, az), (bx, bz) in zip(road, road[1:]):
            steps = max(1, int(math.hypot(bx - ax, bz - az) // GRID_STEP))
            for k in range(steps + 1):
                x, z = ax + (bx - ax) * k / steps, az + (bz - az) * k / steps
                if math.hypot(x - cx, z - cz) <= radius:
                    spots.append((x, z))
    return spots


def _candidate(role: Role, anchor, radius, context: PropContext, rng: random.Random, by_role: dict, state: dict):
    rule = role.rule
    kind = rule["type"]
    ax, az = anchor
    if kind == "at_anchor":
        ox, oz = rule.get("offset", [0.0, 0.0])
        return ax + ox, az + oz, None
    if kind == "ring":
        angle = rng.uniform(0.0, 2.0 * math.pi)
        distance = rng.uniform(float(rule["r1"]), float(rule["r2"]))
        return ax + math.cos(angle) * distance, az + math.sin(angle) * distance, None
    if kind == "scatter":
        x, z = _in_disc(rng, ax, az, radius * float(rule.get("radius_fraction", 0.9)))
        return x, z, None
    if kind == "cluster":
        if "centre" not in state:
            state["centre"] = _in_disc(rng, ax, az, radius * float(rule.get("radius_fraction", 0.6)))
        x, z = _in_disc(rng, *state["centre"], float(rule.get("spread", 4.0)))
        return x, z, None
    if kind == "against_cliff":
        if "cliff" not in state:
            state["cliff"] = _cliff_spots(context.query, ax, az, radius, float(rule.get("min_cliff_slope", 40.0)),
                                          float(rule.get("max_offset", 3.0)))
        if not state["cliff"]:
            return None
        x, z, facing = rng.choice(state["cliff"])
        return x + rng.uniform(-GRID_STEP / 2.0, GRID_STEP / 2.0), z + rng.uniform(-GRID_STEP / 2.0, GRID_STEP / 2.0), facing
    if kind == "along_path":
        if "path" not in state:
            state["path"] = _path_spots(context.query, context.roads, ax, az, radius)
        if not state["path"]:
            return None
        px, pz = rng.choice(state["path"])
        px += rng.uniform(-GRID_STEP / 2.0, GRID_STEP / 2.0)
        pz += rng.uniform(-GRID_STEP / 2.0, GRID_STEP / 2.0)
        lo, hi = rule.get("offset", [2.0, 5.0])
        angle = rng.uniform(0.0, 2.0 * math.pi)
        distance = rng.uniform(float(lo), float(hi))
        return px + math.cos(angle) * distance, pz + math.sin(angle) * distance, None
    if kind == "near_role":
        bases = by_role.get(rule["role"], [])
        if not bases:
            return None
        base = rng.choice(bases)["position"]
        lo, hi = rule.get("distance", [2.0, 5.0])
        angle = rng.uniform(0.0, 2.0 * math.pi)
        distance = rng.uniform(float(lo), float(hi))
        return base[0] + math.cos(angle) * distance, base[2] + math.sin(angle) * distance, None
    raise ValueError(f"unknown rule {kind!r}")


def place(template: Template, anchor, radius: float, context: PropContext, seed: int, keep_away=()) -> PlacementResult:
    """Fits the template onto a site. keep_away: extra (x, z, r) circles no item (footprint included, at_anchor
    roles too) may touch. The template's own clear radii exempt only at_anchor roles."""
    rng = random.Random(seed)
    items: list[dict] = []
    props: list[PlacedProp] = []
    gaps: list[dict] = []
    by_role: dict[str, list[dict]] = {}
    clear = [(anchor[0], anchor[1], r) for r in template.clear]
    keep_away = list(keep_away)
    for role in template.roles:
        assets = select_assets(context.catalog, context.rules, tags=role.tags, exclude=role.exclude, size=role.size,
                               allow=list(role.assets) if role.assets is not None else None)
        if role.store == "hfol":
            assets = [a for a in assets if a.endswith(".hmsh")]
        wanted = rng.randint(*role.count)
        if not assets:
            gaps.append({"role": role.name, "kind": "asset", "wanted": wanted, "placed": 0})
            continue
        state: dict = {}
        circles = keep_away if role.rule["type"] == "at_anchor" else clear + keep_away   # only the template's own clear radii exempt at_anchor
        for _ in range(wanted):
            for _attempt in range(ATTEMPTS):
                spot = _candidate(role, anchor, radius, context, rng, by_role, state)
                if spot is None:
                    break
                x, z, facing = spot
                if any(math.hypot(x - o["position"][0], z - o["position"][2]) < role.spacing for o in by_role.get(role.name, [])):
                    continue
                asset = rng.choice(assets)
                info, tags = context.catalog[asset], context.tags_for(asset)
                lo, hi = role.scale or tags.scale
                scale = round(rng.uniform(lo, hi), 2)
                footprint = info.radius * scale * tags.footprint_scale
                if any(math.hypot(x - cx, z - cz) < r + footprint for cx, cz, r in circles):
                    continue
                if facing is not None:
                    yaw = facing + rng.uniform(-20.0, 20.0)
                else:
                    yaw = rng.uniform(*(role.yaw or (0.0, 360.0)))
                # The role's sink only changes placement depth; the lint below keeps judging with the tag's own sink.
                settle_tags = replace(tags, sink=role.sink) if role.sink is not None else tags
                settled = settle(info, settle_tags, context.query, x, z, yaw, scale)
                if settled is None:
                    continue
                y, pitch, roll = settled
                collides = tags.collides_override if tags.collides_override is not None else info.has_collision
                item = {"role": role.name, "asset": asset, "store": role.store,
                        "position": [round(x, 2), round(y, 2), round(z, 2)], "yaw": round(yaw % 360.0, 2),
                        "tilt": [pitch, roll], "scale": scale, "collides": bool(collides)}
                prop = item_to_prop(item, f"draft:{len(items)}")
                problems = [v for v in lint_prop(prop, context, [*context.existing, *props])
                            if v.severity == "error" or v.rule == "prop_outside_poi"]
                if problems:
                    continue
                items.append(item)
                props.append(prop)
                by_role.setdefault(role.name, []).append(item)
                break
        placed = len(by_role.get(role.name, []))
        if placed < wanted:
            gaps.append({"role": role.name, "kind": "space", "wanted": wanted, "placed": placed})
    entry = tuple(anchor)
    if template.entry != "anchor" and by_role.get(template.entry):
        first = by_role[template.entry][0]["position"]
        entry = (first[0], first[2])
    return PlacementResult(items, gaps, entry)
