# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""The world atlas: named places, zone bands and roads for one map (data/world/atlas/map_<id>.json).

Tool-side only; never exported to the game. Agents edit it as text and mmo_edit edits it in the
Atlas edit mode, so the file format is part of the contract:
  - UTF-8, 2-space indent, non-ASCII kept as-is, trailing newline;
  - every float rounded to 0.1;
  - existing key order preserved (unknown keys kept), new objects written in schema order.
Status: placeholder (an agent's guess, may carry an `ask`), canon (confirmed by the user),
note (the user's design intent). Only the user promotes to canon.
"""

from __future__ import annotations

import json
import math
import re
from dataclasses import dataclass
from pathlib import Path

TOP_KEYS = ("map", "version", "zones", "pois", "roads")
ZONE_KEYS = ("areaId", "name", "levelMin", "levelMax", "theme", "description", "status", "ask", "source", "notes")
POI_KEYS = ("id", "name", "kind", "center", "radius", "polygon", "areaId", "levelMin", "levelMax", "faction",
            "description", "status", "ask", "source", "notes")
ROAD_KEYS = ("id", "name", "points", "status", "ask", "source", "notes")
STATUSES = ("placeholder", "canon", "note")
POI_KINDS = ("hub", "camp", "ruin", "lair", "landmark", "resource", "dungeon_entrance", "note")
SOURCES = ("agent-bootstrap", "agent", "user")
_ID = re.compile(r"^[a-z0-9_]+$")


class AtlasError(ValueError):
    pass


def _is_number(value) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _is_point(value) -> bool:
    return isinstance(value, list) and len(value) == 2 and all(_is_number(v) for v in value)


def _round_floats(value):
    if isinstance(value, float):
        return round(value, 1)
    if isinstance(value, list):
        return [_round_floats(v) for v in value]
    if isinstance(value, dict):
        return {k: _round_floats(v) for k, v in value.items()}
    return value


def dumps(doc: dict) -> str:
    return json.dumps(_round_floats(doc), indent=2, ensure_ascii=False) + "\n"


def slugify(text: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")
    return slug or "place"


def _ordered(fields: dict, keys: tuple[str, ...]) -> dict:
    out = {k: fields[k] for k in keys if k in fields and fields[k] is not None}
    out.update({k: v for k, v in fields.items() if k not in keys and v is not None})
    return out


def _check_levels(entry: dict, where: str, errors: list[str]) -> None:
    lo, hi = entry.get("levelMin"), entry.get("levelMax")
    for key, value in (("levelMin", lo), ("levelMax", hi)):
        if value is not None and (not isinstance(value, int) or isinstance(value, bool) or value < 1):
            errors.append(f"{where}.{key}: must be a positive integer")
    if isinstance(lo, int) and isinstance(hi, int) and lo > hi:
        errors.append(f"{where}.levelMin: {lo} is greater than levelMax {hi}")


def _check_common(entry: dict, where: str, allowed_status: tuple[str, ...], errors: list[str]) -> None:
    if not isinstance(entry.get("name"), str) or not entry.get("name"):
        errors.append(f"{where}.name: required non-empty string")
    status = entry.get("status")
    if status not in allowed_status:
        errors.append(f"{where}.status: {status!r} is not one of {allowed_status}")
    if "ask" in entry and not isinstance(entry["ask"], str):
        errors.append(f"{where}.ask: must be a string")
    if status == "canon" and entry.get("ask"):
        errors.append(f"{where}.ask: a canon entry must not keep an open ask (Confirm clears it)")
    if "source" in entry and entry["source"] not in SOURCES:
        errors.append(f"{where}.source: {entry['source']!r} is not one of {SOURCES}")


def validate(doc: dict) -> list[str]:
    errors: list[str] = []
    if not isinstance(doc, dict):
        return ["atlas: top level must be an object"]
    if not isinstance(doc.get("map"), int) or isinstance(doc.get("map"), bool):
        errors.append("map: required integer map id")
    if doc.get("version") != 1:
        errors.append(f"version: {doc.get('version')!r} is not the supported version 1")
    for key in ("zones", "pois", "roads"):
        if not isinstance(doc.get(key), list):
            errors.append(f"{key}: required array")
    if errors:
        return errors

    seen_areas = set()
    for i, zone in enumerate(doc["zones"]):
        where = f"zones[{i}]"
        if not isinstance(zone, dict):
            errors.append(f"{where}: must be an object")
            continue
        area = zone.get("areaId")
        if not isinstance(area, int) or isinstance(area, bool) or area <= 0:
            errors.append(f"{where}.areaId: required positive integer")
        elif area in seen_areas:
            errors.append(f"{where}.areaId: duplicate id {area}")
        seen_areas.add(area)
        _check_common(zone, where, ("placeholder", "canon"), errors)
        _check_levels(zone, where, errors)

    seen_ids = set()
    for i, poi in enumerate(doc["pois"]):
        where = f"pois[{i}]"
        if not isinstance(poi, dict):
            errors.append(f"{where}: must be an object")
            continue
        poi_id = poi.get("id")
        if not isinstance(poi_id, str) or not _ID.match(poi_id):
            errors.append(f"{where}.id: required lower_snake_case string")
        elif poi_id in seen_ids:
            errors.append(f"{where}.id: duplicate id {poi_id!r}")
        seen_ids.add(poi_id)
        if poi.get("kind") not in POI_KINDS:
            errors.append(f"{where}.kind: {poi.get('kind')!r} is not one of {POI_KINDS}")
        if not _is_point(poi.get("center")):
            errors.append(f"{where}.center: required [x, z]")
        has_radius = "radius" in poi
        has_polygon = "polygon" in poi
        if has_radius == has_polygon:
            errors.append(f"{where}: needs exactly one of radius or polygon")
        elif has_radius and (not _is_number(poi["radius"]) or poi["radius"] <= 0):
            errors.append(f"{where}.radius: must be a positive number")
        elif has_polygon and (not isinstance(poi["polygon"], list) or len(poi["polygon"]) < 3
                              or not all(_is_point(p) for p in poi["polygon"])):
            errors.append(f"{where}.polygon: needs at least 3 [x, z] points")
        _check_common(poi, where, STATUSES, errors)
        _check_levels(poi, where, errors)
        if (poi.get("kind") == "note") != (poi.get("status") == "note"):
            errors.append(f"{where}: kind 'note' and status 'note' must go together")

    seen_roads = set()
    for i, road in enumerate(doc["roads"]):
        where = f"roads[{i}]"
        if not isinstance(road, dict):
            errors.append(f"{where}: must be an object")
            continue
        road_id = road.get("id")
        if not isinstance(road_id, str) or not _ID.match(road_id):
            errors.append(f"{where}.id: required lower_snake_case string")
        elif road_id in seen_roads:
            errors.append(f"{where}.id: duplicate id {road_id!r}")
        seen_roads.add(road_id)
        points = road.get("points")
        if not isinstance(points, list) or len(points) < 2 or not all(_is_point(p) for p in points):
            errors.append(f"{where}.points: needs at least 2 [x, z] points")
        _check_common(road, where, ("placeholder", "canon"), errors)
    return errors


def contains(poi: dict, x: float, z: float) -> bool:
    if "radius" in poi:
        cx, cz = poi["center"]
        return math.hypot(x - cx, z - cz) <= poi["radius"]
    inside = False
    points = poi["polygon"]
    for i in range(len(points)):
        x1, z1 = points[i]
        x2, z2 = points[(i + 1) % len(points)]
        if (z1 > z) != (z2 > z) and x < (x2 - x1) * (z - z1) / (z2 - z1) + x1:
            inside = not inside
    return inside


def _poi_area(poi: dict) -> float:
    if "radius" in poi:
        return math.pi * poi["radius"] ** 2
    pts = poi["polygon"]
    n = len(pts)
    return abs(sum(pts[i][0] * pts[(i + 1) % n][1] - pts[(i + 1) % n][0] * pts[i][1] for i in range(n))) / 2.0


@dataclass
class Atlas:
    doc: dict
    path: Path | None = None

    @property
    def zones(self) -> list[dict]:
        return self.doc["zones"]

    @property
    def pois(self) -> list[dict]:
        return self.doc["pois"]

    @property
    def roads(self) -> list[dict]:
        return self.doc["roads"]

    def zone(self, area_id: int) -> dict | None:
        return next((z for z in self.zones if z.get("areaId") == area_id), None)

    def poi(self, poi_id: str) -> dict | None:
        return next((p for p in self.pois if p.get("id") == poi_id), None)

    def poi_by_name(self, name: str) -> dict | None:
        folded = name.casefold()
        return next((p for p in self.pois if str(p.get("name", "")).casefold() == folded), None)

    def pois_at(self, x: float, z: float) -> list[dict]:
        """Places containing (x, z), smallest first."""
        return sorted((p for p in self.pois if contains(p, x, z)), key=_poi_area)

    def band_for(self, x: float, z: float, area_id: int | None) -> tuple[int, int, str] | None:
        """Level band (lo, hi, label) at a point: the smallest place with a band, else the zone's band."""
        for poi in self.pois_at(x, z):
            if poi.get("levelMin") is not None and poi.get("levelMax") is not None:
                return poi["levelMin"], poi["levelMax"], poi["name"]
        zone = self.zone(area_id) if area_id else None
        if zone and zone.get("levelMin") is not None and zone.get("levelMax") is not None:
            return zone["levelMin"], zone["levelMax"], zone["name"]
        return None

    def unique_poi_id(self, name: str) -> str:
        base = slugify(name)
        taken = {p.get("id") for p in self.pois}
        if base not in taken:
            return base
        suffix = 2
        while f"{base}_{suffix}" in taken:
            suffix += 1
        return f"{base}_{suffix}"

    def _append(self, key: str, entry: dict) -> dict:
        self.doc[key].append(entry)
        errors = validate(self.doc)
        if errors:
            self.doc[key].pop()
            raise AtlasError("; ".join(errors))
        return entry

    def add_zone(self, **fields) -> dict:
        return self._append("zones", _ordered(fields, ZONE_KEYS))

    def add_poi(self, **fields) -> dict:
        fields.setdefault("id", self.unique_poi_id(fields.get("name", "place")))
        return self._append("pois", _ordered(fields, POI_KEYS))

    def add_road(self, **fields) -> dict:
        fields.setdefault("id", slugify(fields.get("name", "road")))
        return self._append("roads", _ordered(fields, ROAD_KEYS))


def empty_atlas(map_id: int) -> Atlas:
    return Atlas({"map": map_id, "version": 1, "zones": [], "pois": [], "roads": []})


def load_atlas(path: Path) -> Atlas:
    doc = json.loads(Path(path).read_text(encoding="utf-8"))
    errors = validate(doc)
    if errors:
        raise AtlasError(f"{path}: " + "; ".join(errors))
    return Atlas(doc, Path(path))


def load_atlas_or_none(path: Path) -> Atlas | None:
    return load_atlas(path) if Path(path).is_file() else None


def save_atlas(atlas: Atlas, path: Path | None = None) -> Path:
    target = Path(path or atlas.path)
    errors = validate(atlas.doc)
    if errors:
        raise AtlasError("; ".join(errors))
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(dumps(atlas.doc), encoding="utf-8", newline="\n")
    atlas.path = target
    return target
