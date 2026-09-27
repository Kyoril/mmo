# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Placed world entities and their placement footprints.

Footprints are path heuristics (mesh bounds are not stored cheaply in .hmsh). World models (.hwmo)
have walkable interiors, so they never block a spawn. They do count as a *structure*: spawns
standing on a floor or inside a building legitimately sit above or below the terrain, so the
height rule is skipped there.
"""

from __future__ import annotations

from pathlib import Path

from .formats.wobj import WorldEntity, parse_wobj
from .paths import REPO, entities_dir

# (substring of the lower-cased asset path, footprint radius in metres at scale 1). First match wins.
_FOOTPRINTS = (
    ("/buildings/", 6.0),
    ("fortress_", 6.0),
    ("tent", 3.0),
    ("wagon", 2.5),
    ("haystack", 1.5),
    ("/trees/", 1.0),
    ("fence", 0.5),
)
_DEFAULT_FOOTPRINT = 1.0
_WMO_STRUCTURE = 25.0
_BUILDING_STRUCTURE = 8.0


def load_entities(directory: str, repo: Path = REPO) -> list[WorldEntity]:
    root = entities_dir(directory, repo)
    if not root.is_dir():
        return []
    return [parse_wobj(path) for path in sorted(root.glob("*/*.wobj"))]


def _asset(entity: WorldEntity) -> str:
    return entity.asset.lower().replace("\\", "/")


def _planar_scale(entity: WorldEntity) -> float:
    return max(abs(entity.scale[0]), abs(entity.scale[2]))


def footprint_radius(entity: WorldEntity) -> float:
    """Radius (m) around the entity origin in which a spawn would stand inside the mesh."""
    if entity.kind == "wmo":
        return 0.0
    asset = _asset(entity)
    for needle, radius in _FOOTPRINTS:
        if needle in asset:
            return radius * _planar_scale(entity)
    return _DEFAULT_FOOTPRINT * _planar_scale(entity)


def structure_radius(entity: WorldEntity) -> float:
    """Radius (m) in which a spawn may legitimately stand on the structure instead of the terrain."""
    if entity.kind == "wmo":
        return _WMO_STRUCTURE * _planar_scale(entity)
    if "/buildings/" in _asset(entity):
        return _BUILDING_STRUCTURE * _planar_scale(entity)
    return 0.0
