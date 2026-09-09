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

# class id -> (item display ids in application order, ready animation state)
OUTFITS = {
    0: ([51, 55, 54, 52, 50, 76], "2HLReady"),   # Mage: Battlemage white set + bent staff
    1: ([72, 164, 165, 169, 111, 130], "1HReady"),  # Warrior: guard kit + sword and shield
    2: ([155, 154, 157, 153, 150, 75], "1HReady"),  # Cleric: Luminous Aegis set + mace
    3: ([131, 132, 121, 133], "2HLReady"),       # Acolyte: initiate robes + acolyte staff
    4: ([72, 70, 115, 117, 74], "1HReady"),      # Scout: traveller leathers + dagger
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
    out_dir = Path(tempfile.mkdtemp(prefix="mmo_outfits_proto_"))
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

        displays, animation = OUTFITS[entry.id]

        # Replace rather than append so the script is safe to re-run.
        del entry.outfits[:]

        outfit = entry.outfits.add()
        outfit.race = -1
        outfit.gender = -1
        outfit.animation = animation
        outfit.item_displays.extend(displays)

        print(f"{path}: class {entry.id} ({entry.name}) -> {list(displays)} / {animation}")

    if dry_run:
        return

    path.write_bytes(classes.SerializeToString())


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    classes_pb2 = load_classes_module(repo_root)

    for relative in DATA_FILES:
        path = repo_root / relative
        if not path.is_file():
            raise SystemExit(f"{path} not found. Did you initialise the data submodules?")

        apply(path, classes_pb2, args.dry_run)


if __name__ == "__main__":
    main()
