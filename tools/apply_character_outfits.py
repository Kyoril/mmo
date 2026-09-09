#!/usr/bin/env python3
"""Writes the character creation preview outfits into classes.data.

Both the editor project file and the client's exported ClientDB copy are written, because
the editor's client export is a plain file copy and re-running it is a manual step.

Run from the repository root:

    python tools/apply_character_outfits.py

Pass --dry-run to print what would change without writing anything.
"""

from __future__ import annotations

import argparse
import importlib
import subprocess
import sys
import tempfile
from pathlib import Path

ANY = -1
RACE_ORC = 1
GENDER_MALE = 0

# Item display ids per class, in application order.
MAGE_SET = [51, 55, 54, 52, 50, 76]         # Battlemage white set + bent staff
WARRIOR_SET = [72, 164, 165, 169, 111, 130]  # Guard kit + sword and shield
CLERIC_SET = [155, 154, 157, 153, 150, 75]   # Luminous Aegis set + mace
ACOLYTE_SET = [131, 132, 121, 133]           # Initiate robes + acolyte staff
SCOUT_SET = [72, 70, 115, 117, 74]           # Traveller leathers + dagger

# The Orc meshes name their sub entities SK_Or_*, so the Human targeted armor displays are inert
# on them and only the bone attached weapon would render. Until Orc armor art exists, Orc gets an
# empty outfit which falls back to today's behaviour: naked body, default idle animation.
ORC_SUPPRESSED = (RACE_ORC, ANY, [], "")

# class id -> list of scoped outfit entries as (race, gender, item display ids, animation).
# -1 is the wildcard for race and gender. SelectCharacterOutfit scores an explicit race 2 and an
# explicit gender 1, so a more specific entry always beats the wildcard one.
#
# Skeleton coverage drives the animation scoping. HumanFemale_V2.skel (models 7 and 18) has
# 1HReady, 2HReady and 2HLReady; HumanMale.skel (models 8 and 17) only has 1HReady; the two Orc
# skeletons have neither, only Idle. So the staff classes need a male scoped entry, which covers
# both Human and Undead males because Undead reuses the Human meshes and skeletons.
OUTFITS = {
    0: [  # Mage
        (ANY, ANY, MAGE_SET, "2HLReady"),
        (ANY, GENDER_MALE, MAGE_SET, "1HReady"),
        ORC_SUPPRESSED,
    ],
    1: [  # Warrior
        (ANY, ANY, WARRIOR_SET, "1HReady"),
        ORC_SUPPRESSED,
    ],
    2: [  # Cleric
        (ANY, ANY, CLERIC_SET, "1HReady"),
        ORC_SUPPRESSED,
    ],
    3: [  # Acolyte
        (ANY, ANY, ACOLYTE_SET, "2HLReady"),
        (ANY, GENDER_MALE, ACOLYTE_SET, "1HReady"),
        ORC_SUPPRESSED,
    ],
    4: [  # Scout
        (ANY, ANY, SCOUT_SET, "1HReady"),
        ORC_SUPPRESSED,
    ],
}

DATA_FILES = (
    Path("data") / "editor" / "data" / "classes.data",
    Path("data") / "client" / "ClientDB" / "classes.data",
)


def find_protoc(repo_root: Path) -> Path:
    for config in ("Release", "RelWithDebInfo", "Debug"):
        candidate = repo_root / "build" / "_deps" / "protobuf-build" / config / "protoc.exe"
        if candidate.is_file():
            return candidate

    raise SystemExit(
        "protoc.exe not found under build/_deps/protobuf-build. Configure and build the "
        "project first."
    )


def load_classes_module(repo_root: Path):
    proto_dir = repo_root / "src" / "shared" / "proto_data"
    with tempfile.TemporaryDirectory(prefix="mmo_outfits_proto_") as out_dir:
        subprocess.run(
            [str(find_protoc(repo_root)), f"-I{proto_dir}", f"--python_out={out_dir}", "classes.proto"],
            check=True,
            cwd=proto_dir,
        )
        sys.path.insert(0, str(out_dir))
        return importlib.import_module("classes_pb2")


def apply(path: Path, classes_pb2, dry_run: bool) -> None:
    classes = classes_pb2.Classes()
    classes.ParseFromString(path.read_bytes())

    for entry in classes.entry:
        if entry.id not in OUTFITS:
            continue

        # Replace rather than append so the script is safe to re-run.
        del entry.outfits[:]

        for race, gender, displays, animation in OUTFITS[entry.id]:
            outfit = entry.outfits.add()
            outfit.race = race
            outfit.gender = gender
            outfit.animation = animation
            outfit.item_displays.extend(displays)

            print(
                f"{path}: class {entry.id} ({entry.name}) race={race} gender={gender} "
                f"-> {list(displays)} / {animation or '<default>'}"
            )

    if dry_run:
        return

    path.write_bytes(classes.SerializeToString())


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    paths = [repo_root / relative for relative in DATA_FILES]
    for path in paths:
        if not path.is_file():
            raise SystemExit(f"{path} not found. Did you initialise the data submodules?")

    classes_pb2 = load_classes_module(repo_root)

    for path in paths:
        apply(path, classes_pb2, args.dry_run)


if __name__ == "__main__":
    main()
