# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Placement checks for props and trees (spec 2, section 4.6).

Every check uses the prop's rotated footprint rectangle from the asset catalog (bounds x scale, trees
shrunk to their trunk by footprint_scale), not just its origin. Errors are placements a player would
notice as broken; warnings are design smells. Tags relax rules (sink, max_slope, may_overlap).
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np

from .atlas import contains
from .geometry import quat_to_matrix
from .lint import Violation
from .spawns import is_grid_pack
from .terrain_kinds import ROAD_KINDS, SUBMERGED_DEPTH

FLOAT_TOLERANCE = 0.3
ROAD_CLEARANCE = 3.0
WATER_LIMIT = 0.3
GRID_MIN = 5


@dataclass(frozen=True)
class PlacedProp:
    key: str                                       # "wobj:<id>", "hfol:<id>" or "<pass>:<index>"
    asset: str
    store: str                                     # "wobj" | "hfol"
    position: tuple[float, float, float]
    rotation: tuple[float, float, float, float]    # (w, x, y, z)
    scale: float
    collides: bool

    @property
    def x(self) -> float:
        return self.position[0]

    @property
    def z(self) -> float:
        return self.position[2]


@dataclass
class PropContext:
    query: object
    catalog: dict
    rules: object
    existing: list[PlacedProp]
    spawns: list = field(default_factory=list)
    poi: dict | None = None
    roads: list = field(default_factory=list)      # atlas road point lists [[x, z], ...]
    _tags: dict = field(default_factory=dict, init=False, repr=False)   # asset path -> AssetTags

    def tags_for(self, asset: str):
        """Tag rules resolved for an asset path, cached (resolution scans every rule and is pure)."""
        tags = self._tags.get(asset)
        if tags is None:
            tags = self._tags[asset] = self.rules.for_asset(asset)
        return tags


def props_from_entities(entities) -> list[PlacedProp]:
    return [PlacedProp(f"wobj:{e.unique_id}", e.asset.replace("\\", "/"), "wobj", e.position, e.rotation,
                       max(abs(e.scale[0]), abs(e.scale[2])), True) for e in entities]


def props_from_foliage(foliage: dict) -> list[PlacedProp]:
    return [PlacedProp(f"hfol:{i.unique_id}", i.mesh.replace("\\", "/"), "hfol", i.position, i.rotation,
                       max(abs(i.scale[0]), abs(i.scale[2])), i.collides) for ff in foliage.values() for i in ff.instances]


def footprint_corners(prop: PlacedProp, info, tags) -> np.ndarray:
    """World positions of the four bottom corners of the (tag-shrunk) footprint rectangle."""
    x0, z0, x1, z1 = info.footprint
    fs = tags.footprint_scale
    y = info.bounds_min[1]
    local = np.array([[x0 * fs, y, z0 * fs], [x1 * fs, y, z0 * fs], [x1 * fs, y, z1 * fs], [x0 * fs, y, z1 * fs]]) * prop.scale
    return local @ quat_to_matrix(prop.rotation).T + np.asarray(prop.position, float)


def bottom_center(prop: PlacedProp, info) -> np.ndarray:
    return quat_to_matrix(prop.rotation) @ np.array([0.0, info.bounds_min[1] * prop.scale, 0.0]) + np.asarray(prop.position, float)


def _quads_overlap(a: np.ndarray, b: np.ndarray) -> bool:
    """Separating-axis test for two convex quads given as (4, 2) arrays."""
    for poly in (a, b):
        for i in range(4):
            edge = poly[(i + 1) % 4] - poly[i]
            axis = np.array([-edge[1], edge[0]])
            pa, pb = a @ axis, b @ axis
            if pa.max() <= pb.min() + 1e-6 or pb.max() <= pa.min() + 1e-6:
                return False
    return True


def _inside_quad(point, quad: np.ndarray) -> bool:
    """Point-in-convex-quad (boundary counts as inside); a degenerate (zero-area) quad contains nothing."""
    sign = None
    for i in range(4):
        edge = quad[(i + 1) % 4] - quad[i]
        cross = edge[0] * (point[1] - quad[i][1]) - edge[1] * (point[0] - quad[i][0])
        if abs(cross) < 1e-9:
            continue
        if sign is None:
            sign = cross > 0
        elif (cross > 0) != sign:
            return False
    return sign is not None


def _segment_distance(px, pz, ax, az, bx, bz) -> float:
    dx, dz = bx - ax, bz - az
    length = dx * dx + dz * dz
    t = 0.0 if length == 0 else max(0.0, min(1.0, ((px - ax) * dx + (pz - az) * dz) / length))
    return math.hypot(px - (ax + t * dx), pz - (az + t * dz))


def _painted_road(query, x: float, z: float) -> bool:
    return query.terrain_kind_at(x, z) in ROAD_KINDS and query.water_depth_at(x, z) <= SUBMERGED_DEPTH


def lint_prop(prop: PlacedProp, context: PropContext, others: list[PlacedProp]) -> list[Violation]:
    found: list[Violation] = []
    name = prop.asset.rsplit("/", 1)[-1]

    def add(rule: str, severity: str, text: str) -> None:
        found.append(Violation(rule, severity, prop.key, f"{name} at ({prop.x:.1f}, {prop.z:.1f}): {text}", prop.x, prop.z))

    info = context.catalog.get(prop.asset)
    if info is None or info.error:
        add("prop_unknown_asset", "error", "asset is not in the catalog or unreadable")
        return found
    q = context.query
    tags = context.tags_for(prop.asset)
    corners = footprint_corners(prop, info, tags)
    quad = corners[:, [0, 2]]
    points = [(prop.x, prop.z)] + [(float(c[0]), float(c[2])) for c in corners]
    if any(not q.has_terrain(px, pz) for px, pz in points) or not q.edge_clear(prop.x, prop.z):
        add("prop_terrain_edge", "error", "footprint reaches the terrain edge")
        return found
    if q.hole_at(prop.x, prop.z):
        add("prop_over_hole", "error", "stands over a terrain hole")
    depth = q.water_depth_at(prop.x, prop.z)
    if depth > WATER_LIMIT and not tags.tags & {"pier", "bridge"}:
        add("prop_in_water", "error", f"base is under {depth:.1f} m of water")
    gap = max(float(c[1]) - q.height_at(float(c[0]), float(c[2])) for c in corners)
    if gap > FLOAT_TOLERANCE:
        add("prop_floating", "error", f"a footprint corner floats {gap:.2f} m above the ground")
    bottom = bottom_center(prop, info)
    buried = q.height_at(prop.x, prop.z) - float(bottom[1])
    if buried > tags.sink + FLOAT_TOLERANCE:
        add("prop_buried", "error", f"base is {buried:.2f} m below the ground (allowed {tags.sink:.2f} m)")
    slope = max(q.slope_at(px, pz) or 0.0 for px, pz in points)
    if slope > tags.max_slope:
        add("prop_slope", "error", f"ground slope {slope:.0f} deg exceeds {tags.max_slope:.0f} deg for this asset")
    radius = info.radius * prop.scale * tags.footprint_scale
    near_road = any(_painted_road(q, px, pz) for px, pz in points) or any(
        _segment_distance(prop.x, prop.z, *road[i], *road[i + 1]) - radius < ROAD_CLEARANCE
        for road in context.roads for i in range(len(road) - 1))
    if near_road:
        add("prop_on_road", "error" if prop.collides else "warning", "stands on or next to a road")
    for other in others:
        if other.key == prop.key:
            continue
        other_info = context.catalog.get(other.asset)
        if other_info is None or other_info.error:
            continue
        distance = math.hypot(other.x - prop.x, other.z - prop.z)
        # Conservative bound without tags (footprint_scale only ever shrinks), so far-apart pairs never resolve tags.
        if distance > info.radius * prop.scale + other_info.radius * other.scale:
            continue
        other_tags = context.tags_for(other.asset)
        if distance > radius + other_info.radius * other.scale * other_tags.footprint_scale:
            continue
        if tags.tags & other_tags.may_overlap or other_tags.tags & tags.may_overlap:
            continue
        if _quads_overlap(quad, footprint_corners(other, other_info, other_tags)[:, [0, 2]]):
            add("prop_overlap", "error", f"overlaps {other.asset.rsplit('/', 1)[-1]} ({other.key})")
            break
    quad_reach = max(math.hypot(float(px) - prop.x, float(pz) - prop.z) for px, pz in quad) + 1.0
    for record in context.spawns:
        if not record.active or math.hypot(record.x - prop.x, record.z - prop.z) > quad_reach:
            continue
        if _inside_quad((record.x, record.z), quad):
            add("prop_on_spawn", "warning", f"covers the spawn point of {record.name or record.key}")
            break
    if context.poi is not None and not contains(context.poi, prop.x, prop.z):
        add("prop_outside_poi", "warning", f"outside the place '{context.poi.get('id')}'")
    return found


def _grid_warnings(props: list[PlacedProp]) -> list[Violation]:
    by_asset: dict[str, list[PlacedProp]] = {}
    for prop in props:
        by_asset.setdefault(prop.asset, []).append(prop)
    out = []
    for asset, group in sorted(by_asset.items()):
        if len(group) >= GRID_MIN and is_grid_pack(group, min_size=GRID_MIN):
            first = min(group, key=lambda p: p.key)
            out.append(Violation("prop_grid_pattern", "warning", f"grid:{first.key}",
                                 f"{len(group)} x {asset.rsplit('/', 1)[-1]} are evenly spaced like a machine-made grid",
                                 first.x, first.z))
    return out


def _sorted(violations: list[Violation]) -> list[Violation]:
    return sorted(violations, key=lambda v: (v.severity != "error", v.rule, v.subject))


def lint_props(props: list[PlacedProp], context: PropContext) -> list[Violation]:
    """New props checked against the world and against each other."""
    others = [*context.existing, *props]
    out = [v for prop in props for v in lint_prop(prop, context, others)]
    return _sorted(out + _grid_warnings(props))


def lint_world_props(context: PropContext) -> list[Violation]:
    """Every existing prop and tree, for the content audit's props domain."""
    out = [v for prop in context.existing for v in lint_prop(prop, context, context.existing)]
    return _sorted(out + _grid_warnings(context.existing))
