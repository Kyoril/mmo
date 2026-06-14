#!/usr/bin/env python3
"""Helpers for compiling and loading MMO protobuf modules for quest tooling."""

from __future__ import annotations

import importlib
import subprocess
import sys
import tempfile
from pathlib import Path


def find_project_root(explicit: str | None = None) -> Path:
    if explicit:
        return Path(explicit).resolve()
    return Path(__file__).resolve().parents[4]


def find_proto_dir(project_root: Path) -> Path:
    return project_root / "src" / "shared" / "proto_data"


def find_protoc(project_root: Path) -> Path:
    candidates = [
        project_root / "build" / "_deps" / "protobuf-build" / "Release" / "protoc.exe",
        project_root / "build" / "_deps" / "protobuf-build" / "RelWithDebInfo" / "protoc.exe",
        project_root / "build" / "_deps" / "protobuf-build" / "Debug" / "protoc.exe",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError("Unable to locate protoc.exe under build/_deps/protobuf-build")


def compile_proto_modules(project_root: Path) -> Path:
    proto_dir = find_proto_dir(project_root)
    protoc = find_protoc(project_root)
    out_dir = Path(tempfile.mkdtemp(prefix="mmo_quest_proto_"))
    proto_files = sorted(path.name for path in proto_dir.glob("*.proto"))
    command = [
        str(protoc),
        f"-I{proto_dir}",
        f"--python_out={out_dir}",
        *proto_files,
    ]
    subprocess.run(command, check=True, cwd=proto_dir)
    if str(out_dir) not in sys.path:
        sys.path.insert(0, str(out_dir))
    return out_dir


def load_modules(project_root: Path) -> dict[str, object]:
    compile_proto_modules(project_root)
    module_names = {
        "quests": "quests_pb2",
        "units": "units_pb2",
        "objects": "objects_pb2",
        "items": "items_pb2",
        "spells": "spells_pb2",
        "classes": "classes_pb2",
        "races": "races_pb2",
        "skills": "skills_pb2",
        "triggers": "triggers_pb2",
        "area_triggers": "area_triggers_pb2",
        "maps": "maps_pb2",
    }
    return {key: importlib.import_module(name) for key, name in module_names.items()}
