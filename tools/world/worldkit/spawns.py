# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Flat, position-resolved spawn records with stable identity keys.

A record is one placement: a unit spawn with several `locations` yields one record per location.
The key survives re-ordering of the spawn list (it does not use the list index) and changes when
the spawn moves, which is what the lint baseline needs: moving a buried spawn resolves it.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

_MOVEMENT_NAMES = {"STATIONARY": 0, "RANDOM": 1, "PATROL": 2}


@dataclass(frozen=True)
class SpawnRecord:
    key: str
    map_id: int
    kind: str                 # "unit" | "object"
    entry: int                # unit or object entry id
    index: int                # index in the map's spawn list
    location: int             # index in spawn.locations (0 for legacy single-position spawns)
    name: str
    x: float
    y: float
    z: float
    active: bool
    movement: int             # 0 stationary, 1 random, 2 patrol
    respawn_delay_ms: int
    waypoints: tuple

    @property
    def poi_prefix(self) -> str | None:
        """'Barrowfield' for 'Barrowfield - Barrow Skeleton 07' (the spawn naming convention)."""
        if " - " not in self.name:
            return None
        return self.name.split(" - ", 1)[0].strip() or None


def spawn_key(kind: str, map_id: int, entry: int, name: str, x: float, z: float) -> str:
    return f"{kind}:{map_id}:{entry}:{name or '-'}:{int(round(x))}:{int(round(z))}"


def _positions(spawn) -> list[tuple[float, float, float]]:
    if len(spawn.locations):
        return [(loc.positionx, loc.positiony, loc.positionz) for loc in spawn.locations]
    return [(spawn.positionx, spawn.positiony, spawn.positionz)]


def unit_spawn_records(map_entry) -> list[SpawnRecord]:
    records = []
    for index, spawn in enumerate(map_entry.unitspawns):
        waypoints = tuple((w.positionx, w.positiony, w.positionz) for w in spawn.waypoints)
        for location, (x, y, z) in enumerate(_positions(spawn)):
            records.append(SpawnRecord(spawn_key("unit", map_entry.id, spawn.unitentry, spawn.name, x, z),
                                       map_entry.id, "unit", spawn.unitentry, index, location, spawn.name, x, y, z,
                                       spawn.isactive, int(spawn.movement), int(spawn.respawndelay), waypoints))
    return records


def object_spawn_records(map_entry) -> list[SpawnRecord]:
    records = []
    for index, spawn in enumerate(map_entry.objectspawns):
        loc = spawn.location
        x, y, z = loc.positionx, loc.positiony, loc.positionz
        records.append(SpawnRecord(spawn_key("object", map_entry.id, spawn.objectentry, spawn.name, x, z),
                                   map_entry.id, "object", spawn.objectentry, index, 0, spawn.name, x, y, z,
                                   spawn.isactive, 0, int(spawn.respawndelay), ()))
    return records


def records_from_npc_draft(doc: dict) -> list[SpawnRecord]:
    """Spawn records from an mmo-npc-designer draft ({"unit": {...}, "spawns": [{"map_id", "spawn"}]})."""
    unit_id = int((doc.get("unit") or {}).get("id", 0))
    records = []
    for index, wrapper in enumerate(doc.get("spawns") or []):
        spawn = wrapper.get("spawn") or {}
        map_id = int(wrapper.get("map_id", 0))
        entry = int(spawn.get("unitentry", unit_id))
        name = spawn.get("name", "")
        movement = spawn.get("movement", 0)
        movement = _MOVEMENT_NAMES.get(movement, 0) if isinstance(movement, str) else int(movement)
        locations = spawn.get("locations") or [spawn]
        waypoints = tuple((float(w.get("positionx", 0.0)), float(w.get("positiony", 0.0)), float(w.get("positionz", 0.0)))
                          for w in spawn.get("waypoints", []))
        for location, loc in enumerate(locations):
            x, y, z = float(loc.get("positionx", 0.0)), float(loc.get("positiony", 0.0)), float(loc.get("positionz", 0.0))
            records.append(SpawnRecord(spawn_key("unit", map_id, entry, name, x, z), map_id, "unit", entry, index,
                                       location, name, x, y, z, bool(spawn.get("isactive", True)), movement,
                                       int(spawn.get("respawndelay", 0) or 0), waypoints))
    return records


def find_packs(records: list[SpawnRecord], link_distance: float = 40.0) -> list[list[SpawnRecord]]:
    """Single-linkage clusters of same-kind, same-entry spawns (planar distance <= link_distance)."""
    by_entry: dict[tuple[str, int, int], list[SpawnRecord]] = {}
    for record in records:
        by_entry.setdefault((record.kind, record.map_id, record.entry), []).append(record)
    packs = []
    for group in by_entry.values():
        unvisited = list(group)
        while unvisited:
            frontier = [unvisited.pop()]
            pack = []
            while frontier:
                current = frontier.pop()
                pack.append(current)
                near = [r for r in unvisited if math.hypot(r.x - current.x, r.z - current.z) <= link_distance]
                for r in near:
                    unvisited.remove(r)
                frontier.extend(near)
            packs.append(sorted(pack, key=lambda r: r.key))
    return packs


def is_grid_pack(pack: list[SpawnRecord], min_size: int = 5, max_cv: float = 0.12) -> bool:
    """True when nearest-neighbour spacing is suspiciously uniform (a machine-placed grid)."""
    if len(pack) < min_size:
        return False
    nearest = []
    for record in pack:
        nearest.append(min(math.hypot(o.x - record.x, o.z - record.z) for o in pack if o is not record))
    mean = sum(nearest) / len(nearest)
    if mean <= 0.0:
        return True
    variance = sum((d - mean) ** 2 for d in nearest) / len(nearest)
    return math.sqrt(variance) / mean < max_cv
