# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Placement lint for unit and object spawns.

Errors are placements a player would notice as broken (buried, floating, in a lake, on a cliff, at
the world edge). Warnings are design smells (machine-made grids, spawns outside the place they are
named for, levels outside the band of the area).
"""

from __future__ import annotations

from dataclasses import asdict, dataclass

from .atlas import contains
from .query import EDGE_MARGIN, SLOPE_LIMIT, WATER_DEPTH_LIMIT, WorldQuery
from .spawns import SpawnRecord, find_packs, is_grid_pack, object_spawn_records, unit_spawn_records

HEIGHT_WARNING = 0.5
HEIGHT_ERROR = 2.0
_SEVERITY_ORDER = {"error": 0, "warning": 1}


@dataclass(frozen=True)
class Violation:
    rule: str
    severity: str        # "error" | "warning"
    subject: str         # spawn key, or "pack:<first spawn key>"
    message: str
    x: float | None = None
    z: float | None = None

    @property
    def key(self) -> str:
        return f"{self.rule}|{self.subject}"

    def to_dict(self) -> dict:
        return {"key": self.key, **asdict(self)}


Names = dict[tuple[str, int], str]


def _label(record: SpawnRecord, names: Names | None = None) -> str:
    if record.name:
        return record.name
    name = (names or {}).get((record.kind, record.entry))
    return name or f"{record.kind} {record.entry}"


def entry_names(game_data) -> Names:
    """(kind, entry id) -> display name, so unnamed spawns read as 'Forest Boar' instead of 'unit 19'."""
    names: Names = {("unit", uid): unit.name for uid, unit in game_data.units.items()}
    names.update({("object", oid): obj.name for oid, obj in game_data.objects.items()})
    return names


def lint_placement(record: SpawnRecord, query: WorldQuery, names: Names | None = None) -> list[Violation]:
    found: list[Violation] = []
    x, z = record.x, record.z
    label = _label(record, names)

    def add(rule: str, severity: str, text: str) -> None:
        found.append(Violation(rule, severity, record.key, f"{label} at ({x:.0f}, {z:.0f}): {text}", x, z))

    if not query.has_terrain(x, z):
        add("no_terrain", "error", "no terrain under the spawn")
        return found
    depth = query.water_depth_at(x, z)
    if depth > WATER_DEPTH_LIMIT:
        add("in_water", "error", f"stands in {depth:.1f} m deep water")
    if not query.edge_clear(x, z):
        add("terrain_edge", "error", f"within {EDGE_MARGIN:.0f} m of the terrain edge")
    if query.hole_at(x, z):
        # Terrain is not rendered in a hole: the ground there is whatever mesh fills it (a cellar
        # floor, a cave entrance), so the terrain's slope and height say nothing about the spawn.
        add("over_hole", "warning", "stands over a terrain hole; make sure a floor mesh fills it")
    else:
        slope = query.slope_at(x, z)
        if slope is not None and slope > SLOPE_LIMIT:
            add("steep_slope", "error", f"slope {slope:.0f}° exceeds {SLOPE_LIMIT:.0f}°")
    if not query.hole_at(x, z) and query.structure_at(x, z) is None:
        delta = record.y - query.height_at(x, z)
        if abs(delta) > HEIGHT_WARNING:
            severity = "error" if abs(delta) > HEIGHT_ERROR else "warning"
            add("height_delta", severity, f"{abs(delta):.1f} m {'above' if delta > 0 else 'below'} the ground")
    blocker = query.footprint_at(x, z)
    if blocker is not None:
        add("inside_footprint", "warning", f"inside the footprint of {blocker.asset}")
    return found


def lint_packs(records: list[SpawnRecord], names: Names | None = None) -> list[Violation]:
    found = []
    for pack in find_packs(records):
        if is_grid_pack(pack):
            first = pack[0]
            cx = sum(r.x for r in pack) / len(pack)
            cz = sum(r.z for r in pack) / len(pack)
            found.append(Violation("grid_pattern", "warning", f"pack:{first.key}",
                                   f"{len(pack)} x {_label(first, names)} around ({cx:.0f}, {cz:.0f}) sit on a regular grid; scatter them irregularly",
                                   cx, cz))
    return found


def lint_levels(records: list[SpawnRecord], query: WorldQuery, unit_levels: dict[int, tuple[int, int]],
                names: Names | None = None, service_units: set[int] | frozenset[int] = frozenset()) -> list[Violation]:
    """Place/level-band checks. Level bands describe what players fight, so service NPCs (quest
    givers, trainers, vendors, gossip) are exempt from the level check."""
    atlas = query.atlas
    if atlas is None:
        return []
    found = []
    for record in records:
        if record.kind != "unit":
            continue
        x, z = record.x, record.z
        prefix = record.poi_prefix
        if prefix:
            poi = atlas.poi_by_name(prefix)
            if poi is not None and not contains(poi, x, z):
                found.append(Violation("outside_poi", "warning", record.key,
                                       f"{_label(record, names)} at ({x:.0f}, {z:.0f}) is named for '{poi['name']}' but stands outside it", x, z))
        levels = None if record.entry in service_units else unit_levels.get(record.entry)
        band = atlas.band_for(x, z, query.area_at(x, z)) if levels else None
        if band and (levels[1] < band[0] or levels[0] > band[1]):
            found.append(Violation("level_band", "warning", record.key,
                                   f"{_label(record, names)} at ({x:.0f}, {z:.0f}) is level {levels[0]}-{levels[1]}, "
                                   f"outside the {band[2]} band {band[0]}-{band[1]}", x, z))
    return found


def naming_violations(records: list[SpawnRecord]) -> list[Violation]:
    """Spawn naming convention '<Place> - <Unit name>[ NN]' (applied to drafts, not to legacy data)."""
    found = []
    for record in records:
        if record.name and (" - " not in record.name or record.name.isupper()):
            found.append(Violation("spawn_name", "warning", record.key,
                                   f"spawn name '{record.name}' should follow '<Place> - <Unit name>' (see docs/world/bible.md naming guide)",
                                   record.x, record.z))
    return found


def _sorted(violations: list[Violation]) -> list[Violation]:
    return sorted(violations, key=lambda v: (_SEVERITY_ORDER[v.severity], v.rule, v.subject))


def lint_records(records: list[SpawnRecord], query: WorldQuery, unit_levels: dict[int, tuple[int, int]],
                 names: Names | None = None, service_units: set[int] | frozenset[int] = frozenset()) -> list[Violation]:
    found: list[Violation] = []
    for record in records:
        found.extend(lint_placement(record, query, names))
    found.extend(lint_packs(records, names))
    found.extend(lint_levels(records, query, unit_levels, names, service_units))
    return _sorted(found)


def lint_map(game_data, map_entry, query: WorldQuery) -> tuple[list[SpawnRecord], list[Violation]]:
    records = unit_spawn_records(map_entry) + object_spawn_records(map_entry)
    return records, lint_records(records, query, game_data.unit_levels(), entry_names(game_data), game_data.service_units())
