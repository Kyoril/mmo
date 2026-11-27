# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Loads the protobuf game data (data/editor/data/*.data) for world tooling.

Schemas are compiled with the repository's protoc from src/shared/proto_data, so worldkit always
reads the same schema version as the authoring skills.
"""

from __future__ import annotations

import importlib
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

from .paths import REPO, data_dir, skill_scripts

# key -> (python module, container message, data file)
_CATALOGS = {
    "units": ("units_pb2", "Units", "units.data"),
    "maps": ("maps_pb2", "Maps", "maps.data"),
    "quests": ("quests_pb2", "Quests", "quests.data"),
    "zones": ("zones_pb2", "Zones", "zones.data"),
    "objects": ("objects_pb2", "Objects", "objects.data"),
    "items": ("items_pb2", "Items", "items.data"),
    "unit_loot": ("unit_loot_pb2", "UnitLoot", "unit_loot.data"),
    "object_loot": ("object_loot_pb2", "ObjectLoot", "object_loot.data"),
    "triggers": ("triggers_pb2", "Triggers", "triggers.data"),
}
_MODULES: dict[str, dict] = {}


# Where CMake leaves protoc: next to the build dir in a single-config build (Makefiles, Ninja),
# in a per-configuration subdirectory in a multi-config one (Visual Studio, Ninja Multi-Config),
# and without the .exe suffix on everything that is not Windows.
_PROTOC_NAMES = ("protoc.exe", "protoc")
_PROTOC_CONFIGS = ("", "Release", "RelWithDebInfo", "Debug")


def find_protoc(repo: Path = REPO) -> Path:
    base = repo / "build" / "_deps" / "protobuf-build"
    candidates = [base / config / name for config in _PROTOC_CONFIGS for name in _PROTOC_NAMES]
    candidates += [path for name in _PROTOC_NAMES for path in sorted(base.glob("*/" + name))]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(f"Unable to locate protoc under {base}")


def _compile_proto_modules(repo: Path) -> Path:
    proto_dir = repo / "src" / "shared" / "proto_data"
    out_dir = Path(tempfile.mkdtemp(prefix="mmo_worldkit_proto_"))
    proto_files = sorted(path.name for path in proto_dir.glob("*.proto"))
    subprocess.run([str(find_protoc(repo)), f"-I{proto_dir}", f"--python_out={out_dir}", *proto_files],
                   check=True, cwd=proto_dir)
    if str(out_dir) not in sys.path:
        sys.path.insert(0, str(out_dir))
    return out_dir


def load_proto_modules(repo: Path = REPO) -> dict:
    key = str(repo)
    if key not in _MODULES:
        scripts = str(skill_scripts(repo))
        if scripts not in sys.path:
            sys.path.insert(0, scripts)
        # A skill script that imports worldkit has usually compiled the schemas already; each compile
        # leaves a temp dir behind, so only compile when the generated modules are not importable.
        if not all(module in sys.modules for module, _, _ in _CATALOGS.values()):
            _compile_proto_modules(repo)
        _MODULES[key] = {name: importlib.import_module(module) for name, (module, _, _) in _CATALOGS.items()}
    return _MODULES[key]


@dataclass
class GameData:
    units: dict
    maps: dict
    quests: dict
    zones: dict
    objects: dict
    items: dict
    unit_loot: dict
    object_loot: dict
    triggers: dict = field(default_factory=dict)
    modules: dict = field(repr=False, default_factory=dict)

    def map_by_directory(self, directory: str):
        for entry in self.maps.values():
            if entry.directory == directory:
                return entry
        return None

    def service_units(self) -> set[int]:
        """Units players talk to rather than fight: quest givers/enders, trainers, vendors, gossip NPCs."""
        return {uid for uid, unit in self.units.items()
                if len(unit.quests) or len(unit.end_quests) or unit.trainerentry or unit.vendorentry or len(unit.gossip_menus)}

    def unit_levels(self) -> dict[int, tuple[int, int]]:
        return {unit_id: (unit.minlevel, max(unit.minlevel, unit.maxlevel)) for unit_id, unit in self.units.items()}


def load_game_data(repo: Path = REPO, data_root: Path | None = None) -> GameData:
    modules = load_proto_modules(repo)
    root = data_root or data_dir(repo)
    catalogs = {}
    for key, (_, container, file_name) in _CATALOGS.items():
        message = getattr(modules[key], container)()
        message.ParseFromString((root / file_name).read_bytes())
        catalogs[key] = {entry.id: entry for entry in message.entry}
    return GameData(modules=modules, **catalogs)
