# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Wires the level-up column into game data: a SpellVisualization that plays
``Particles/LevelUp.hpar``, and a player trigger that fires it on level up.

Run from the repo root::

    python tools/particle_gen/recipes/level_up_data.py

Idempotent -- re-running updates the two entries in place rather than appending duplicates.

Writes three files:
  data/editor/data/spell_visualizations.data     server/editor copy
  data/client/ClientDB/spell_visualizations.data client copy (same wire format)
  data/editor/data/triggers.data                 server only; triggers never ship to clients
"""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / ".claude" / "skills" /
                       "mmo-spell-designer" / "scripts"))

import proto_runtime  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[3]

VISUALIZATION_ID = 40
TRIGGER_ID = 51

# Must match src/shared/proto_data/trigger_helper.h.
TRIGGER_FLAG_PLAYER = 0x0008
EVENT_ON_PLAYER_LEVEL_UP = 25
ACTION_PLAY_SPELL_VISUAL = 37
TARGET_OWNING_OBJECT = 1

# proto SpellVisualEvent. IMPACT is the one-shot slot: the cast and aura events expect a
# matching lifecycle event to tear their effects down, and nothing raises those for a
# visualization played by id.
VISUAL_EVENT_IMPACT = 4

SPELL_VISUALIZATION_FILES = [
    ROOT / "data" / "editor" / "data" / "spell_visualizations.data",
    ROOT / "data" / "client" / "ClientDB" / "spell_visualizations.data",
]
TRIGGERS_FILE = ROOT / "data" / "editor" / "data" / "triggers.data"


def upsert(entries, entry_id):
    """Return the existing entry with this id, or a freshly appended one."""
    for entry in entries:
        if entry.id == entry_id:
            entry.Clear()
            entry.id = entry_id
            return entry
    entry = entries.add()
    entry.id = entry_id
    return entry


def build_visualization(entry):
    entry.name = "Level Up"

    kit = entry.kits_by_event[VISUAL_EVENT_IMPACT].kits.add()
    kit.scope = 0                                  # CASTER -- the unit the visual plays on
    kit.loop = False
    kit.particles.append("Particles/LevelUp.hpar")
    # No attach_bone on purpose: the effect then hangs off the unit's own scene node (its feet),
    # which is what keeps the column's ground flare on the terrain rather than at chest height.
    # No animation either -- a level-up pose would fight whatever the player is doing.


def build_trigger(entry):
    entry.name = "Player - Level Up Visual"
    # Players carry no trigger list of their own, so this flag is what puts the trigger in front
    # of every player character.
    entry.flags = TRIGGER_FLAG_PLAYER

    event = entry.newevents.add()
    event.type = EVENT_ON_PLAYER_LEVEL_UP
    # No data: fires on every level. Setting data[0] to a level would restrict it to that level.

    action = entry.actions.add()
    action.action = ACTION_PLAY_SPELL_VISUAL
    action.target = TARGET_OWNING_OBJECT           # the player the trigger fired for
    action.data.append(VISUALIZATION_ID)
    action.data.append(VISUAL_EVENT_IMPACT)


def main():
    proto_runtime.compile_proto_modules(ROOT)
    import spell_visualizations_pb2
    import triggers_pb2

    for path in SPELL_VISUALIZATION_FILES:
        dataset = spell_visualizations_pb2.SpellVisualizations()
        dataset.ParseFromString(path.read_bytes())
        build_visualization(upsert(dataset.entry, VISUALIZATION_ID))
        path.write_bytes(dataset.SerializeToString())
        print("wrote %s (%d visualizations)" % (path.relative_to(ROOT), len(dataset.entry)))

    triggers = triggers_pb2.Triggers()
    triggers.ParseFromString(TRIGGERS_FILE.read_bytes())
    build_trigger(upsert(triggers.entry, TRIGGER_ID))
    TRIGGERS_FILE.write_bytes(triggers.SerializeToString())
    print("wrote %s (%d triggers)" % (TRIGGERS_FILE.relative_to(ROOT), len(triggers.entry)))


if __name__ == "__main__":
    main()
