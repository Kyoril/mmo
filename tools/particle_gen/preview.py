# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Offline preview renderer for ``.hpar`` particle systems.

Runs the same simulation the engine runs (``EmitterInstance::UpdateSimulation`` /
``UpdateParticles``) on the CPU, then renders additive/alpha soft-blob sprites into a
contact sheet of frames so an effect can be judged without launching the client.

It is an *approximation of the look*, not a pixel-accurate render: particle materials are
reduced to "additive" vs "alpha" plus a radial falloff sprite, and there is no scene, no
depth sorting against geometry and no post-processing. It is accurate about the things that
actually go wrong when authoring — timing, extent, density, colour ramps, stretch length,
and whether the effect reads as the shape you intended.

Usage::

    python tools/particle_gen/preview.py data/client/Particles/LevelUp.hpar
    python tools/particle_gen/preview.py LevelUp.hpar --frames 8 --duration 2.5 --out sheet.png
    python tools/particle_gen/preview.py LevelUp.hpar --camera side --figure

``--figure`` draws a 1.8-unit humanoid reference silhouette at the origin so the effect can
be judged against character scale.
"""

from __future__ import annotations

import argparse
import math
import os
import random
import struct
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import hpar  # noqa: E402

DEFAULT_DATA_ROOT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "data", "client")


# =========================================================================================
# Curve evaluation (mirrors FloatCurve / ColorCurve in src/shared/graphics)
# =========================================================================================

def _auto_tangents(keys, get_value):
    """Recompute auto (mode 0) tangents exactly like ``FloatCurve::CalculateTangents``."""
    tangents = []
    for i, key in enumerate(keys):
        prev_key = keys[i - 1] if i > 0 else None
        next_key = keys[i + 1] if i < len(keys) - 1 else None
        if key.tangent_mode != 0:
            tangents.append((key.in_tangent, key.out_tangent))
            continue
        if prev_key is None and next_key is None:
            tangents.append((0.0, 0.0))
        elif prev_key is None:
            dt = next_key.time - key.time
            d = (get_value(next_key) - get_value(key)) / dt if dt > 0 else 0.0
            tangents.append((d, d))
        elif next_key is None:
            dt = key.time - prev_key.time
            d = (get_value(key) - get_value(prev_key)) / dt if dt > 0 else 0.0
            tangents.append((d, d))
        else:
            dt_prev = key.time - prev_key.time
            dt_next = next_key.time - key.time
            if dt_prev > 0 and dt_next > 0:
                m0 = (get_value(key) - get_value(prev_key)) / dt_prev
                m1 = (get_value(next_key) - get_value(key)) / dt_next
                t = (m0 + m1) * 0.5
                tangents.append((t, t))
            else:
                tangents.append((0.0, 0.0))
    return tangents


def _hermite(v0, v1, out_t0, in_t1, dt, t):
    if t < 0.001:
        return v0
    if t > 0.999:
        return v1
    m0 = out_t0 * dt
    m1 = in_t1 * dt
    t2 = t * t
    t3 = t2 * t
    return ((2 * t3 - 3 * t2 + 1) * v0 + (t3 - 2 * t2 + t) * m0 +
            (-2 * t3 + 3 * t2) * v1 + (t3 - t2) * m1)


class FloatCurveEval:
    def __init__(self, keys):
        self.keys = sorted(keys, key=lambda k: k.time)
        self.tangents = _auto_tangents(self.keys, lambda k: k.value)

    def __call__(self, time):
        keys = self.keys
        if not keys:
            return 0.0
        if len(keys) == 1 or time <= keys[0].time:
            return keys[0].value
        if time >= keys[-1].time:
            return keys[-1].value
        i = 0
        while i < len(keys) - 1 and keys[i + 1].time <= time:
            i += 1
        k0, k1 = keys[i], keys[i + 1]
        dt = k1.time - k0.time
        if dt <= 0:
            return k0.value
        return _hermite(k0.value, k1.value, self.tangents[i][1], self.tangents[i + 1][0],
                        dt, (time - k0.time) / dt)


class ColorCurveEval:
    def __init__(self, keys):
        self.keys = sorted(keys, key=lambda k: k.time)
        self.tangents = [_auto_tangents(self.keys, lambda k, c=c: k.color[c]) for c in range(4)]

    def __call__(self, time):
        keys = self.keys
        if not keys:
            return np.array([1.0, 1.0, 1.0, 1.0])
        if len(keys) == 1 or time <= keys[0].time:
            return np.array(keys[0].color, dtype=float)
        if time >= keys[-1].time:
            return np.array(keys[-1].color, dtype=float)
        i = 0
        while i < len(keys) - 1 and keys[i + 1].time <= time:
            i += 1
        k0, k1 = keys[i], keys[i + 1]
        dt = k1.time - k0.time
        if dt <= 0:
            return np.array(k0.color, dtype=float)
        t = (time - k0.time) / dt
        return np.array([
            _hermite(k0.color[c], k1.color[c], self.tangents[c][i][1], self.tangents[c][i + 1][0], dt, t)
            for c in range(4)
        ])


# =========================================================================================
# Simulation (mirrors EmitterInstance)
# =========================================================================================

class _Particle:
    __slots__ = ("pos", "vel", "age", "lifetime", "base_size", "size", "color", "rotation",
                 "angular_velocity", "phase")


def _curl_noise(p, t):
    """Cheap stand-in for the engine's CurlNoise: divergence-free-ish smooth field."""
    x, y, z = p
    return np.array([
        math.sin(y * 1.7 + t * 1.3) - math.sin(z * 1.1 - t * 0.7),
        math.sin(z * 1.9 - t * 1.1) - math.sin(x * 1.3 + t * 0.9),
        math.sin(x * 1.5 + t * 0.8) - math.sin(y * 1.7 - t * 1.2),
    ])


class EmitterSim:
    def __init__(self, params: hpar.Emitter, rng: random.Random):
        self.p = params
        self.rng = rng
        self.particles = []
        self.spawn_accumulator = 0.0
        self.age = 0.0
        self.cycle_time = 0.0
        self.noise_time = 0.0
        self.burst_fired = [False] * len(params.bursts)
        self.size_curve = FloatCurveEval(params.size_over_life)
        self.color_curve = ColorCurveEval(params.color_over_lifetime)

    def _rand(self, lo, hi):
        return lo if lo == hi else self.rng.uniform(lo, hi)

    def _rand_vec(self, lo, hi):
        return np.array([self._rand(lo[i], hi[i]) for i in range(3)])

    def _spawn_position(self):
        p = self.p
        if p.shape == hpar.SHAPE_SPHERE:
            while True:
                off = np.array([self.rng.uniform(-1, 1) for _ in range(3)])
                if off.dot(off) <= 1.0:
                    break
            n = np.linalg.norm(off)
            direction = off / n if n > 1e-3 else np.array([0.0, 1.0, 0.0])
            return off * p.shape_extents[0], direction
        if p.shape == hpar.SHAPE_BOX:
            half = np.array(p.shape_extents) * 0.5
            return np.array([self.rng.uniform(-half[i], half[i]) for i in range(3)]), np.array([0.0, 1.0, 0.0])
        if p.shape == hpar.SHAPE_CONE:
            height, base_radius = p.shape_extents[1], p.shape_extents[2]
            t = self.rng.random()
            theta = self.rng.uniform(0.0, 2.0 * math.pi)
            off = np.array([base_radius * t * math.cos(theta), height * t, base_radius * t * math.sin(theta)])
            n = np.linalg.norm(off)
            direction = off / n if n > 1e-3 else np.array([0.0, 1.0, 0.0])
            return off, direction
        return np.zeros(3), np.array([0.0, 1.0, 0.0])

    def _spawn_one(self):
        p = self.p
        if len(self.particles) >= p.max_particles:
            return
        pos, direction = self._spawn_position()
        speed = self._rand(p.min_start_speed, p.max_start_speed)
        vel = self._rand_vec(p.min_velocity, p.max_velocity) + direction * speed

        particle = _Particle()
        particle.pos = pos
        particle.vel = vel
        particle.lifetime = max(1e-4, self._rand(p.min_lifetime, p.max_lifetime))
        particle.age = 0.0
        particle.base_size = self._rand(p.min_start_size, p.max_start_size)
        particle.size = particle.base_size * self.size_curve(0.0)
        particle.rotation = self._rand(p.min_start_rotation, p.max_start_rotation)
        particle.angular_velocity = self._rand(p.min_angular_velocity, p.max_angular_velocity)
        particle.color = self.color_curve(0.0)
        particle.phase = self.rng.random()
        self.particles.append(particle)

    def step(self, dt):
        p = self.p
        if not p.enabled:
            return

        self.age += dt
        self.cycle_time += dt
        self.noise_time += dt

        if p.loop and p.duration > 0.0:
            while self.cycle_time >= p.duration:
                self.cycle_time -= p.duration
                self.burst_fired = [False] * len(p.bursts)

        past_delay = self.age >= p.start_delay
        cycle_for_burst = self.cycle_time - (0.0 if p.loop else p.start_delay)
        emitting = past_delay and not (not p.loop and self.age >= p.start_delay + p.duration)

        if emitting:
            if p.spawn_rate > 0.0:
                self.spawn_accumulator += dt * p.spawn_rate
                while self.spawn_accumulator >= 1.0 and len(self.particles) < p.max_particles:
                    self._spawn_one()
                    self.spawn_accumulator -= 1.0
            for i, burst in enumerate(p.bursts):
                if not self.burst_fired[i] and cycle_for_burst >= burst.time:
                    for _ in range(burst.count):
                        if len(self.particles) >= p.max_particles:
                            break
                        self._spawn_one()
                    self.burst_fired[i] = True

        gravity = np.array(p.gravity, dtype=float)
        attractor = np.array(p.attractor_position, dtype=float)
        cos_a = math.cos(p.orbital_speed * dt)
        sin_a = math.sin(p.orbital_speed * dt)

        alive = []
        for particle in self.particles:
            particle.age += dt
            if particle.age >= particle.lifetime:
                continue

            particle.vel = particle.vel + gravity * dt

            if abs(p.radial_acceleration) > 1e-6:
                radial = particle.pos.copy()
                n = np.linalg.norm(radial)
                if n > 1e-3:
                    particle.vel = particle.vel + (radial / n) * (p.radial_acceleration * dt)

            if abs(p.attractor_strength) > 1e-6:
                to_attractor = attractor - particle.pos
                n = np.linalg.norm(to_attractor)
                if n > 1e-3:
                    particle.vel = particle.vel + (to_attractor / n) * (p.attractor_strength * dt)

            if p.noise_amplitude > 1e-6:
                sample = particle.pos * p.noise_frequency + np.array([particle.phase * 13.0, 0.0, 0.0])
                particle.vel = particle.vel + _curl_noise(sample, self.noise_time) * (p.noise_amplitude * dt)

            if p.drag > 1e-6:
                particle.vel = particle.vel * max(0.0, 1.0 - p.drag * dt)

            particle.pos = particle.pos + particle.vel * dt

            if abs(p.orbital_speed) > 1e-6:
                x, y, z = particle.pos
                particle.pos = np.array([x * cos_a - z * sin_a, y, x * sin_a + z * cos_a])

            particle.rotation += particle.angular_velocity * dt

            t = particle.age / particle.lifetime
            particle.size = particle.base_size * self.size_curve(t)
            particle.color = self.color_curve(t)
            alive.append(particle)

        self.particles = alive

    @property
    def finished(self):
        return (not self.p.loop) and self.age >= (self.p.start_delay + self.p.duration) and not self.particles


# =========================================================================================
# Rendering
# =========================================================================================

def _blob(size, sharpness=2.0):
    """Radial falloff sprite standing in for a soft particle texture."""
    ax = (np.arange(size) + 0.5) / size * 2.0 - 1.0
    xx, yy = np.meshgrid(ax, ax)
    r = np.sqrt(xx * xx + yy * yy)
    return np.clip(1.0 - r, 0.0, 1.0) ** sharpness


_BLOB_CACHE = {}


def _get_blob(size):
    size = max(1, min(int(size), 512))
    if size not in _BLOB_CACHE:
        _BLOB_CACHE[size] = _blob(size)
    return _BLOB_CACHE[size]


# --- Material blend resolution -----------------------------------------------------------
#
# The engine has exactly two blend states: opaque (ONE/ZERO) and standard alpha
# (SRC_ALPHA/INV_SRC_ALPHA). There is no additive path, and a material whose type is Unlit
# rather than Translucent renders OPAQUE -- hard, occluding quads. The preview reads the
# material's real ATTR chunk so it reproduces that instead of flattering it.

_MATERIAL_TYPES = ("Opaque", "Unlit", "Masked", "Translucent", "UserInterface")
_BLEND_CACHE = {}


def _resolve_material(material_name, data_root):
    """Return (blend, note) for a material path. blend is 'alpha' or 'opaque'."""
    if not material_name:
        return "alpha", None
    if material_name in _BLEND_CACHE:
        return _BLEND_CACHE[material_name]

    result = ("alpha", "material not found: assuming translucent")
    path = os.path.join(data_root, material_name.replace("/", os.sep))
    try:
        with open(path, "rb") as f:
            data = f.read()
        pos = 0
        while pos + 8 <= len(data):
            magic = data[pos:pos + 4]
            size = struct.unpack("<I", data[pos + 4:pos + 8])[0]
            if magic == b"ATTR" and size in (4, 6):
                payload = data[pos + 8:pos + 8 + size]
                type_id = payload[3]
                type_name = _MATERIAL_TYPES[type_id] if type_id < len(_MATERIAL_TYPES) else "?"
                translucent = type_name in ("Translucent", "UserInterface")
                note = None
                if not translucent:
                    note = ("type=%s renders OPAQUE -- particles will be hard occluding quads"
                            % type_name)
                elif size == 6 and payload[4]:
                    note = "depth-write is on: translucent particles will cull each other"
                result = ("alpha" if translucent else "opaque", note)
                break
            pos += 8 + size
    except OSError:
        pass

    _BLEND_CACHE[material_name] = result
    return result


def _draw_particle(buf, cx, cy, half_w, half_h, angle, rgba, opaque):
    """Stamp an axis-aligned-then-rotated soft blob into the float RGB buffer."""
    h, w = buf.shape[:2]
    ext = int(math.ceil(math.hypot(half_w, half_h))) + 1
    if ext <= 0:
        return
    x0, x1 = max(0, int(cx) - ext), min(w, int(cx) + ext + 1)
    y0, y1 = max(0, int(cy) - ext), min(h, int(cy) + ext + 1)
    if x0 >= x1 or y0 >= y1:
        return

    ys, xs = np.mgrid[y0:y1, x0:x1]
    dx = xs + 0.5 - cx
    dy = ys + 0.5 - cy
    ca, sa = math.cos(-angle), math.sin(-angle)
    u = (dx * ca - dy * sa) / max(half_w, 1e-3)
    v = (dx * sa + dy * ca) / max(half_h, 1e-3)
    r = np.sqrt(u * u + v * v)
    mask = np.clip(1.0 - r, 0.0, 1.0) ** 2.0
    a = mask * rgba[3]
    if not np.any(a > 0.002):
        return

    color = np.array(rgba[:3], dtype=float)
    region = buf[y0:y1, x0:x1]
    if opaque:
        # An Unlit material ignores alpha entirely: the whole quad is written flat.
        hard = (np.abs(u) <= 1.0) & (np.abs(v) <= 1.0)
        region[hard] = color
    else:
        region *= (1.0 - a)[..., None]
        region += a[..., None] * color


def _project(point, camera, pixels_per_unit, width, height, origin_y_px):
    """World -> pixel. Cameras are fixed orthographic views; y is up in world space."""
    x, y, z = point
    if camera == "side":
        px, py = x, y
    elif camera == "front":
        px, py = z, y
    elif camera == "top":
        px, py = x, z
    else:
        # 3/4 view: rotate 35 deg around Y, then tilt.
        ca, sa = math.cos(math.radians(35.0)), math.sin(math.radians(35.0))
        rx = x * ca - z * sa
        rz = x * sa + z * ca
        px, py = rx, y + rz * 0.30
    return (width * 0.5 + px * pixels_per_unit, origin_y_px - py * pixels_per_unit)


def _draw_reference_figure(img_buf, camera, ppu, width, height, origin_y_px):
    """Faint 1.8-unit humanoid silhouette for scale (matches the client's ~1.8u capsule)."""
    def to_px(p):
        return _project(p, camera, ppu, width, height, origin_y_px)

    segments = [
        ((0, 1.45, 0), (0, 1.75, 0)),   # head
        ((0, 0.95, 0), (0, 1.45, 0)),   # torso
        ((-0.28, 1.35, 0), (0.28, 1.35, 0)),  # shoulders
        ((-0.28, 1.35, 0), (-0.34, 0.95, 0)),  # arms
        ((0.28, 1.35, 0), (0.34, 0.95, 0)),
        ((0, 0.95, 0), (-0.16, 0.0, 0)),  # legs
        ((0, 0.95, 0), (0.16, 0.0, 0)),
    ]
    for a, b in segments:
        ax, ay = to_px(a)
        bx, by = to_px(b)
        steps = int(max(abs(bx - ax), abs(by - ay))) + 1
        for i in range(steps + 1):
            t = i / steps
            x = int(ax + (bx - ax) * t)
            y = int(ay + (by - ay) * t)
            for ox in (-1, 0, 1):
                for oy in (-1, 0, 1):
                    xx, yy = x + ox, y + oy
                    if 0 <= xx < width and 0 <= yy < height:
                        img_buf[yy, xx] = np.maximum(img_buf[yy, xx], 0.16)


def render_frames(system, frames=8, duration=None, fps=60.0, size=320, camera="threequarter",
                  view_height=6.0, seed=1234, figure=False, exposure=1.0,
                  data_root=DEFAULT_DATA_ROOT):
    """Simulate the system and return a list of (time, PIL.Image) frames."""
    rng = random.Random(seed)
    sims = [EmitterSim(e, rng) for e in system.emitters]

    if duration is None:
        longest = 0.0
        for e in system.emitters:
            if e.loop:
                longest = max(longest, e.start_delay + e.duration * 2.0)
            else:
                longest = max(longest, e.start_delay + e.duration + e.max_lifetime)
        duration = max(0.5, longest * 1.05)

    ppu = size / view_height
    # Ground line near the bottom for the elevation views; a top-down view has no ground
    # line, so its origin sits in the middle of the tile instead.
    origin_y_px = size * 0.5 if camera == "top" else size * 0.88
    dt = 1.0 / fps
    sample_times = [duration * (i + 1) / frames for i in range(frames)]

    out = []
    t = 0.0
    next_sample = 0
    steps = int(math.ceil(duration / dt))
    for _ in range(steps):
        for sim in sims:
            sim.step(dt)
        t += dt
        while next_sample < len(sample_times) and t >= sample_times[next_sample]:
            out.append((t, _render_frame(sims, size, camera, ppu, origin_y_px, figure, exposure, data_root)))
            next_sample += 1
    while len(out) < frames:
        out.append((t, _render_frame(sims, size, camera, ppu, origin_y_px, figure, exposure, data_root)))
    return out


def _render_frame(sims, size, camera, ppu, origin_y_px, figure, exposure, data_root):
    buf = np.zeros((size, size, 3), dtype=float)
    if figure:
        _draw_reference_figure(buf, camera, ppu, size, size, origin_y_px)

    for sim in sims:
        params = sim.p
        blend, _ = _resolve_material(params.material_name, data_root)
        opaque = (blend == "opaque")
        stretched = params.render_mode in (hpar.RENDER_VELOCITY_ALIGNED, hpar.RENDER_STRETCHED)
        for particle in sim.particles:
            cx, cy = _project(particle.pos, camera, ppu, size, size, origin_y_px)
            half = particle.size * 0.5 * ppu
            if half < 0.35:
                continue

            angle = 0.0
            half_w = half_h = half
            if stretched:
                length = half * (max(1.0, params.length_scale) if params.render_mode == hpar.RENDER_STRETCHED else 1.0)
                vx, vy = _project(particle.pos + particle.vel, camera, ppu, size, size, origin_y_px)
                dx, dy = vx - cx, vy - cy
                if abs(dx) + abs(dy) > 1e-4:
                    angle = math.atan2(dy, dx) - math.pi * 0.5
                half_w, half_h = half, length
            elif params.render_mode == hpar.RENDER_HORIZONTAL:
                # Flat on the ground: foreshortened in the side/threequarter views.
                squash = {"top": 1.0, "side": 0.18, "front": 0.18}.get(camera, 0.45)
                half_w, half_h = half, max(0.35, half * squash)
            else:
                angle = particle.rotation

            _draw_particle(buf, cx, cy, half_w, half_h, angle, particle.color, opaque)

    buf = buf * exposure
    return Image.fromarray((np.clip(buf, 0.0, 1.0) * 255).astype(np.uint8), "RGB")


def contact_sheet(frames, columns=4, label=True):
    from PIL import ImageDraw
    if not frames:
        raise ValueError("no frames to lay out")
    tile = frames[0][1].size[0]
    rows = int(math.ceil(len(frames) / columns))
    sheet = Image.new("RGB", (columns * tile, rows * tile), (12, 12, 16))
    draw = ImageDraw.Draw(sheet)
    for i, (t, img) in enumerate(frames):
        x = (i % columns) * tile
        y = (i // columns) * tile
        sheet.paste(img, (x, y))
        if label:
            draw.text((x + 6, y + 5), "t=%.2fs" % t, fill=(190, 190, 200))
        draw.rectangle([x, y, x + tile - 1, y + tile - 1], outline=(40, 40, 48))
    return sheet


def main():
    ap = argparse.ArgumentParser(description="Render a contact sheet preview of a .hpar particle system.")
    ap.add_argument("input", help="path to the .hpar file")
    ap.add_argument("--out", help="output PNG (default: <input>_preview.png next to the input)")
    ap.add_argument("--frames", type=int, default=8)
    ap.add_argument("--columns", type=int, default=4)
    ap.add_argument("--duration", type=float, default=None,
                    help="seconds to simulate (default: auto from emitter duration + lifetime)")
    ap.add_argument("--size", type=int, default=320, help="tile size in pixels")
    ap.add_argument("--camera", default="threequarter", choices=["side", "front", "top", "threequarter"])
    ap.add_argument("--view-height", type=float, default=6.0, help="world units covered by the tile height")
    ap.add_argument("--seed", type=int, default=1234)
    ap.add_argument("--fps", type=float, default=60.0)
    ap.add_argument("--exposure", type=float, default=1.0,
                    help="plain output gain; the engine has no HDR or bloom, so 1.0 is the truth")
    ap.add_argument("--data-root", default=DEFAULT_DATA_ROOT,
                    help="client data root used to resolve material blend modes")
    ap.add_argument("--figure", action="store_true", help="draw a 1.8-unit character silhouette for scale")
    args = ap.parse_args()

    system = hpar.load(args.input)
    frames = render_frames(system, frames=args.frames, duration=args.duration, fps=args.fps,
                           size=args.size, camera=args.camera, view_height=args.view_height,
                           seed=args.seed, figure=args.figure, exposure=args.exposure,
                           data_root=args.data_root)
    sheet = contact_sheet(frames, columns=args.columns)

    out = args.out or os.path.splitext(args.input)[0] + "_preview.png"
    sheet.save(out)
    print("wrote %s (%d emitters, %d frames, %.2fs)" %
          (out, len(system.emitters), len(frames), frames[-1][0]))

    for e in system.emitters:
        blend, note = _resolve_material(e.material_name, args.data_root)
        print("  %-16s %-38s %s%s" % (e.name, e.material_name or "(none)", blend,
                                      ("  <-- " + note) if note else ""))


if __name__ == "__main__":
    main()
