# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""One-shot atlas bootstrap: derives placeholder places from what the world already contains.

1. zones from area ids on the terrain (level band from the quests handed out inside them);
2. named places from spawn-name prefixes ("Barrowfield - Barrow Skeleton 07"), split into clusters;
   prefixes that are zone names label the zone, not a place, and are skipped;
3. places the user named in docs/world/setting-notes-2026-09-27.md, anchored on the largest cluster
   of their NPCs when those exist, parked in the middle of the content otherwise;
4. hubs from quest givers that are not inside a place yet;
5. unnamed creature clusters;
6. prop clusters (placed entities);
7. the named roads from the notes, as stubs starting at Oakenshire.
Everything is a placeholder with an `ask`; the user names, moves and confirms it in mmo_edit.
"""

from __future__ import annotations

import math

import numpy as np

from .atlas import Atlas, contains, empty_atlas
from .constants import CELL_SIZE
from .report import quest_givers
from .spawns import object_spawn_records, unit_spawn_records

DEFAULT_ASK = "Name this place, check its kind (hub/camp/ruin/lair/landmark/resource), level band and radius, then confirm."
KNOWN_PLACES = (
    {"name": "Forest Camp", "kind": "hub", "anchors": ("Farmer Haldor", "Healer Mirenna", "Guard Emrik"),
     "ask": "The camp where the human story begins (formal name not settled). Check position and radius, rename if you like, confirm."},
    {"name": "Haven", "kind": "hub", "anchors": ("Armsmaster Theobald Kerrin", "Sergeant Bram Halford", "Baldric Steelheart"),
     "ask": "Haven, the walled town (~level 10 hub). Drag this pin to where Haven is or should be built and confirm; "
            "the road from Oakenshire is meant to be 500-1000 m long."},
    {"name": "Dungeon Entrance", "kind": "dungeon_entrance", "anchors": (),
     "ask": "Where is the group dungeon beyond the forest line entered? Drag and confirm, or delete the pin if still undecided."},
)
KNOWN_ROADS = (
    {"name": "Westroad", "direction": (-1.0, 0.0),
     "ask": "Drag the start and end of the Westroad, add points where it bends, then confirm."},
    {"name": "Northroad", "direction": (0.0, 1.0),
     "ask": "Drag the start and end of the Northroad, add points where it bends, then confirm."},
)
NAMED_MIN_RADIUS = 20.0
LINK_NAMED = 60.0
LINK_ANCHORS = 80.0
LINK_GIVERS = 60.0
LINK_CREATURES = 40.0
LINK_PROPS = 30.0
MIN_CREATURE_CLUSTER = 4
MAX_CAMP_RADIUS = 90.0      # creatures spread wider than this are wildlife across a zone, not a camp
MIN_PROP_CLUSTER = 5


def cluster(points, link: float) -> list[list]:
    """Single-linkage clusters of (x, z, payload) triples; returns lists of payloads."""
    remaining = list(points)
    groups = []
    while remaining:
        frontier = [remaining.pop(0)]
        group = []
        while frontier:
            current = frontier.pop()
            group.append(current)
            near = [p for p in remaining if math.hypot(p[0] - current[0], p[1] - current[1]) <= link]
            for p in near:
                remaining.remove(p)
            frontier.extend(near)
        groups.append([p[2] for p in group])
    return groups


def _centre(xs, zs) -> list[float]:
    return [round(sum(xs) / len(xs), 1), round(sum(zs) / len(zs), 1)]


def _radius(centre, xs, zs, minimum: float) -> float:
    far = max(math.hypot(x - centre[0], z - centre[1]) for x, z in zip(xs, zs))
    return float(max(minimum, math.ceil((far + 10.0) / 5.0) * 5.0))


def _inside_any(atlas: Atlas, x: float, z: float) -> bool:
    return any(contains(p, x, z) for p in atlas.pois)


def _zone_centre(query, area_id: int) -> list[float] | None:
    rows, cols = np.nonzero(query.snapshot.area == area_id)
    if rows.size == 0:
        return None
    ox, oz = query.snapshot.origin
    return [round(float(ox + (cols.mean() + 0.5) * CELL_SIZE), 1), round(float(oz + (rows.mean() + 0.5) * CELL_SIZE), 1)]


def _add_zones(atlas: Atlas, data, query, givers) -> None:
    quest_levels_by_area: dict[int, list[int]] = {}
    for quest_id, points in givers.items():
        quest = data.quests.get(quest_id)
        level = quest.questlevel if quest is not None else 0
        for x, z in points:
            area = query.area_at(x, z)
            if area and level > 0:
                quest_levels_by_area.setdefault(area, []).append(level)
    for area in sorted({int(a) for a in np.unique(query.snapshot.area).tolist() if a}):
        zone = data.zones.get(area)
        fields = {"areaId": area, "name": zone.name if zone is not None else f"Area {area}", "status": "placeholder",
                  "ask": "Confirm the level band and write a one-line theme and description.", "source": "agent-bootstrap"}
        band = quest_levels_by_area.get(area)
        if band:
            fields["levelMin"], fields["levelMax"] = min(band), max(band)
        atlas.add_zone(**fields)


def _add_named_places(atlas: Atlas, data, query, records, giver_units, levels) -> None:
    by_prefix: dict[str, list] = {}
    for record in records:
        if record.poi_prefix:
            by_prefix.setdefault(record.poi_prefix.casefold(), []).append(record)
    zone_names = {zone.name.casefold() for zone in data.zones.values()}
    for key in sorted(by_prefix):
        if key in zone_names:
            continue  # "Briarwatch March - Bog Rat 03" labels the zone, not a place inside it
        clusters = cluster(sorted(((r.x, r.z, r) for r in by_prefix[key]), key=lambda p: p[2].key), LINK_NAMED)
        clusters.sort(key=len, reverse=True)
        if len(clusters) > 1:
            clusters = [c for c in clusters if len(c) > 1] or clusters[:1]
        for index, group in enumerate(clusters):
            xs, zs = [r.x for r in group], [r.z for r in group]
            centre = _centre(xs, zs)
            prefix = group[0].poi_prefix
            has_giver = any(r.kind == "unit" and r.entry in giver_units for r in group)
            fields = {"name": prefix if index == 0 else f"{prefix} ({centre[0]:.0f}, {centre[1]:.0f})",
                      "kind": "hub" if has_giver else "camp", "center": centre,
                      "radius": _radius(centre, xs, zs, NAMED_MIN_RADIUS), "status": "placeholder", "ask": DEFAULT_ASK,
                      "source": "agent-bootstrap"}
            area = query.area_at(*centre)
            if area:
                fields["areaId"] = area
            bands = [levels[r.entry] for r in group if r.kind == "unit" and r.entry in levels]
            if bands and not has_giver:
                fields["levelMin"] = min(b[0] for b in bands)
                fields["levelMax"] = max(b[1] for b in bands)
            atlas.add_poi(**fields)


def _add_known_places(atlas: Atlas, data, units, park_at: list[float]) -> None:
    unit_ids = {unit.name: uid for uid, unit in data.units.items()}
    parked = 0
    for place in KNOWN_PLACES:
        if atlas.poi_by_name(place["name"]):
            continue
        anchors = [(r.x, r.z, data.units[r.entry].name) for r in units
                   if r.entry in {unit_ids[n] for n in place["anchors"] if n in unit_ids}]
        if anchors:
            groups = sorted(cluster([(a[0], a[1], i) for i, a in enumerate(anchors)], LINK_ANCHORS), key=len, reverse=True)
            used = [anchors[i] for i in groups[0]]
            elsewhere = sorted({f"{a[2]} ({a[0]:.0f}, {a[1]:.0f})" for a in anchors if a not in used})
            xs, zs = [a[0] for a in used], [a[1] for a in used]
            centre = _centre(xs, zs)
            radius = _radius(centre, xs, zs, 30.0)
            ask = f"{place['ask']} Anchored on: {', '.join(sorted({a[2] for a in used}))}."
            if elsewhere:
                ask += f" Also found elsewhere: {', '.join(elsewhere)}."
        else:
            centre = [round(park_at[0] + parked * 40.0, 1), park_at[1]]
            radius = 30.0
            parked += 1
            ask = "PARKED (position unknown). " + place["ask"]
        atlas.add_poi(name=place["name"], kind=place["kind"], center=centre, radius=radius, status="placeholder",
                      ask=ask, source="agent-bootstrap")


def _add_giver_hubs(atlas: Atlas, data, query, units, giver_units) -> None:
    loose = [(r.x, r.z, r) for r in units if r.entry in giver_units and r.active and not _inside_any(atlas, r.x, r.z)]
    for group in cluster(sorted(loose, key=lambda p: p[2].key), LINK_GIVERS):
        xs, zs = [r.x for r in group], [r.z for r in group]
        centre = _centre(xs, zs)
        area = query.area_at(*centre)
        zone = data.zones.get(area) if area else None
        first = data.units[group[0].entry].name
        name = f"{zone.name} hub" if zone is not None and not atlas.poi_by_name(f"{zone.name} hub") else f"Hub near {first}"
        atlas.add_poi(name=name, kind="hub", center=centre, radius=_radius(centre, xs, zs, 25.0), status="placeholder",
                      ask=DEFAULT_ASK, source="agent-bootstrap")


def _camp_sized(group, link: float) -> list[list]:
    """Splits a creature cluster with ever tighter links until every piece is camp-sized."""
    xs, zs = [r.x for r in group], [r.z for r in group]
    if _radius(_centre(xs, zs), xs, zs, 0.0) <= MAX_CAMP_RADIUS or link <= 15.0:
        return [group]
    pieces = []
    for sub in cluster([(r.x, r.z, r) for r in group], link * 0.6):
        pieces.extend(_camp_sized(sub, link * 0.6))
    return pieces


def _add_creature_grounds(atlas: Atlas, data, units, giver_units, levels) -> None:
    loose = [(r.x, r.z, r) for r in units if r.active and r.entry not in giver_units and not _inside_any(atlas, r.x, r.z)]
    groups = [piece for group in cluster(sorted(loose, key=lambda p: p[2].key), LINK_CREATURES)
              for piece in _camp_sized(group, LINK_CREATURES)]
    for group in groups:
        if len(group) < MIN_CREATURE_CLUSTER:
            continue
        xs, zs = [r.x for r in group], [r.z for r in group]
        centre = _centre(xs, zs)
        counts: dict[int, int] = {}
        for r in group:
            counts[r.entry] = counts.get(r.entry, 0) + 1
        dominant = data.units.get(max(counts, key=counts.get))
        base = f"{dominant.name if dominant is not None else 'Creature'} grounds"
        name = base if not atlas.poi_by_name(base) else f"{base} ({centre[0]:.0f}, {centre[1]:.0f})"
        bands = [levels[r.entry] for r in group if r.entry in levels]
        atlas.add_poi(name=name, kind="camp", center=centre, radius=_radius(centre, xs, zs, NAMED_MIN_RADIUS),
                      levelMin=min(b[0] for b in bands) if bands else None,
                      levelMax=max(b[1] for b in bands) if bands else None,
                      status="placeholder", ask=DEFAULT_ASK, source="agent-bootstrap")


def _add_prop_landmarks(atlas: Atlas, query) -> None:
    props = [(e.position[0], e.position[2], e) for e in query.snapshot.entities
             if not _inside_any(atlas, e.position[0], e.position[2])]
    for group in cluster(sorted(props, key=lambda p: p[2].unique_id), LINK_PROPS):
        if len(group) < MIN_PROP_CLUSTER:
            continue
        xs, zs = [e.position[0] for e in group], [e.position[2] for e in group]
        centre = _centre(xs, zs)
        atlas.add_poi(name=f"Props near ({centre[0]:.0f}, {centre[1]:.0f})", kind="landmark", center=centre,
                      radius=_radius(centre, xs, zs, 15.0), status="placeholder",
                      ask="What is this place? Name it, pick its kind and confirm.", source="agent-bootstrap")


def _add_roads(atlas: Atlas, data, query, fallback: list[float]) -> None:
    start = atlas.poi_by_name("Oakenshire") or atlas.poi_by_name("Oakenshire hub")
    if start is not None:
        sx, sz = start["center"]
    else:
        zone_id = next((zid for zid, zone in data.zones.items() if zone.name.casefold() == "oakenshire"), None)
        sx, sz = (zone_id is not None and _zone_centre(query, zone_id)) or fallback
    for road in KNOWN_ROADS:
        dx, dz = road["direction"]
        atlas.add_road(name=road["name"], points=[[sx, sz], [round(sx + dx * 300.0, 1), round(sz + dz * 300.0, 1)]],
                       status="placeholder", ask=road["ask"], source="agent-bootstrap")


def bootstrap_atlas(data, map_entry, query) -> Atlas:
    atlas = empty_atlas(map_entry.id)
    units = unit_spawn_records(map_entry)
    objects = object_spawn_records(map_entry)
    levels = data.unit_levels()
    # People a hub is made of: quest givers and service NPCs (trainers, vendors, gossip).
    giver_units = {uid for uid, unit in data.units.items()
                   if len(unit.quests) or len(unit.end_quests) or unit.trainerentry or unit.vendorentry or len(unit.gossip_menus)}
    everything = units + objects
    content_centre = _centre([r.x for r in everything], [r.z for r in everything]) if everything else [0.0, 0.0]

    _add_zones(atlas, data, query, quest_givers(data, map_entry))
    _add_named_places(atlas, data, query, everything, giver_units, levels)
    _add_known_places(atlas, data, units, content_centre)
    _add_giver_hubs(atlas, data, query, units, giver_units)
    _add_creature_grounds(atlas, data, units, giver_units, levels)
    _add_prop_landmarks(atlas, query)
    _add_roads(atlas, data, query, content_centre)
    return atlas
