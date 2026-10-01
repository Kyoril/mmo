# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""A small numpy rasterizer for asset contact sheets and site previews.

Z-buffered, two-sided (no culling: robust to mirrored meshes), Lambert-shaded from the north-west
(north = -Z, west = -X), nearest-neighbour texturing with perspective-correct UVs. Deterministic: the
same meshes and camera always give the same pixels. Not engine-accurate; good enough to tell assets apart.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

NEAR = 0.05
LIGHT = np.array([-1.0, 1.6, -1.0]) / np.linalg.norm([-1.0, 1.6, -1.0])
AMBIENT = 0.35


@dataclass
class Camera:
    eye: tuple[float, float, float]
    target: tuple[float, float, float]
    width: int
    height: int
    fov_deg: float = 40.0
    ortho_height: float | None = None   # world units visible vertically; None = perspective
    up: tuple[float, float, float] = (0.0, 1.0, 0.0)


@dataclass
class DrawMesh:
    positions: np.ndarray                  # (n, 3) world space
    indices: np.ndarray                    # (m, 3) or flat (3m,) triangle list
    uvs: np.ndarray | None = None          # (n, 2)
    texture: np.ndarray | None = None      # (h, w, 4) uint8
    color: tuple[int, int, int] = (170, 170, 170)
    tri_colors: np.ndarray | None = None   # (m, 3) per-triangle colours (terrain)
    object_id: int = 0


def _basis(camera: Camera):
    eye = np.asarray(camera.eye, float)
    forward = np.asarray(camera.target, float) - eye
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, np.asarray(camera.up, float))
    right /= np.linalg.norm(right)
    return eye, forward, right, np.cross(right, forward)


def _project(points: np.ndarray, camera: Camera):
    eye, forward, right, up = _basis(camera)
    rel = points - eye
    xv, yv, depth = rel @ right, rel @ up, rel @ forward
    if camera.ortho_height:
        scale = camera.height / camera.ortho_height
        return camera.width / 2 + xv * scale, camera.height / 2 - yv * scale, depth, np.ones_like(depth)
    focal = (camera.height / 2) / math.tan(math.radians(camera.fov_deg) / 2)
    safe = np.where(depth > NEAR, depth, NEAR)
    return camera.width / 2 + xv * focal / safe, camera.height / 2 - yv * focal / safe, depth, 1.0 / safe


def render(meshes: list[DrawMesh], camera: Camera, background=(28, 28, 28)) -> tuple[np.ndarray, np.ndarray]:
    width, height = camera.width, camera.height
    rgb = np.empty((height, width, 3), np.float32)
    rgb[:] = background
    depth = np.full((height, width), np.inf, np.float64)
    ids = np.full((height, width), -1, np.int32)
    ortho = camera.ortho_height is not None
    for mesh in meshes:
        positions = np.asarray(mesh.positions, float)
        tris = np.asarray(mesh.indices, np.int64).reshape(-1, 3)
        if not len(tris):
            continue
        sx, sy, d, winv = _project(positions, camera)
        normals = np.cross(positions[tris[:, 1]] - positions[tris[:, 0]], positions[tris[:, 2]] - positions[tris[:, 0]])
        lengths = np.linalg.norm(normals, axis=1)
        shade = AMBIENT + (1.0 - AMBIENT) * np.abs(normals @ LIGHT) / np.where(lengths > 1e-12, lengths, 1.0)
        tex = mesh.texture
        for k, (a, b, c) in enumerate(tris):
            if not ortho and min(d[a], d[b], d[c]) <= NEAR:
                continue
            xs_t, ys_t = (sx[a], sx[b], sx[c]), (sy[a], sy[b], sy[c])
            x0, x1 = max(int(math.floor(min(xs_t))), 0), min(int(math.ceil(max(xs_t))), width - 1)
            y0, y1 = max(int(math.floor(min(ys_t))), 0), min(int(math.ceil(max(ys_t))), height - 1)
            if x0 > x1 or y0 > y1:
                continue
            area = (xs_t[1] - xs_t[0]) * (ys_t[2] - ys_t[0]) - (xs_t[2] - xs_t[0]) * (ys_t[1] - ys_t[0])
            if abs(area) < 1e-9:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            w0 = ((xs_t[1] - gx) * (ys_t[2] - gy) - (xs_t[2] - gx) * (ys_t[1] - gy)) / area
            w1 = ((xs_t[2] - gx) * (ys_t[0] - gy) - (xs_t[0] - gx) * (ys_t[2] - gy)) / area
            w2 = 1.0 - w0 - w1
            inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not inside.any():
                continue
            if ortho:
                z = w0 * d[a] + w1 * d[b] + w2 * d[c]
                iw = None
            else:
                iw = w0 * winv[a] + w1 * winv[b] + w2 * winv[c]
                z = 1.0 / np.where(iw > 0, iw, 1e-12)
            region = (slice(y0, y1 + 1), slice(x0, x1 + 1))
            mask = inside & (z < depth[region])
            if not mask.any():
                continue
            if tex is not None and mesh.uvs is not None:
                uv = np.asarray(mesh.uvs, float)
                if ortho:
                    u = w0 * uv[a, 0] + w1 * uv[b, 0] + w2 * uv[c, 0]
                    v = w0 * uv[a, 1] + w1 * uv[b, 1] + w2 * uv[c, 1]
                else:
                    u = (w0 * uv[a, 0] * winv[a] + w1 * uv[b, 0] * winv[b] + w2 * uv[c, 0] * winv[c]) / iw
                    v = (w0 * uv[a, 1] * winv[a] + w1 * uv[b, 1] * winv[b] + w2 * uv[c, 1] * winv[c]) / iw
                th, tw = tex.shape[:2]
                tx = np.clip((np.mod(u, 1.0) * tw).astype(np.int64), 0, tw - 1)
                ty = np.clip((np.mod(v, 1.0) * th).astype(np.int64), 0, th - 1)
                colour = tex[ty, tx, :3].astype(np.float32) * shade[k]
            else:
                base = mesh.tri_colors[k] if mesh.tri_colors is not None else mesh.color
                colour = np.broadcast_to(np.asarray(base, np.float32) * shade[k], mask.shape + (3,))
            depth[region][mask] = z[mask]
            rgb[region][mask] = colour[mask]
            ids[region][mask] = mesh.object_id
    return np.clip(rgb, 0, 255).astype(np.uint8), ids


def outline(rgb: np.ndarray, ids: np.ndarray, highlight: set[int], color=(255, 220, 60)) -> np.ndarray:
    """Draws a 1-pixel border around every object whose id is in `highlight`."""
    if not highlight:
        return rgb
    mask = np.isin(ids, list(highlight))
    edge = np.zeros_like(mask)
    edge[1:, :] |= mask[1:, :] & ~mask[:-1, :]
    edge[:-1, :] |= mask[:-1, :] & ~mask[1:, :]
    edge[:, 1:] |= mask[:, 1:] & ~mask[:, :-1]
    edge[:, :-1] |= mask[:, :-1] & ~mask[:, 1:]
    out = rgb.copy()
    out[edge] = color
    return out
