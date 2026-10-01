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
from .geometry import quat_from_yaw_tilt, trs_matrix
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


_KIND_COLOURS = {"grass": (88, 130, 62), "forest_floor": (70, 105, 52), "path": (150, 120, 80), "road": (160, 135, 95),
                 "dirt": (130, 100, 70), "mud": (100, 80, 60), "rock": (125, 125, 120), "sand": (194, 178, 128),
                 "snow": (235, 235, 240)}
_WATER = (60, 110, 190)
_FIRST_ITEM_ID = 1000
_TERRAIN_STEP = 2.0


def _compass(dx: float, dz: float) -> str:
    angle = (math.degrees(math.atan2(dx, -dz)) + 360.0) % 360.0   # 0 = north (-Z), 90 = east (+X)
    return ["north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west"][int((angle + 22.5) // 45) % 8]


def terrain_meshes(query, cx: float, cz: float, radius: float) -> list[DrawMesh]:
    """Ground (coloured by terrain kind, holes left open) and water surfaces around a point."""
    steps = int(math.ceil(radius / _TERRAIN_STEP))
    xs = cx + (np.arange(-steps, steps + 1)) * _TERRAIN_STEP
    zs = cz + (np.arange(-steps, steps + 1)) * _TERRAIN_STEP
    n = len(xs)
    heights = np.full((n, n), np.nan)
    for j, z in enumerate(zs):
        for i, x in enumerate(xs):
            h = query.height_at(float(x), float(z))
            if h is not None:
                heights[j, i] = h
    positions = np.stack([np.repeat(xs[None, :], n, 0), np.nan_to_num(heights), np.repeat(zs[:, None], n, 1)], -1).reshape(-1, 3)
    tris, colours, water_quads = [], [], []
    for j in range(n - 1):
        for i in range(n - 1):
            if np.isnan(heights[j:j + 2, i:i + 2]).any():
                continue
            mx, mz = float(xs[i] + _TERRAIN_STEP / 2), float(zs[j] + _TERRAIN_STEP / 2)
            if query.hole_at(mx, mz):
                continue
            a, b, c, d = j * n + i, j * n + i + 1, (j + 1) * n + i, (j + 1) * n + i + 1
            colour = _KIND_COLOURS.get(query.terrain_kind_at(mx, mz) or "grass", _KIND_COLOURS["grass"])
            tris += [(a, b, d), (a, d, c)]
            colours += [colour, colour]
            depth = query.water_depth_at(mx, mz)
            if depth > 0.05:
                level = float(np.mean(heights[j:j + 2, i:i + 2])) + depth
                water_quads.append((float(xs[i]), float(zs[j]), level))
    meshes = [DrawMesh(positions, np.asarray(tris, np.int64).reshape(-1, 3), tri_colors=np.asarray(colours, np.float32),
                       object_id=-2)] if tris else []
    if water_quads:
        points, faces = [], []
        for x, z, level in water_quads:
            base = len(points)
            points += [(x, level, z), (x + _TERRAIN_STEP, level, z), (x, level, z + _TERRAIN_STEP),
                       (x + _TERRAIN_STEP, level, z + _TERRAIN_STEP)]
            faces += [(base, base + 1, base + 3), (base, base + 3, base + 2)]
        meshes.append(DrawMesh(np.asarray(points, float), np.asarray(faces, np.int64), color=_WATER, object_id=-3))
    return meshes


def item_matrix(item: dict) -> np.ndarray:
    pitch, roll = item.get("tilt", [0.0, 0.0])
    scale = float(item["scale"])
    return trs_matrix(item["position"], quat_from_yaw_tilt(float(item["yaw"]), float(pitch), float(roll)), (scale, scale, scale))


def _item_id(item: dict) -> int | None:
    value = item.get("unique_id")
    return int(value, 16) if isinstance(value, str) else value


def site_previews(query, catalog, geometry, textures, center, radius: float, entities, instances, items,
                  count: int = 3, size=(960, 640)) -> list[tuple[str, Image.Image]]:
    """Perspective views around a site: terrain, existing props and trees, and the draft items outlined."""
    from .meshrender import outline
    cx, cz = center
    ground = query.height_at(cx, cz) or 0.0
    reach = radius * 1.4
    skip = {_item_id(i) for i in items if _item_id(i) is not None}
    meshes = terrain_meshes(query, cx, cz, reach)
    for entity in entities:
        if entity.unique_id in skip or math.hypot(entity.position[0] - cx, entity.position[2] - cz) > reach:
            continue
        meshes += asset_meshes(entity.asset, catalog, geometry, textures,
                               trs_matrix(entity.position, entity.rotation, entity.scale), object_id=-4)
    for inst in instances:
        if inst.unique_id in skip or math.hypot(inst.position[0] - cx, inst.position[2] - cz) > reach:
            continue
        meshes += asset_meshes(inst.mesh, catalog, geometry, textures, trs_matrix(inst.position, inst.rotation, inst.scale),
                               object_id=-5)
    highlight = set()
    for index, item in enumerate(items):
        meshes += asset_meshes(item["asset"], catalog, geometry, textures, item_matrix(item), object_id=_FIRST_ITEM_ID + index)
        highlight.add(_FIRST_ITEM_ID + index)
    out = []
    for k in range(count):
        azimuth = math.radians(30.0 + k * 360.0 / count)
        eye = (cx + math.sin(azimuth) * radius * 1.7, ground + radius * 0.9, cz + math.cos(azimuth) * radius * 1.7)
        camera = Camera(eye=eye, target=(cx, ground + 2.0, cz), width=size[0], height=size[1], fov_deg=50.0)
        rgb, ids = render(meshes, camera, background=(150, 180, 215))
        image = Image.fromarray(outline(rgb, ids, highlight))
        label = f"preview {k + 1} - looking {_compass(cx - eye[0], cz - eye[2])}"
        ImageDraw.Draw(image).text((8, 6), label, fill=(20, 20, 20), font=_font(14))
        out.append((label, image))
    return out
