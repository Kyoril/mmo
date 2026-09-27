# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Repository-relative locations of every worldkit input and output."""

from __future__ import annotations

from pathlib import Path

# tools/world/worldkit/paths.py -> parents: worldkit, world, tools, <repo>
REPO = Path(__file__).resolve().parents[3]


def world_root(directory: str, repo: Path = REPO) -> Path:
    return repo / "data" / "client" / "Worlds" / directory / directory


def terrain_dir(directory: str, repo: Path = REPO) -> Path:
    return world_root(directory, repo) / "Terrain"


def entities_dir(directory: str, repo: Path = REPO) -> Path:
    return world_root(directory, repo) / "Entities"


def hwld_path(directory: str, repo: Path = REPO) -> Path:
    return repo / "data" / "client" / "Worlds" / directory / f"{directory}.hwld"


def atlas_path(map_id: int, repo: Path = REPO) -> Path:
    return repo / "data" / "world" / "atlas" / f"map_{map_id}.json"


def cache_dir(directory: str, repo: Path = REPO) -> Path:
    return repo / "generated" / "world" / directory


def data_dir(repo: Path = REPO) -> Path:
    return repo / "data" / "editor" / "data"


def skill_scripts(repo: Path = REPO) -> Path:
    """Scripts dir whose proto_runtime.py compiles the .proto schemas (tracked copy)."""
    return repo / ".agents" / "skills" / "mmo-quest-creator" / "scripts"
