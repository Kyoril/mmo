# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Pictures for the agent and the review packet: asset contact sheets, asset views, site previews.

All images are north-aware: views name their compass direction (north = -Z, east = +X).
"""

from __future__ import annotations

import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from .assets import AssetInfo, GeometryCache
from .geometry import trs_matrix
from .materials import TextureCache
from .meshrender import Camera, DrawMesh, render

_LABEL = (230, 230, 230)
_MAX_WMO_DEPTH = 2


def _font(size: int):
    return ImageFont.load_default(size=size)


def _transform(points: np.ndarray, matrix: np.ndarray) -> np.ndarray:
    return points @ matrix[:3, :3].T + matrix[:3, 3]


def asset_meshes(rel: str, catalog: dict[str, AssetInfo], geometry: GeometryCache, textures: TextureCache,
                 matrix: np.ndarray, object_id: int = 0, _depth: int = 0) -> list[DrawMesh]:
    """World-space draw meshes of one placed asset (a mesh, or a WMO's group meshes)."""
    info = catalog.get(rel)
    if info is None or info.error:
        return []
    if info.kind == "wmo":
        model = geometry.world_model(rel)
        if model is None or _depth >= _MAX_WMO_DEPTH:
            return []
        out = []
        for ref in model.mesh_refs:
            if ref.visible:
                child = matrix @ trs_matrix(ref.position, ref.rotation, ref.scale)
                out += asset_meshes(ref.mesh.replace("\\", "/"), catalog, geometry, textures, child, object_id, _depth + 1)
        return out
    mesh = geometry.mesh(rel)
    if mesh is None:
        return []
    return [DrawMesh(_transform(sub.positions.astype(float), matrix), sub.indices.reshape(-1, 3), uvs=sub.uvs,
                     texture=textures.for_material(sub.material), object_id=object_id)
            for sub in mesh.submeshes if len(sub.indices)]


def _framing(info: AssetInfo) -> tuple[np.ndarray, float]:
    lo, hi = np.asarray(info.bounds_min, float), np.asarray(info.bounds_max, float)
    return (lo + hi) / 2.0, max(float(np.linalg.norm(hi - lo)) / 2.0, 0.25)


def _thumbnail(rel, catalog, geometry, textures, tile: int) -> np.ndarray:
    info = catalog[rel]
    centre, radius = _framing(info)
    direction = np.array([1.0, 0.55, 1.0]) / np.linalg.norm([1.0, 0.55, 1.0])   # from the south-east, above
    camera = Camera(eye=tuple(centre + direction * radius * 2.8), target=tuple(centre), width=tile, height=tile)
    rgb, _ = render(asset_meshes(rel, catalog, geometry, textures, np.eye(4)), camera)
    return rgb


def contact_sheet(rels: list[str], catalog, geometry, textures, tile: int = 128, columns: int = 8,
                  title: str = "") -> Image.Image:
    """One tile per asset: 3/4 view from the south-east, file name, size in metres, C = has collision."""
    label_h, header = 30, 24 if title else 0
    rows = max(1, math.ceil(len(rels) / columns))
    sheet = Image.new("RGB", (columns * tile, header + rows * (tile + label_h)), (18, 18, 18))
    draw = ImageDraw.Draw(sheet)
    if title:
        draw.text((6, 4), title, fill=_LABEL, font=_font(14))
    for index, rel in enumerate(rels):
        x, y = (index % columns) * tile, header + (index // columns) * (tile + label_h)
        info = catalog[rel]
        if not info.error:
            sheet.paste(Image.fromarray(_thumbnail(rel, catalog, geometry, textures, tile)), (x, y))
        name = Path(rel).stem
        draw.text((x + 3, y + tile + 2), name[:22], fill=_LABEL, font=_font(11))
        extra = "unreadable" if info.error else f"{info.size:.1f} m{'  C' if info.has_collision else ''}"
        draw.text((x + 3, y + tile + 15), extra, fill=(160, 160, 160), font=_font(11))
    return sheet


def asset_views(rel: str, catalog, geometry, textures, size: int = 320) -> Image.Image:
    """Front (looking north), side (looking west) and top (north up) orthographic views with a 1 m grid."""
    info = catalog[rel]
    centre, radius = _framing(info)
    extent = max(info.size, 0.5) * 1.25
    views = [("front (looking north)", np.array([0.0, 0.0, 1.0]), (0.0, 1.0, 0.0)),
             ("side (looking west)", np.array([1.0, 0.0, 0.0]), (0.0, 1.0, 0.0)),
             ("top (north up)", np.array([0.0, 1.0, 0.0]), (0.0, 0.0, -1.0))]
    image = Image.new("RGB", (size * 3, size + 22), (18, 18, 18))
    draw = ImageDraw.Draw(image)
    meshes = asset_meshes(rel, catalog, geometry, textures, np.eye(4))
    for index, (label, direction, up) in enumerate(views):
        camera = Camera(eye=tuple(centre + direction * radius * 4.0), target=tuple(centre), width=size, height=size,
                        ortho_height=extent, up=up)
        rgb, ids = render(meshes, camera)
        scale = size / extent
        horizontal = 2 if index == 1 else 0                                 # side view: horizontal axis is Z
        vertical = 2 if index == 2 else 1                                   # top view: vertical axis is Z
        grid = np.zeros(ids.shape, bool)
        for axis, along_rows in ((horizontal, False), (vertical, True)):
            lo = centre[axis] - extent / 2
            for line in range(int(math.floor(lo)), int(math.ceil(lo + extent)) + 1):
                pixel = int(round((line - lo) * scale))
                if 0 <= pixel < size:
                    if along_rows:
                        grid[size - 1 - pixel if axis == 1 else pixel, :] = True
                    else:
                        grid[:, pixel if index != 1 else size - 1 - pixel] = True
        rgb[grid & (ids < 0)] = (48, 48, 48)
        image.paste(Image.fromarray(rgb), (index * size, 22))
        draw.text((index * size + 6, 4), label, fill=_LABEL, font=_font(12))
    return image


def write_contact_sheets(catalog: dict[str, AssetInfo], geometry: GeometryCache, textures: TextureCache,
                         out_dir: Path) -> list[Path]:
    """One sheet per model folder: generated/world/assets/sheets/<folder with _>.png."""
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    folders: dict[str, list[str]] = {}
    for rel in sorted(catalog):
        folders.setdefault(rel.rsplit("/", 1)[0], []).append(rel)
    written = []
    for folder, rels in folders.items():
        path = out_dir / (folder.replace("/", "_") + ".png")
        contact_sheet(rels, catalog, geometry, textures, title=folder).save(path)
        written.append(path)
    return written
