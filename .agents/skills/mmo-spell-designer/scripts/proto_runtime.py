#!/usr/bin/env python3
"""Helpers for compiling and loading MMO protobuf modules on demand."""

from __future__ import annotations

import importlib
import subprocess
import sys
import tempfile
from pathlib import Path


PROTO_FILES = [
    "src/shared/proto_data/spells.proto",
    "src/shared/proto_data/items.proto",
    "src/shared/proto_data/classes.proto",
    "src/shared/proto_data/races.proto",
    "src/shared/proto_data/spell_categories.proto",
    "src/shared/proto_data/spell_visualizations.proto",
    "src/shared/proto_data/proficiencies.proto",
    "src/shared/proto_data/skills.proto",
    "src/shared/proto_data/aura_stacking_categories.proto",
    "src/shared/proto_data/item_classes.proto",
    "src/shared/proto_data/item_subclasses.proto",
]


def find_project_root(explicit: str | None = None) -> Path:
    if explicit:
        return Path(explicit).resolve()
    return Path(__file__).resolve().parents[4]


def find_protoc(project_root: Path) -> Path:
    candidates = [
        project_root / "build/_deps/protobuf-build/Release/protoc.exe",
        project_root / "build/_deps/protobuf-build/RelWithDebInfo/protoc.exe",
        project_root / "build/_deps/protobuf-build/Debug/protoc.exe",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError("Unable to locate protoc.exe under build/_deps/protobuf-build")


def compile_proto_modules(project_root: Path) -> Path:
    protoc = find_protoc(project_root)
    out_dir = Path(tempfile.mkdtemp(prefix="mmo_spell_proto_"))
    command = [
        str(protoc),
        f"-I{project_root / 'src'}",
        f"--python_out={out_dir}",
    ]
    command.extend(str(project_root / path) for path in PROTO_FILES)
    subprocess.run(command, check=True)
    if str(out_dir) not in sys.path:
        sys.path.insert(0, str(out_dir))
    return out_dir


def load_modules(project_root: Path) -> dict[str, object]:
    compile_proto_modules(project_root)
    module_names = {
        "spells": "shared.proto_data.spells_pb2",
        "items": "shared.proto_data.items_pb2",
        "classes": "shared.proto_data.classes_pb2",
        "races": "shared.proto_data.races_pb2",
        "spell_categories": "shared.proto_data.spell_categories_pb2",
        "spell_visualizations": "shared.proto_data.spell_visualizations_pb2",
        "proficiencies": "shared.proto_data.proficiencies_pb2",
        "skills": "shared.proto_data.skills_pb2",
        "aura_stacking_categories": "shared.proto_data.aura_stacking_categories_pb2",
        "item_classes": "shared.proto_data.item_classes_pb2",
        "item_subclasses": "shared.proto_data.item_subclasses_pb2",
    }
    return {key: importlib.import_module(name) for key, name in module_names.items()}
