# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Point queries against a WorldSnapshot: exact height/slope, terrain facts, nearby props, POIs."""

from __future__ import annotations

import math

from .atlas import Atlas
from .entities import footprint_radius, structure_radius
from .snapshot import WorldSnapshot
from .surface import sample
from .terrain_kinds import TerrainKinds

EDGE_MARGIN = 5.0
WATER_DEPTH_LIMIT = 1.0
SLOPE_LIMIT = 35.0
_EDGE_DIRECTIONS = [(math.cos(a), math.sin(a)) for a in (i * math.pi / 4.0 for i in range(8))]


class WorldQuery:
    def __init__(self, snapshot: WorldSnapshot, atlas: Atlas | None = None, kinds: TerrainKinds | None = None,
                 zone_names: dict[int, str] | None = None):
        self.snapshot = snapshot
        self.atlas = atlas
        self.kinds = kinds
        self.zone_names = zone_names or {}

    def has_terrain(self, x: float, z: float) -> bool:
        return self.snapshot.page_local(x, z) is not None

    def _surface(self, x: float, z: float) -> tuple[float, float] | None:
        located = self.snapshot.page_local(x, z)
        if located is None:
            return None
        slot, lx, lz = located
        return sample(self.snapshot.outer[slot], self.snapshot.inner[slot], lx, lz)

    def height_at(self, x: float, z: float) -> float | None:
        surface = self._surface(x, z)
        return surface[0] if surface else None

    def slope_at(self, x: float, z: float) -> float | None:
        surface = self._surface(x, z)
        return surface[1] if surface else None

    def _cell(self, x: float, z: float):
        return self.snapshot.cell_index(x, z) if self.has_terrain(x, z) else None

    def area_at(self, x: float, z: float) -> int | None:
        cell = self._cell(x, z)
        return int(self.snapshot.area[cell]) if cell else None

    def water_depth_at(self, x: float, z: float) -> float:
        cell = self._cell(x, z)
        return float(self.snapshot.water_depth[cell]) if cell else 0.0

    def hole_at(self, x: float, z: float) -> bool:
        cell = self._cell(x, z)
        return bool(self.snapshot.hole[cell]) if cell else False

    def terrain_kind_at(self, x: float, z: float) -> str | None:
        cell = self._cell(x, z)
        if not cell or not self.kinds:
            return None
        material = self.snapshot.material_name(int(self.snapshot.material[cell]))
        return self.kinds.kind(material, int(self.snapshot.layer[cell]))

    def edge_clear(self, x: float, z: float, margin: float = EDGE_MARGIN) -> bool:
        return all(self.has_terrain(x + dx * margin, z + dz * margin) for dx, dz in _EDGE_DIRECTIONS)

    def entities_near(self, x: float, z: float, radius: float):
        found = []
        for entity in self.snapshot.entities:
            distance = math.hypot(entity.position[0] - x, entity.position[2] - z)
            if distance <= radius:
                found.append((distance, entity))
        return sorted(found, key=lambda item: item[0])

    def structure_at(self, x: float, z: float):
        for distance, entity in self.entities_near(x, z, 100.0):
            if distance <= structure_radius(entity):
                return entity
        return None

    def footprint_at(self, x: float, z: float):
        for distance, entity in self.entities_near(x, z, 50.0):
            if distance <= footprint_radius(entity):
                return entity
        return None

    def placeable(self, x: float, z: float) -> tuple[bool, list[str]]:
        if not self.has_terrain(x, z):
            return False, ["no terrain"]
        reasons = []
        if self.hole_at(x, z):
            reasons.append("terrain hole")
        depth = self.water_depth_at(x, z)
        if depth > WATER_DEPTH_LIMIT:
            reasons.append(f"water {depth:.1f} m deep")
        slope = self.slope_at(x, z)
        if slope is not None and slope > SLOPE_LIMIT:
            reasons.append(f"slope {slope:.0f}° (> {SLOPE_LIMIT:.0f}°)")
        if not self.edge_clear(x, z):
            reasons.append(f"within {EDGE_MARGIN:.0f} m of the terrain edge")
        blocker = self.footprint_at(x, z)
        if blocker is not None:
            reasons.append(f"inside footprint of {blocker.asset}")
        return not reasons, reasons

    def describe(self, x: float, z: float) -> dict:
        area = self.area_at(x, z)
        ok, reasons = self.placeable(x, z)
        pois = self.atlas.pois_at(x, z) if self.atlas else []
        return {
            "x": x,
            "z": z,
            "height": self.height_at(x, z),
            "slope_deg": self.slope_at(x, z),
            "area_id": area,
            "area_name": self.zone_names.get(area) if area else None,
            "terrain_kind": self.terrain_kind_at(x, z),
            "water_depth": self.water_depth_at(x, z),
            "hole": self.hole_at(x, z),
            "placeable": {"ok": ok, "reasons": reasons},
            "pois": [{"id": p["id"], "name": p["name"], "kind": p["kind"], "status": p["status"]} for p in pois],
            "entities": [{"asset": e.asset, "distance": round(d, 1)} for d, e in self.entities_near(x, z, 30.0)[:5]],
        }
