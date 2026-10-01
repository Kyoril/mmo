# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Foliage of a world: one .hfol per page in Worlds/<W>/<W>/Foliage/<pageIndex>.hfol."""

from __future__ import annotations

from pathlib import Path

from .formats.hfol import FoliageFile, FoliageInstance, load_hfol
from .paths import REPO, foliage_dir


def page_foliage_path(directory: str, page_index: int, repo: Path = REPO) -> Path:
    return foliage_dir(directory, repo) / f"{page_index}.hfol"


def load_world_foliage(directory: str, repo: Path = REPO) -> dict[int, FoliageFile]:
    folder = foliage_dir(directory, repo)
    if not folder.is_dir():
        return {}
    return {int(path.stem): load_hfol(path) for path in sorted(folder.glob("*.hfol")) if path.stem.isdigit()}


def all_instances(foliage: dict[int, FoliageFile]) -> list[FoliageInstance]:
    return [inst for ff in foliage.values() for inst in ff.instances]
