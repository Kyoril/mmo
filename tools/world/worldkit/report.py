# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Content health of a map: can every quest objective actually be done near its quest giver, does
every quest end with a turn-in, and how are spawns and levels distributed over zones and places."""

from __future__ import annotations

import math
from dataclasses import asdict, dataclass

import numpy as np

from .spawns import object_spawn_records, unit_spawn_records

AUTO_REWARDED = 0x20
DEFAULT_MAX_DISTANCE = 250.0
# Trigger actions (src/shared/proto_data/trigger_helper.h) that bring creatures into the world.
ACTION_SET_SPAWN_STATE = 4     # targetname = spawn name, data[0] != 0 activates the spawner
ACTION_SUMMON_CREATURE = 25    # data[0] = creature entry
TARGET_NAMED_WORLD_OBJECT = 4  # trigger_action_target (trigger_helper.h)
TARGET_NAMED_CREATURE = 5
_SEVERITY_ORDER = {"error": 0, "warning": 1}


@dataclass(frozen=True)
class Finding:
    rule: str
    severity: str
    subject: str
    message: str

    @property
    def key(self) -> str:
        return f"{self.rule}|{self.subject}"

    def to_dict(self) -> dict:
        return {"key": self.key, **asdict(self)}


def _trigger_actions(data, action_type: int):
    for trigger in getattr(data, "triggers", {}).values():
        for action in trigger.actions:
            if action.action == action_type:
                yield action


def trigger_activated_spawn_names(data) -> set[tuple[str, str]]:
    """(kind, name) of spawners some trigger switches on (inactive in data, active during play).
    Mirrors TriggerHandler::HandleSetSpawnState: any non-zero data[0] activates."""
    kinds = {TARGET_NAMED_CREATURE: "unit", TARGET_NAMED_WORLD_OBJECT: "object"}
    return {(kinds[action.target], action.targetname) for action in _trigger_actions(data, ACTION_SET_SPAWN_STATE)
            if action.target in kinds and action.targetname and len(action.data) and action.data[0] != 0}


def trigger_summoned_units(data) -> set[int]:
    return {action.data[0] for action in _trigger_actions(data, ACTION_SUMMON_CREATURE) if len(action.data)}


def _is_live(record, activated: set[tuple[str, str]] | frozenset = frozenset()) -> bool:
    return record.active or (bool(record.name) and (record.kind, record.name) in activated)


def _active_positions(records, activated: set[tuple[str, str]] | frozenset = frozenset()) -> dict[int, list[tuple[float, float]]]:
    positions: dict[int, list[tuple[float, float]]] = {}
    for record in records:
        if _is_live(record, activated):
            positions.setdefault(record.entry, []).append((record.x, record.z))
    return positions


def _loot_ids_with_item(loot: dict, item_id: int) -> set[int]:
    ids = set()
    for loot_id, entry in loot.items():
        for group in entry.groups:
            if any(d.item == item_id and d.isactive for d in group.definitions):
                ids.add(loot_id)
    return ids


def _unit_loot_ids(unit) -> set[int]:
    """Loot tables of a creature, as creature_ai_death_state.cpp picks them: the repeated list when
    it is non-empty, otherwise the legacy single field."""
    if len(unit.unitlootentries):
        return set(unit.unitlootentries)
    return {unit.unitlootentry} if unit.unitlootentry else set()


def _object_loot_ids(obj) -> set[int]:
    """Loot tables of a world object (game_world_object_s.cpp): the repeated list when non-empty,
    otherwise the legacy single field. Like creature loot, they index unit_loot."""
    if len(obj.objectlootentries):
        return set(obj.objectlootentries)
    return {obj.objectlootentry} if obj.objectlootentry else set()


def quest_givers(data, map_entry) -> dict[int, list[tuple[float, float]]]:
    """quest id -> positions of active unit/object spawns on this map that offer it."""
    givers: dict[int, list[tuple[float, float]]] = {}
    activated = trigger_activated_spawn_names(data)
    for entry, points in _active_positions(unit_spawn_records(map_entry), activated).items():
        unit = data.units.get(entry)
        for quest_id in (unit.quests if unit is not None else []):
            givers.setdefault(quest_id, []).extend(points)
    for entry, points in _active_positions(object_spawn_records(map_entry), activated).items():
        obj = data.objects.get(entry)
        for quest_id in (obj.quests if obj is not None else []):
            givers.setdefault(quest_id, []).extend(points)
    return givers


def _quest_enders(data) -> set[int]:
    enders = set()
    for unit in data.units.values():
        enders.update(unit.end_quests)
    for obj in data.objects.values():
        enders.update(obj.end_quests)
    return enders


def objective_sources(data, map_entry, quest, requirement) -> list[tuple[float, float]]:
    """Positions on this map where a requirement can be progressed (kill, use, loot)."""
    activated = trigger_activated_spawn_names(data)
    units = _active_positions(unit_spawn_records(map_entry), activated)
    objects = _active_positions(object_spawn_records(map_entry), activated)
    if requirement.creatureid:
        # Kill credit goes to the unit's killcredit entry when set (game_player_s.cpp).
        credited = {uid for uid, unit in data.units.items()
                    if (unit.killcredit or uid) == requirement.creatureid}
        return [point for uid in credited for point in units.get(uid, [])]
    if requirement.objectid:
        return list(objects.get(requirement.objectid, []))
    if requirement.itemid:
        sources: list[tuple[float, float]] = []
        # Creatures and world objects both draw from unit_loot (object_loot.data is unused by the server).
        loot_ids = _loot_ids_with_item(data.unit_loot, requirement.itemid)
        for unit_id, points in units.items():
            unit = data.units.get(unit_id)
            if unit is not None and _unit_loot_ids(unit) & loot_ids:
                sources.extend(points)
        for spawn in map_entry.objectspawns:
            if not (spawn.isactive or ("object", spawn.name) in activated):
                continue
            obj = data.objects.get(spawn.objectentry)
            spawn_loot = {spawn.loot_entry} if spawn.loot_entry else (_object_loot_ids(obj) if obj is not None else set())
            if spawn_loot & loot_ids:
                sources.append((spawn.location.positionx, spawn.location.positionz))
        return sources
    return []


def _requirement_label(data, requirement) -> str:
    if requirement.creatureid:
        unit = data.units.get(requirement.creatureid)
        return f"kill {unit.name if unit is not None else requirement.creatureid}"
    if requirement.objectid:
        obj = data.objects.get(requirement.objectid)
        return f"use {obj.name if obj is not None else requirement.objectid}"
    item = data.items.get(requirement.itemid)
    return f"collect {item.name if item is not None else requirement.itemid}"


def _nearest(givers, sources) -> tuple[float, tuple[float, float], tuple[float, float]]:
    return min(((math.hypot(g[0] - s[0], g[1] - s[1]), g, s) for g in givers for s in sources), key=lambda t: t[0])


def _counted(requirement) -> bool:
    return bool(requirement.creatureid or requirement.objectid or requirement.itemid)


def check_reachability(data, map_entry, max_distance: float = DEFAULT_MAX_DISTANCE) -> list[Finding]:
    findings: list[Finding] = []
    givers = quest_givers(data, map_entry)
    enders = _quest_enders(data)
    summoned = trigger_summoned_units(data)
    for quest_id in sorted(givers):
        quest = data.quests.get(quest_id)
        if quest is None:
            continue
        title = f"Quest {quest_id} '{quest.name}'"
        if quest.flags & AUTO_REWARDED:
            findings.append(Finding("auto_rewarded", "error", f"quest:{quest_id}",
                                    f"{title} is AutoRewarded; every quest must end with a turn-in at an NPC or object"))
        if quest_id not in enders:
            findings.append(Finding("no_turn_in", "error", f"quest:{quest_id}", f"{title} has no NPC or object that accepts the turn-in"))
        for index, requirement in enumerate(quest.requirements):
            if not _counted(requirement) or (requirement.itemid and requirement.itemid == quest.srcitemid):
                continue
            sources = objective_sources(data, map_entry, quest, requirement)
            subject = f"quest:{quest_id}:req{index}"
            label = _requirement_label(data, requirement)
            if not sources and any(objective_sources(data, other, quest, requirement)
                                   for other in data.maps.values() if other.id != map_entry.id):
                continue  # done on another map (e.g. a dungeon); distance is meaningless there
            if not sources and requirement.creatureid and (requirement.creatureid in summoned or any(
                    (unit.killcredit or uid) == requirement.creatureid for uid, unit in data.units.items() if uid in summoned)):
                continue  # summoned by a trigger (boss adds, events); no fixed position to measure
            if not sources:
                findings.append(Finding("no_active_source", "error", subject, f"{title}: '{label}' has no active source on map {map_entry.id}"))
                continue
            distance, _, _ = _nearest(givers[quest_id], sources)
            if distance > max_distance:
                findings.append(Finding("source_far", "warning", subject,
                                        f"{title}: nearest '{label}' source is {distance:.0f} m from the quest giver (> {max_distance:.0f} m)"))
    return sorted(findings, key=lambda f: (_SEVERITY_ORDER[f.severity], f.rule, f.subject))


def quest_links(data, map_entry) -> list[tuple[int, tuple[float, float], tuple[float, float]]]:
    """(quest id, giver position, nearest objective source) per quest objective, for map arrows."""
    links = []
    givers = quest_givers(data, map_entry)
    for quest_id in sorted(givers):
        quest = data.quests.get(quest_id)
        if quest is None:
            continue
        for requirement in quest.requirements:
            if not _counted(requirement):
                continue
            sources = objective_sources(data, map_entry, quest, requirement)
            if sources:
                _, giver, source = _nearest(givers[quest_id], sources)
                link = (quest_id, (float(giver[0]), float(giver[1])), (float(source[0]), float(source[1])))
                if link not in links:
                    links.append(link)
    return links


def _band(levels: list[tuple[int, int]]) -> list[int] | None:
    return [min(l[0] for l in levels), max(l[1] for l in levels)] if levels else None


def summarize(data, map_entry, query) -> dict:
    unit_levels = data.unit_levels()
    zones: dict[int, list] = {}
    places: dict[str, list] = {}
    givers_outside = []
    giver_units = {unit_id for unit_id, unit in data.units.items() if len(unit.quests)}
    for record in unit_spawn_records(map_entry):
        levels = unit_levels.get(record.entry, (0, 0))
        area = query.area_at(record.x, record.z) or 0
        zones.setdefault(area, []).append(levels)
        pois = query.atlas.pois_at(record.x, record.z) if query.atlas else []
        if pois:
            places.setdefault(pois[0]["name"], []).append(levels)
        elif query.atlas and record.entry in giver_units:
            givers_outside.append(f"{data.units[record.entry].name} at ({record.x:.0f}, {record.z:.0f})")
    present_areas = set(np.unique(query.snapshot.area).tolist())
    no_terrain = sorted(zone.name for zone_id, zone in data.zones.items()
                        if zone_id not in present_areas and (not zone.HasField("map") or zone.map == map_entry.id))
    open_asks = []
    if query.atlas:
        for key in ("zones", "pois", "roads"):
            for entry in query.atlas.doc[key]:
                if entry.get("status") == "placeholder" and entry.get("ask"):
                    open_asks.append(f"{entry['name']}: {entry['ask']}")
    return {
        "zones": {(data.zones[a].name if a in data.zones else ("(no zone)" if a == 0 else f"area {a}")): {"spawns": len(l), "levels": _band(l)}
                  for a, l in sorted(zones.items())},
        "places": {name: {"spawns": len(l), "levels": _band(l)} for name, l in sorted(places.items())},
        "quest_givers_outside_places": givers_outside,
        "zones_without_terrain": no_terrain,
        "open_asks": open_asks,
    }


def to_markdown(map_entry, new: list[Finding], known: list[Finding], summary: dict) -> str:
    lines = [f"# Content report: map {map_entry.id} ({map_entry.name})", ""]
    lines.append(f"**New findings:** {len(new)} (errors: {sum(f.severity == 'error' for f in new)}), known (baselined): {len(known)}")
    lines.append("")
    for finding in new:
        lines.append(f"- **{finding.severity.upper()}** `{finding.rule}` {finding.message}")
    if known:
        lines += ["", "## Known findings (baselined)", ""]
        lines += [f"- `{finding.rule}` {finding.message}" for finding in known]
    lines += ["", "## Spawns per zone", "", "| Zone | Spawns | Levels |", "|---|---|---|"]
    for name, row in summary["zones"].items():
        lines.append(f"| {name} | {row['spawns']} | {row['levels']} |")
    if summary["places"]:
        lines += ["", "## Spawns per place", "", "| Place | Spawns | Levels |", "|---|---|---|"]
        for name, row in summary["places"].items():
            lines.append(f"| {name} | {row['spawns']} | {row['levels']} |")
    for title, key in (("Quest givers outside any place", "quest_givers_outside_places"),
                       ("Zones without terrain", "zones_without_terrain"),
                       ("Open questions in the atlas", "open_asks")):
        if summary[key]:
            lines += ["", f"## {title}", ""] + [f"- {item}" for item in summary[key]]
    return "\n".join(lines) + "\n"
