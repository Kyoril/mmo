# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the creature and gate spawns of the Hollow Choir (map 1) in the Monastery_001 world model.

    py -3 tools/hollow_choir/author_spawns.py            # validate, report, render the check image
    py -3 tools/hollow_choir/author_spawns.py --apply    # also write maps.data (editor + ClientDB)

Replaces every creature spawn on map 1 (the old crypt trash stood at coordinates of the previous
geometry) and the two Door_01 spawns that stood where the G1 and G2 seals now go. Design and room
map: docs/hollow_choir_bosses.md, section "Layout".

World orientation: the monastery is rotated in the world. The design sketch's north (towards the
apse) is world -X, its east is world -Z. Floors: entrance hall y 1.2, Wake and nave y 0.2, the
raised west wing (apse, cloister) y 5.2, crypt y -3.

Spacing rule (user, 2026-10-08): pulling one group must not drag others along. Creatures in combat
call idle allies within 8 units (creature_ai_idle_state.cpp), so the members of two different
groups must stay at least MIN_GROUP_GAP apart. Patrols are the deliberate exception: they are meant
to wander into fights.
"""

import argparse
import json
import math
import shutil
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-npc-designer/scripts"))
sys.path.insert(0, str(ROOT / "tools/world"))
from proto_runtime import load_modules  # noqa: E402
from worldkit.nav import NavQuery  # noqa: E402

EDITOR = ROOT / "data/editor/data"
CLIENTDB = ROOT / "data/client/ClientDB"
MAP_ID = 1

ASSIST_RADIUS = 8.0
# Assist radius plus room for a pulled pack to drift while it fights.
MIN_GROUP_GAP = 12.0

OSWIN, MERETH, VEYR = 86, 87, 88
W, K, Z, N = 92, 93, 94, 95   # Gravewarden, Mourning Cantor, Candlebearer, Restless Novice

# Facing in the engine's convention: FacingToDirection(f) = (cos f, 0, -sin f).
EAST, WEST, SOUTH, NORTH = 0.0, math.pi, -math.pi / 2, math.pi / 2

# Floor levels, used to pick the right floor when the navmesh has several at one x/z.
HALL, WAKE, RAISED, CRYPT = 1.2, 0.2, 5.2, -3.0

# Compact pack: offsets from the group centre, in world units.
PACK3 = [(0.0, 0.0), (-1.8, 1.6), (-1.8, -1.6)]
PACK4 = [(0.0, 0.0), (-1.8, 1.8), (-1.8, -1.8), (-3.4, 0.0)]
PACK2 = [(0.0, 0.9), (0.0, -0.9)]

# id, sketch letter, room, members, centre (x, z), level, facing
GROUPS = [
    ("A", "01 Vorhalle", [W, N, N], (-13.0, -1.0), HALL, EAST),
    ("B", "02 Totenwache", [W, Z, N], (-15.0, -14.0), WAKE, SOUTH),
    ("C", "03 Kirchenschiff", [W, N, N], (-37.5, 9.0), WAKE, EAST),
    ("D", "03 Kirchenschiff", [W, K, N, N], (-64.0, -9.0), WAKE, EAST),
    ("E", "03 Kirchenschiff", [W, Z, N, N], (-56.0, 9.0), WAKE, EAST),
    ("F", "03 Kirchenschiff", [W, K, Z], (-75.0, 8.5), WAKE, EAST),
    ("H", "04 Kreuzgang", [W, N, N], (-79.0, 21.0), RAISED, EAST),
    ("I", "04 Kreuzgang", [W, K, Z, Z], (-102.5, 21.3), RAISED, SOUTH),
    ("J", "06 Apsis", [W, N, N], (-91.0, 16.0), RAISED, NORTH),
    # K stands in the apse's north passage: the round room is too small to keep a pack more than
    # 12 units from Veyr on the podium.
    ("K", "06 Apsis", [W, K, Z], (-91.0, -15.5), RAISED, SOUTH),
    ("M", "Sakristei", [Z, N], (-37.0, -20.0), WAKE, SOUTH),
]

# Groups where the floor is too cramped for a rotated pack: member positions picked by hand from
# the navmesh probe (each list matches the group's members).
EXPLICIT = {
    # The cloister holds two packs and a patrol at 12-unit spacing: the packs hold its two northern
    # corners, the patrol the other three arcades.
    "I": [(-101.5, 20.5), (-103.5, 20.5), (-101.5, 22.2), (-103.5, 22.2)],
    "H": [(-80.5, 21.0), (-78.5, 20.3), (-78.5, 22.0)],
    # The round apse is too small to keep a pack 12 units from Veyr on the podium: J and K stand
    # in the passages left and right of it, J on the apse side of the sealed cloister corridor.
    "J": [(-91.0, 15.0), (-92.2, 16.6), (-89.8, 16.6)],
    "K": [(-91.0, -14.5), (-92.2, -16.2), (-89.8, -16.2)],
}

# Spawn name, unit, (x, z), level, facing. Triggers address the bosses by these names.
BOSSES = [
    ("HollowChoir_Oswin", OSWIN, (-6.5, -40.0), WAKE, SOUTH),
    # Mereth holds the crypt below the south-west stair: the centre aisle of the hall, facing the
    # corridor the group comes down (+z is SOUTH in the facing convention).
    ("HollowChoir_Mereth", MERETH, (-127.0, -3.0), CRYPT, SOUTH),
    ("HollowChoir_Veyr", VEYR, (-92.0, 0.0), RAISED, EAST),
]

# id, room, members, waypoints [(x, z)], level, wait at the turns (ms). Both members walk the same
# loop on lanes 1.2 units apart, starting together.
PATROLS = [
    ("P1", "03 Kirchenschiff", [W, Z],
     [(-40.0, -4.7), (-75.0, -4.5), (-75.5, 4.6), (-40.0, 4.8)], WAKE, 4000),
    ("P2", "04 Kreuzgang", [N, N],
     [(-103.0, 26.0), (-103.0, 39.5), (-79.0, 39.5), (-79.0, 26.0)], RAISED, 3000),
]

# Gates: (spawn name, centre x, y, z). Seals are FP_Wall_01 pieces, 6 wide, facing along X
# (the client turns world objects by +90 degrees, so identity rotation blocks an X-running way).
SEAL_ENTRY = 14
GATES = [
    ("HollowChoir_G1", -27.0, HALL, 0.0, None),
    ("HollowChoir_G2_North", -85.5, RAISED, -3.0, None),
    ("HollowChoir_G2_South", -85.5, RAISED, 3.0, None),
    # Apse <-> cloister corridor, sealed at the cloister end (group J stands in the corridor on the
    # apse side). Opens with G2, so the cloister cannot be used to skip it.
    ("HollowChoir_G2_Corridor", -91.0, RAISED, 18.4, "rotated"),
]
# The old Door_01 spawns these replace (G1 corridor, apse <-> cloister corridor).
REPLACED_DOORS = [(-27.0, 0.0), (-91.0, 12.0)]


def rotate(offset, facing):
    """Turn a pack offset (x forward, z right) to the group's facing."""
    fx, fz = math.cos(facing), -math.sin(facing)
    rx, rz = -fz, fx
    return offset[0] * fx + offset[1] * rx, offset[0] * fz + offset[1] * rz


def floor_height(nav, x, z, level):
    """The navmesh floor at x/z closest to the expected level, or None."""
    best = None
    for step in range(-8, 9):
        y = level + step * 0.25
        distance = nav.on_mesh((x, y, z), radius=0.5)
        if distance is not None and distance < 0.5 and (best is None or abs(y - level) < abs(best - level)):
            best = y
    return best


def plan_spawns(nav):
    """Every creature spawn as (group id, name, unit, x, y, z, facing, waypoints)."""
    spawns, problems = [], []
    for group, room, members, (cx, cz), level, facing in GROUPS:
        if group in EXPLICIT:
            positions = EXPLICIT[group]
        else:
            offsets = {2: PACK2, 3: PACK3, 4: PACK4}[len(members)]
            positions = []
            for offset in offsets:
                dx, dz = rotate(offset, facing)
                positions.append((cx + dx, cz + dz))
        assert len(positions) == len(members), group
        for i, (unit, (x, z)) in enumerate(zip(members, positions)):
            y = floor_height(nav, x, z, level)
            if y is None:
                problems.append(f"{group}{i + 1} at ({x:.1f}, {z:.1f}) is off the navmesh")
                y = level
            spawns.append((group, f"HollowChoir_{group}{i + 1}", unit, x, y, z, facing, []))
    for name, unit, (x, z), level, facing in BOSSES:
        y = floor_height(nav, x, z, level)
        if y is None:
            problems.append(f"{name} at ({x:.1f}, {z:.1f}) is off the navmesh")
            y = level
        spawns.append((name, name, unit, x, y, z, facing, []))
    for group, room, members, points, level, wait in PATROLS:
        for i, unit in enumerate(members):
            lane = (i - (len(members) - 1) / 2) * 1.2
            route = []
            for px, pz in points:
                y = floor_height(nav, px, pz + lane, level)
                if y is None:
                    problems.append(f"{group} waypoint ({px:.1f}, {pz + lane:.1f}) is off the navmesh")
                    y = level
                route.append((px, y, pz + lane, wait))
            x, y, z, _ = route[0]
            spawns.append((group, f"HollowChoir_{group}_{i + 1}", unit, x, y, z, WEST, route))
    return spawns, problems


# Groups a solid wall separates: assisting needs sight of the ally, so the gap does not apply.
WALLED_PAIRS = {frozenset(("A", "B")), frozenset(("I", "J")), frozenset(("H", "J"))}


def check_spacing(spawns):
    """Pairs of creatures from different static groups that stand too close together."""
    static = [s for s in spawns if not s[7]]
    problems = []
    for i, a in enumerate(static):
        for b in static[i + 1:]:
            if a[0] == b[0] or frozenset((a[0], b[0])) in WALLED_PAIRS:
                continue
            gap = math.hypot(a[3] - b[3], a[5] - b[5])
            if gap < MIN_GROUP_GAP and abs(a[4] - b[4]) < 3.0:
                problems.append(f"{a[1]} and {b[1]} are {gap:.1f} apart (minimum {MIN_GROUP_GAP:g})")
    return problems


def render(spawns, out):
    from PIL import Image, ImageDraw, ImageFont
    survey = ROOT / "generated/hollow_choir/layout.png"
    image = Image.open(survey).convert("RGBA")
    overlay = Image.new("RGBA", image.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    font = ImageFont.load_default()
    framing = json.loads(survey.with_suffix(".json").read_text())   # written by survey_layout.py
    x0, z0, ppu = framing["x0"], framing["z0"], framing["ppu"]

    def px(x, z):
        return (x - x0) * ppu, (z - z0) * ppu

    colours = {OSWIN: (255, 80, 200), MERETH: (255, 80, 200), VEYR: (255, 80, 200)}
    for group, name, unit, x, y, z, facing, route in spawns:
        if route:
            points = [px(p[0], p[2]) for p in route] + [px(route[0][0], route[0][2])]
            draw.line(points, fill=(120, 200, 255, 160), width=1)
        cx, cy = px(x, z)
        r = ASSIST_RADIUS * ppu
        if not route and unit not in colours:
            draw.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(255, 90, 90, 70))
        dot = colours.get(unit, (255, 230, 80) if route else (255, 70, 70))
        size = 6 if unit in colours else 3
        draw.ellipse([cx - size, cy - size, cx + size, cy + size], fill=dot + (255,))
    for group, room, members, (cx, cz), level, facing in GROUPS:
        p = px(cx, cz)
        draw.text((p[0] + 10, p[1] - 14), group, fill=(255, 255, 255, 255), font=font)
    for name, unit, (x, z), level, facing in BOSSES:
        p = px(x, z)
        draw.text((p[0] + 9, p[1] - 6), name.split("_")[1], fill=(255, 160, 230, 255), font=font)
    for name, x, y, z, rot in GATES:
        a, b = (px(x - 0.5, z - 3), px(x + 0.5, z + 3)) if rot is None else (px(x - 3, z - 0.5), px(x + 3, z + 0.5))
        draw.rectangle([a, b], fill=(80, 230, 230, 230))
    Image.alpha_composite(image, overlay).convert("RGB").save(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    with NavQuery(ROOT / "data/editor/nav", "Test", exe=ROOT / "bin/Release/nav_query.exe") as nav:
        spawns, problems = plan_spawns(nav)
    problems += check_spacing(spawns)

    out = ROOT / "generated/hollow_choir/spawns.png"
    render(spawns, out)
    print(f"{len(spawns)} creature spawns; check image {out.relative_to(ROOT)}")
    for problem in problems:
        print("PROBLEM:", problem)
    if problems:
        sys.exit(1)

    mods = load_modules(ROOT)
    maps = mods["maps"].Maps()
    maps.ParseFromString((EDITOR / "maps.data").read_bytes())
    dungeon = next(m for m in maps.entry if m.id == MAP_ID)

    del dungeon.unitspawns[:]
    for group, name, unit, x, y, z, facing, route in spawns:
        spawn = dungeon.unitspawns.add()
        spawn.name = name
        # No trash respawns during a run; an instance reset brings everything back.
        spawn.respawn = False
        spawn.positionx, spawn.positiony, spawn.positionz = x, y, z
        spawn.rotation = facing % (2 * math.pi)
        spawn.unitentry = unit
        spawn.isactive = True
        if route:
            spawn.movement = mods["maps"].UnitSpawnEntry.PATROL
            for wx, wy, wz, wait in route:
                waypoint = spawn.waypoints.add()
                waypoint.positionx, waypoint.positiony, waypoint.positionz = wx, wy, wz
                waypoint.waittime = wait

    kept = [o for o in dungeon.objectspawns
            if not any(abs(o.location.positionx - x) < 0.5 and abs(o.location.positionz - z) < 0.5
                       for x, z in REPLACED_DOORS)
            and not o.name.startswith("HollowChoir_")]
    del dungeon.objectspawns[:]
    dungeon.objectspawns.extend(kept)
    for name, x, y, z, rot in GATES:
        seal = dungeon.objectspawns.add()
        seal.name = name
        seal.respawn = False
        seal.state = 0          # closed
        seal.objectentry = SEAL_ENTRY
        seal.isactive = True
        seal.location.positionx, seal.location.positiony, seal.location.positionz = x, y, z
        if rot:
            seal.location.rotationw, seal.location.rotationy = 0.7071068, 0.7071068
        else:
            seal.location.rotationw = 1.0
        seal.location.rotationx = seal.location.rotationz = 0.0
        if not rot:
            seal.location.rotationy = 0.0

    # The seal: a trigger-only door (NotInteractable) on the 6-wide FP_Wall_01 display. Art is a
    # placeholder; the boss death triggers open it with SetWorldObjectState.
    import importlib
    objects_pb2 = importlib.import_module("objects_pb2")
    objects = objects_pb2.Objects()
    objects.ParseFromString((EDITOR / "objects.data").read_bytes())
    seal_entry = next((o for o in objects.entry if o.id == SEAL_ENTRY), None) or objects.entry.add()
    seal_entry.Clear()
    seal_entry.id = SEAL_ENTRY
    seal_entry.name = "Hollow Choir Seal"
    seal_entry.type = 1                 # door
    seal_entry.displayid = 8            # FP_Wall_01, 6 x 4
    seal_entry.factionid = 0
    seal_entry.scale = 1.0
    seal_entry.flags = 0x04             # NotInteractable: only triggers open it
    seal_entry.data.extend([3, 0, 0])   # door lock, no post-unlock lock, never auto-closes

    print(f"map {MAP_ID}: {len(dungeon.unitspawns)} creature spawns, {len(dungeon.objectspawns)} object spawns")
    if not args.apply:
        print("validated; pass --apply to write")
        return

    backup = ROOT / "generated/hollow_choir/backup" / datetime.now().strftime("%Y%m%d%H%M%S")
    backup.mkdir(parents=True, exist_ok=True)
    for path in (EDITOR / "maps.data", CLIENTDB / "maps.data"):
        shutil.copy2(path, backup / f"{path.parent.name}_{path.name}")
        path.write_bytes(maps.SerializeToString())
        print(f"wrote {path.relative_to(ROOT)}")
    path = EDITOR / "objects.data"
    shutil.copy2(path, backup / f"{path.parent.name}_{path.name}")
    path.write_bytes(objects.SerializeToString())
    print(f"wrote {path.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
