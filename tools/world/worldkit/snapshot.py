# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Per-map world model: terrain grids at cell resolution plus placed entities, cached as npz.

The cache lives in generated/world/<Directory>/ (gitignored). It is keyed by a fingerprint of
every source file's path, size and mtime, so editing a page or moving a prop rebuilds it.
"""

from __future__ import annotations

import dataclasses
import hashlib
import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from .constants import CELL_SIZE, CELLS_PER_TILE, INNER_PER_PAGE, PAGE_SIZE, PIXELS_PER_PAGE, TILES_PER_PAGE, page_origin
from .entities import load_entities
from .formats.hwld import WorldHeader, parse_hwld
from .formats.tile import parse_tile
from .formats.wobj import WorldEntity
from .paths import REPO, cache_dir, entities_dir, hwld_path, terrain_dir
from .surface import max_cell_slopes

SNAPSHOT_VERSION = 1
_ARRAYS = ("page_slot", "page_versions", "outer", "inner", "height", "slope", "area", "water_depth", "hole", "layer", "material")
_CELL_PIXELS = np.rint((np.arange(INNER_PER_PAGE) + 0.5) * (PIXELS_PER_PAGE - 1) / INNER_PER_PAGE).astype(np.int64)


class NoTerrainError(RuntimeError):
    """The world has no terrain pages (e.g. a pure world-model dungeon or an empty world)."""


@dataclass
class WorldSnapshot:
    directory: str
    page_min_x: int
    page_min_z: int
    pages_x: int
    pages_z: int
    page_slot: np.ndarray       # (pages_z, pages_x) int32 index into outer/inner, -1 = no page
    page_versions: np.ndarray   # (n,) uint8 .tile format version per slot
    outer: np.ndarray           # (n, 129, 129) float32 outer heights per page
    inner: np.ndarray           # (n, 128, 128) float32 inner heights per page
    height: np.ndarray          # (Z, X) float32 cell-centre height, NaN where there is no page
    slope: np.ndarray           # (Z, X) float32 steepest fan triangle, degrees
    area: np.ndarray            # (Z, X) uint32 area/zone id
    water_depth: np.ndarray     # (Z, X) float32 water depth at the cell centre, 0 = dry
    hole: np.ndarray            # (Z, X) bool terrain hole
    layer: np.ndarray           # (Z, X) uint8 dominant splat layer 0..3
    material: np.ndarray        # (Z, X) int16 index into materials, -1 = world default material
    materials: list[str]
    default_material: str
    entities: list[WorldEntity]

    @property
    def origin(self) -> tuple[float, float]:
        return page_origin(self.page_min_x, self.page_min_z)

    @property
    def extent(self) -> tuple[float, float, float, float]:
        x0, z0 = self.origin
        return x0, z0, x0 + self.pages_x * PAGE_SIZE, z0 + self.pages_z * PAGE_SIZE

    def cell_index(self, x: float, z: float) -> tuple[int, int] | None:
        x0, z0 = self.origin
        cx = int(np.floor((x - x0) / CELL_SIZE))
        cz = int(np.floor((z - z0) / CELL_SIZE))
        if 0 <= cx < self.height.shape[1] and 0 <= cz < self.height.shape[0]:
            return cz, cx
        return None

    def page_local(self, x: float, z: float) -> tuple[int, float, float] | None:
        x0, z0 = self.origin
        px = int(np.floor((x - x0) / PAGE_SIZE))
        pz = int(np.floor((z - z0) / PAGE_SIZE))
        if not (0 <= px < self.pages_x and 0 <= pz < self.pages_z):
            return None
        slot = int(self.page_slot[pz, px])
        if slot < 0:
            return None
        return slot, (x - x0) - px * PAGE_SIZE, (z - z0) - pz * PAGE_SIZE

    def material_name(self, index: int) -> str:
        return self.materials[index] if index >= 0 else self.default_material


def dominant_layers(layers: np.ndarray) -> np.ndarray:
    """(128, 128) uint8: index (0..3) of the heaviest splat layer at each cell's centre pixel."""
    pixels = layers[np.ix_(_CELL_PIXELS, _CELL_PIXELS)]
    weights = np.stack([(pixels >> np.uint32(8 * i)) & np.uint32(0xFF) for i in range(4)], axis=-1)
    return weights.argmax(axis=-1).astype(np.uint8)


def _expand_tiles(values: np.ndarray) -> np.ndarray:
    return np.repeat(np.repeat(values, CELLS_PER_TILE, axis=0), CELLS_PER_TILE, axis=1)


def _page_files(directory: str, repo: Path) -> list[tuple[Path, int, int]]:
    files = []
    for path in sorted(terrain_dir(directory, repo).glob("*.tile")):
        page_x, page_z = (int(part) for part in path.stem.split("_", 1))
        files.append((path, page_x, page_z))
    return files


def build_snapshot(directory: str, repo: Path = REPO) -> WorldSnapshot:
    files = _page_files(directory, repo)
    if not files:
        raise NoTerrainError(f"world '{directory}' has no terrain pages in {terrain_dir(directory, repo)}")

    min_x = min(f[1] for f in files)
    min_z = min(f[2] for f in files)
    pages_x = max(f[1] for f in files) - min_x + 1
    pages_z = max(f[2] for f in files) - min_z + 1
    rows, cols = pages_z * INNER_PER_PAGE, pages_x * INNER_PER_PAGE

    page_slot = np.full((pages_z, pages_x), -1, np.int32)
    page_versions = np.zeros(len(files), np.uint8)
    outer = np.zeros((len(files), INNER_PER_PAGE + 1, INNER_PER_PAGE + 1), np.float32)
    inner = np.zeros((len(files), INNER_PER_PAGE, INNER_PER_PAGE), np.float32)
    height = np.full((rows, cols), np.nan, np.float32)
    slope = np.full((rows, cols), np.nan, np.float32)
    area = np.zeros((rows, cols), np.uint32)
    water_depth = np.zeros((rows, cols), np.float32)
    hole = np.zeros((rows, cols), bool)
    layer = np.zeros((rows, cols), np.uint8)
    material = np.full((rows, cols), -1, np.int16)
    materials: list[str] = []
    material_index: dict[str, int] = {}

    hwld = hwld_path(directory, repo)
    header = parse_hwld(hwld) if hwld.is_file() else WorldHeader(version=0)

    for slot, (path, page_x, page_z) in enumerate(files):
        page = parse_tile(path)
        page_slot[page_z - min_z, page_x - min_x] = slot
        page_versions[slot] = page.version
        outer[slot] = page.outer
        inner[slot] = page.inner

        r0 = (page_z - min_z) * INNER_PER_PAGE
        c0 = (page_x - min_x) * INNER_PER_PAGE
        cells = (slice(r0, r0 + INNER_PER_PAGE), slice(c0, c0 + INNER_PER_PAGE))
        height[cells] = page.inner
        slope[cells] = max_cell_slopes(page.outer, page.inner)
        area[cells] = _expand_tiles(page.areas)
        hole[cells] = page.hole_cells()
        wh = page.water_heights
        surface = (wh[:-1, :-1] + wh[:-1, 1:] + wh[1:, :-1] + wh[1:, 1:]) * 0.25
        water_depth[cells] = np.where(page.water_cells(), np.maximum(surface - page.inner, 0.0), 0.0)
        layer[cells] = dominant_layers(page.layers)

        tile_material = np.full((TILES_PER_PAGE, TILES_PER_PAGE), -1, np.int16)
        for tile_index, name in enumerate(page.materials):
            if name:
                if name not in material_index:
                    material_index[name] = len(materials)
                    materials.append(name)
                tile_material[tile_index // TILES_PER_PAGE, tile_index % TILES_PER_PAGE] = material_index[name]
        material[cells] = _expand_tiles(tile_material)

    return WorldSnapshot(directory, min_x, min_z, pages_x, pages_z, page_slot, page_versions, outer, inner,
                         height, slope, area, water_depth, hole, layer, material, materials, header.default_material,
                         load_entities(directory, repo))


def _fingerprint(directory: str, repo: Path) -> str:
    digest = hashlib.sha1(f"worldkit-snapshot-v{SNAPSHOT_VERSION}".encode())
    sources = sorted(terrain_dir(directory, repo).glob("*.tile")) + sorted(entities_dir(directory, repo).glob("*/*.wobj"))
    hwld = hwld_path(directory, repo)
    if hwld.is_file():
        sources.append(hwld)
    for path in sources:
        stat = path.stat()
        digest.update(f"{path.relative_to(repo).as_posix()}|{stat.st_size}|{stat.st_mtime_ns}\n".encode())
    return digest.hexdigest()


def _save(snapshot: WorldSnapshot, fingerprint: str, repo: Path) -> None:
    folder = cache_dir(snapshot.directory, repo)
    folder.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(folder / "snapshot.npz", **{name: getattr(snapshot, name) for name in _ARRAYS})
    meta = {
        "version": SNAPSHOT_VERSION,
        "fingerprint": fingerprint,
        "directory": snapshot.directory,
        "page_min_x": snapshot.page_min_x,
        "page_min_z": snapshot.page_min_z,
        "pages_x": snapshot.pages_x,
        "pages_z": snapshot.pages_z,
        "materials": snapshot.materials,
        "default_material": snapshot.default_material,
        "entities": [dataclasses.asdict(e) for e in snapshot.entities],
    }
    (folder / "snapshot.json").write_text(json.dumps(meta), encoding="utf-8")


def _entity_from_dict(data: dict) -> WorldEntity:
    return WorldEntity(
        data["unique_id"], data["kind"], data["asset"], tuple(data["position"]), tuple(data["rotation"]),
        tuple(data["scale"]), data["name"], data["category"],
        tuple((int(i), str(m)) for i, m in data["material_overrides"]), data["path"])


def _load_cached(directory: str, repo: Path, fingerprint: str) -> WorldSnapshot | None:
    folder = cache_dir(directory, repo)
    meta_path = folder / "snapshot.json"
    npz_path = folder / "snapshot.npz"
    if not meta_path.is_file() or not npz_path.is_file():
        return None
    try:
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return None
    if meta.get("version") != SNAPSHOT_VERSION or meta.get("fingerprint") != fingerprint:
        return None
    with np.load(npz_path) as arrays:
        loaded = {name: arrays[name] for name in _ARRAYS}
    return WorldSnapshot(directory, meta["page_min_x"], meta["page_min_z"], meta["pages_x"], meta["pages_z"],
                         materials=meta["materials"], default_material=meta["default_material"],
                         entities=[_entity_from_dict(e) for e in meta["entities"]], **loaded)


def load_snapshot(directory: str, repo: Path = REPO, use_cache: bool = True) -> WorldSnapshot:
    """Returns the snapshot, rebuilding and re-caching it when any source file changed."""
    fingerprint = _fingerprint(directory, repo)
    if use_cache:
        cached = _load_cached(directory, repo, fingerprint)
        if cached is not None:
            return cached
    snapshot = build_snapshot(directory, repo)
    if use_cache:
        _save(snapshot, fingerprint, repo)
    return snapshot
