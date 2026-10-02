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


def client_root(repo: Path = REPO) -> Path:
    return repo / "data" / "client"


def foliage_dir(directory: str, repo: Path = REPO) -> Path:
    return world_root(directory, repo) / "Foliage"


def world_tools(repo: Path = REPO) -> Path:
    return repo / "tools" / "world"


def templates_dir(repo: Path = REPO) -> Path:
    return world_tools(repo) / "templates"


def assets_cache_dir(repo: Path = REPO) -> Path:
    return repo / "generated" / "world" / "assets"


def passes_dir(repo: Path = REPO) -> Path:
    """Drafts, previews and scratch output of dressing passes (gitignored)."""
    return repo / "generated" / "world" / "passes"


def manifests_dir(repo: Path = REPO) -> Path:
    """Tracked manifests of applied dressing passes."""
    return repo / "data" / "world" / "passes"


def review_dir(repo: Path = REPO) -> Path:
    return repo / "generated" / "world" / "review"


def material_scripts(repo: Path = REPO) -> Path:
    """The material skill's scripts (material_tool.py, htex_tool.py), reused for textures."""
    return repo / ".agents" / "skills" / "mmo-material-editor" / "scripts"
