# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""World checks used by the content skills' validators (validate_npc_json.py, validate_quest_json.py).

Placement errors that are already in the lint baseline are downgraded to warnings, so exporting and
re-validating existing content (the weekly content audit does this for every NPC) never fails on
known legacy problems, while a new draft cannot introduce new ones.
"""

from __future__ import annotations

from .baseline import load_baseline
from .cli_query import open_map
from .lint import entry_names, lint_records, naming_violations
from .snapshot import NoTerrainError
from .spawns import records_from_npc_draft, unit_spawn_records

MAP_GLOBAL = 0            # proto MapEntry.MapInstanceType.GLOBAL
_MAPS: dict[int, tuple] = {}


def _map(map_id: int):
    if map_id not in _MAPS:
        _MAPS[map_id] = open_map(map_id)
    return _MAPS[map_id]


def npc_draft_findings(doc: dict) -> tuple[list[str], list[str]]:
    """(errors, warnings) for the spawns of an mmo-npc-designer draft."""
    records = records_from_npc_draft(doc)
    errors: list[str] = []
    warnings: list[str] = []
    if not records:
        return errors, warnings
    baseline = load_baseline("placement")
    unit = doc.get("unit") or {}
    for map_id in sorted({r.map_id for r in records}):
        try:
            data, map_entry, query = _map(map_id)
        except (NoTerrainError, SystemExit) as exc:
            warnings.append(f"world placement checks skipped for map {map_id}: {exc}")
            continue
        if map_entry.instancetype != MAP_GLOBAL:
            # Instances (dungeons, raids, arenas) are world-model interiors: their floors are not the
            # terrain, so terrain heights say nothing about a spawn there (same scope as the audit).
            warnings.append(f"world placement checks skipped for map {map_id}: instance map (spawns stand on world-model floors)")
            continue
        levels = data.unit_levels()
        names = entry_names(data)
        if "id" in unit and "minlevel" in unit:
            lo = int(unit["minlevel"])
            levels[int(unit["id"])] = (lo, max(lo, int(unit.get("maxlevel", lo))))
        if "id" in unit and unit.get("name"):
            names[("unit", int(unit["id"]))] = unit["name"]
        map_records = [r for r in records if r.map_id == map_id]
        for violation in lint_records(map_records, query, levels, names, data.service_units()) + naming_violations(map_records):
            if violation.key in baseline:
                warnings.append(f"{violation.message} (known issue, baselined)")
            elif violation.severity == "error":
                errors.append(f"placement: {violation.message}")
            else:
                warnings.append(f"placement: {violation.message}")
        if query.atlas is None:
            warnings.append(f"map {map_id} has no atlas (data/world/atlas/map_{map_id}.json); place and level-band checks skipped")
    return errors, warnings


def quest_draft_warnings(doc: dict) -> list[str]:
    """Warnings when a quest's level falls outside the band of the place its provider stands in."""
    quest = doc.get("quest") or {}
    level = quest.get("questlevel") or quest.get("minlevel")
    provider_ids = set((doc.get("providers") or {}).get("unit_ids") or [])
    if not level or not provider_ids:
        return []
    warnings: list[str] = []
    data, _, _ = _map(0)
    for map_id, map_entry in sorted(data.maps.items()):
        spawns = [r for r in unit_spawn_records(map_entry) if r.entry in provider_ids]
        if not spawns:
            continue
        try:
            _, _, query = _map(map_id)
        except (NoTerrainError, SystemExit):
            continue
        if query.atlas is None:
            continue
        for record in spawns:
            band = query.atlas.band_for(record.x, record.z, query.area_at(record.x, record.z))
            if band and not band[0] <= int(level) <= band[1]:
                warnings.append(f"quest level {level} is outside the {band[2]} band {band[0]}-{band[1]} where provider "
                                f"{data.units[record.entry].name} stands (map {map_id}, {record.x:.0f}, {record.z:.0f})")
    return sorted(set(warnings))
