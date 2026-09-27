# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Height and slope of the rendered terrain surface.

A cell is four triangles fanned around its stored inner vertex (src/shared/terrain/terrain_raycast.h).
Reading the inner vertex instead of averaging the corners is mandatory: inner vertices are sculpted
independently, and an averaged surface is one the renderer never draws.
"""

from __future__ import annotations

import math

import numpy as np

from .constants import CELL_SIZE, INNER_PER_PAGE

_CENTRE = (0.5, 0.5)
# Triangle name -> its two cell-corner vertices in (u, v); the third vertex is the centre.
_TRIANGLES = {
    "bottom": ((0, 0), (1, 0)),
    "right": ((1, 0), (1, 1)),
    "top": ((1, 1), (0, 1)),
    "left": ((0, 1), (0, 0)),
}
_ORDER = ("bottom", "right", "top", "left")
# Plane h = a + b*u + c*v through the triangle's vertices: (a, b, c) = INV @ (h0, h1, h_centre).
_INV = {
    name: np.linalg.inv(np.array([[1.0, p0[0], p0[1]], [1.0, p1[0], p1[1]], [1.0, _CENTRE[0], _CENTRE[1]]]))
    for name, (p0, p1) in _TRIANGLES.items()
}


def _triangle_of(u: float, v: float) -> str:
    distances = (v, 1.0 - u, 1.0 - v, u)
    return _ORDER[distances.index(min(distances))]


def _slope_degrees(b, c):
    return np.degrees(np.arctan(np.hypot(b, c) / CELL_SIZE))


def sample(outer: np.ndarray, inner: np.ndarray, lx: float, lz: float) -> tuple[float, float]:
    """Returns (height, slope in degrees) at page-local metres (lx, lz), clamped to the page."""
    gx = min(max(lx / CELL_SIZE, 0.0), float(INNER_PER_PAGE))
    gz = min(max(lz / CELL_SIZE, 0.0), float(INNER_PER_PAGE))
    cx = min(int(math.floor(gx)), INNER_PER_PAGE - 1)
    cz = min(int(math.floor(gz)), INNER_PER_PAGE - 1)
    u = gx - cx
    v = gz - cz

    name = _triangle_of(u, v)
    p0, p1 = _TRIANGLES[name]
    heights = np.array([
        outer[cz + p0[1], cx + p0[0]],
        outer[cz + p1[1], cx + p1[0]],
        inner[cz, cx],
    ], dtype=np.float64)
    a, b, c = _INV[name] @ heights
    return float(a + b * u + c * v), float(_slope_degrees(b, c))


def max_cell_slopes(outer: np.ndarray, inner: np.ndarray) -> np.ndarray:
    """(128, 128) float32: steepest of the four fan triangles of every cell, in degrees."""
    corners = {
        (0, 0): outer[:-1, :-1].astype(np.float64),
        (1, 0): outer[:-1, 1:].astype(np.float64),
        (0, 1): outer[1:, :-1].astype(np.float64),
        (1, 1): outer[1:, 1:].astype(np.float64),
    }
    centre = inner.astype(np.float64)
    best = np.zeros(inner.shape, np.float64)
    for name, (p0, p1) in _TRIANGLES.items():
        inv = _INV[name]
        h0 = corners[p0]
        h1 = corners[p1]
        b = inv[1, 0] * h0 + inv[1, 1] * h1 + inv[1, 2] * centre
        c = inv[2, 0] * h0 + inv[2, 1] * h1 + inv[2, 2] * centre
        best = np.maximum(best, _slope_degrees(b, c))
    return best.astype(np.float32)
