# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Rotations for placed props. Quaternions are (w, x, y, z), like .wobj and .hfol files.

Yaw turns about +Y; yaw 0 faces +Z (south) and yaw 90 faces +X (east). Tilt is pitch about X and roll
about Z, applied after the yaw: R = Rx(pitch) . Rz(roll) . Ry(yaw).
"""

from __future__ import annotations

import math

import numpy as np

Quat = tuple[float, float, float, float]


def quat_mul(a: Quat, b: Quat) -> Quat:
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return (aw * bw - ax * bx - ay * by - az * bz,
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw)


def quat_axis_angle(axis, degrees: float) -> Quat:
    half = math.radians(degrees) * 0.5
    x, y, z = axis
    s = math.sin(half)
    return (math.cos(half), x * s, y * s, z * s)


def quat_from_yaw_tilt(yaw: float, pitch: float = 0.0, roll: float = 0.0) -> Quat:
    tilt = quat_mul(quat_axis_angle((1.0, 0.0, 0.0), pitch), quat_axis_angle((0.0, 0.0, 1.0), roll))
    return quat_mul(tilt, quat_axis_angle((0.0, 1.0, 0.0), yaw))


def quat_to_matrix(q) -> np.ndarray:
    w, x, y, z = q
    n = math.sqrt(w * w + x * x + y * y + z * z) or 1.0
    w, x, y, z = w / n, x / n, y / n, z / n
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])


def yaw_of_quat(q) -> float:
    """Heading in degrees of the rotated +Z axis (0 = +Z, 90 = +X)."""
    forward = quat_to_matrix(q) @ np.array([0.0, 0.0, 1.0])
    return math.degrees(math.atan2(forward[0], forward[2]))


def trs_matrix(position, rotation, scale) -> np.ndarray:
    m = np.eye(4)
    m[:3, :3] = quat_to_matrix(rotation) * np.asarray(scale, float)[None, :]
    m[:3, 3] = position
    return m
