# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Catalog of placeable assets (.hmsh, .hwmo under data/client/Models) for dressing.

Cached in generated/world/assets/catalog.json, keyed by each file's size and mtime plus a hash of the
parser code, so only changed assets are re-parsed. A WMO has collision when any mesh it references does.
"""

from __future__ import annotations

import dataclasses
import hashlib
import json
import math
import os
from dataclasses import dataclass, replace
from pathlib import Path

from .formats.chunks import FormatError
from .formats.hmsh import MeshData, parse_hmsh
from .formats.hwmo import WorldModelData, parse_hwmo
from .materials import resolve_base_texture
from .paths import REPO, assets_cache_dir, client_root
from .tags import NEVER_PICK, TagRules

CATALOG_VERSION = 1
SIZE_SMALL = 1.5
SIZE_MEDIUM = 4.0
_SUFFIXES = (".hmsh", ".hwmo")


@dataclass(frozen=True)
class AssetInfo:
    path: str                                   # relative to data/client, '/' separators
    kind: str                                   # "mesh" | "wmo"
    bounds_min: tuple[float, float, float]
    bounds_max: tuple[float, float, float]
    has_collision: bool
    submeshes: int
    materials: tuple[str, ...]
    textures: tuple[str | None, ...]
    references: tuple[str, ...]                 # meshes a WMO places
    error: str | None = None

    @property
    def size(self) -> float:
        return max(self.bounds_max[i] - self.bounds_min[i] for i in range(3))

    @property
    def size_class(self) -> str:
        return "small" if self.size < SIZE_SMALL else "medium" if self.size < SIZE_MEDIUM else "large"

    @property
    def height(self) -> float:
        return self.bounds_max[1] - self.bounds_min[1]

    @property
    def base_offset(self) -> float:
        """How far the origin sits above the lowest vertex."""
        return -self.bounds_min[1]

    @property
    def footprint(self) -> tuple[float, float, float, float]:
        """Model-space rectangle under the asset: (min_x, min_z, max_x, max_z)."""
        return (self.bounds_min[0], self.bounds_min[2], self.bounds_max[0], self.bounds_max[2])

    @property
    def radius(self) -> float:
        x0, z0, x1, z1 = self.footprint
        return max(math.hypot(x, z) for x in (x0, x1) for z in (z0, z1))


def _code_digest() -> str:
    digest = hashlib.sha1(f"assets-v{CATALOG_VERSION}".encode())
    package = Path(__file__).parent
    for path in sorted([package / "assets.py", package / "materials.py", package / "formats" / "hmsh.py",
                        package / "formats" / "hwmo.py", package / "formats" / "chunks.py"]):
        digest.update(path.read_bytes())
    return digest.hexdigest()


def _describe(path: Path, rel: str, client: Path) -> AssetInfo:
    kind = "mesh" if path.suffix.lower() == ".hmsh" else "wmo"
    try:
        if kind == "mesh":
            mesh: MeshData = parse_hmsh(path)
            lo, hi = mesh.bounds
            materials = tuple(s.material for s in mesh.submeshes)
            textures = tuple(resolve_base_texture(m, client) for m in materials)
            return AssetInfo(rel, kind, tuple(float(v) for v in lo), tuple(float(v) for v in hi), mesh.has_collision,
                             len(mesh.submeshes), materials, textures, ())
        model: WorldModelData = parse_hwmo(path)
        references = tuple(sorted({r.mesh.replace("\\", "/") for r in model.mesh_refs}))
        return AssetInfo(rel, kind, tuple(model.bounds_min), tuple(model.bounds_max), False, 0, (), (), references)
    except (FormatError, OSError, ValueError) as exc:  # ValueError covers UnicodeDecodeError: one bad asset never aborts the catalog
        return AssetInfo(rel, kind, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), False, 0, (), (), (), error=str(exc))


def _from_dict(data: dict) -> AssetInfo:
    return AssetInfo(data["path"], data["kind"], tuple(data["bounds_min"]), tuple(data["bounds_max"]),
                     data["has_collision"], data["submeshes"], tuple(data["materials"]), tuple(data["textures"]),
                     tuple(data["references"]), data.get("error"))


def build_catalog(repo: Path = REPO, rebuild: bool = False) -> dict[str, AssetInfo]:
    client = client_root(repo)
    cache_file = assets_cache_dir(repo) / "catalog.json"
    digest = _code_digest()
    cached: dict = {}
    if cache_file.is_file() and not rebuild:
        try:
            doc = json.loads(cache_file.read_text(encoding="utf-8"))
            if doc.get("digest") == digest:
                cached = doc.get("assets", {})
        except ValueError:  # JSONDecodeError and UnicodeDecodeError: a damaged cache is just rebuilt
            cached = {}
    entries: dict[str, AssetInfo] = {}
    stamps: dict[str, str] = {}
    models = client / "Models"
    for path in sorted(models.rglob("*")) if models.is_dir() else []:
        if path.suffix.lower() not in _SUFFIXES or not path.is_file():
            continue
        rel = path.relative_to(client).as_posix()
        stat = path.stat()
        stamp = f"{stat.st_size}:{stat.st_mtime_ns}"
        hit = cached.get(rel)
        entries[rel] = _from_dict(hit["info"]) if hit and hit.get("stamp") == stamp else _describe(path, rel, client)
        stamps[rel] = stamp
    for rel, info in list(entries.items()):
        if info.kind == "wmo" and not info.error:
            collides = any(entries.get(ref) is not None and entries[ref].has_collision for ref in info.references)
            entries[rel] = replace(info, has_collision=collides)
    cache_file.parent.mkdir(parents=True, exist_ok=True)
    payload = {rel: {"stamp": stamps[rel], "info": dataclasses.asdict(info)} for rel, info in entries.items()}
    temp = cache_file.with_name(cache_file.name + ".tmp")
    temp.write_text(json.dumps({"version": CATALOG_VERSION, "digest": digest, "assets": payload}), encoding="utf-8")
    os.replace(temp, cache_file)  # atomic: a crash mid-write never leaves a half-written cache
    return entries


def select_assets(catalog: dict[str, AssetInfo], rules: TagRules, tags=(), exclude=(), size: str | None = None,
                  allow=None) -> list[str]:
    """Usable assets matching a template role's query. An allow-list bypasses tags and size."""
    wanted, unwanted = set(tags), set(exclude)
    out = []
    for rel, info in sorted(catalog.items()):
        if info.error:
            continue
        if allow is not None:
            if rel in allow:
                out.append(rel)
            continue
        have = rules.for_asset(rel).tags
        if not wanted <= have or have & unwanted:
            continue
        if have & NEVER_PICK and not wanted & NEVER_PICK:
            continue
        if size and info.size_class != size:
            continue
        out.append(rel)
    return out


class GeometryCache:
    """Parsed meshes and world models by catalog path, kept for the process lifetime."""

    def __init__(self, repo: Path = REPO):
        self._client = client_root(repo)
        self._meshes: dict[str, MeshData | None] = {}
        self._models: dict[str, WorldModelData | None] = {}

    def mesh(self, rel: str) -> MeshData | None:
        if rel not in self._meshes:
            try:
                self._meshes[rel] = parse_hmsh(self._client / rel)
            except (FormatError, OSError):
                self._meshes[rel] = None
        return self._meshes[rel]

    def world_model(self, rel: str) -> WorldModelData | None:
        if rel not in self._models:
            try:
                self._models[rel] = parse_hwmo(self._client / rel)
            except (FormatError, OSError):
                self._models[rel] = None
        return self._models[rel]
