# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Read / write the engine's ``.hpar`` particle-system files.

Mirrors ``src/shared/scene_graph/particle_emitter_serializer.cpp`` exactly. The v2.0 file
layout is a flat sequence of chunks::

    VERS  uint32 version (0x0200)
    PSYS  uint32 emitterCount          (informational only)
    RTME  <emitter blob>               (one per emitter, in order)

Chunk framing is ``<4-byte magic><uint32 payload size><payload>``. The magics in the file
are the *reversed* four-character codes used in the C++ source (MakeChunkMagic of "EMTR"
lands on disk as ``RTME``).

Everything is little endian. Strings are ``uint16`` length-prefixed (the legacy v1.0 PARM
chunk used ``uint8`` instead).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import List, Sequence

# --- Chunk magics as they appear on disk -----------------------------------------------
MAGIC_VERSION = b"VERS"
MAGIC_SYSTEM = b"PSYS"
MAGIC_EMITTER = b"RTME"
MAGIC_LEGACY_PARAMS = b"PARM"
MAGIC_LEGACY_COLOR = b"COLR"

VERSION_1_0 = 0x0100
VERSION_2_0 = 0x0200

# --- Enums (values must match particle_emitter.h) ---------------------------------------
SHAPE_POINT, SHAPE_SPHERE, SHAPE_BOX, SHAPE_CONE = 0, 1, 2, 3
SIM_WORLD, SIM_LOCAL = 0, 1
RENDER_BILLBOARD, RENDER_VELOCITY_ALIGNED, RENDER_STRETCHED, RENDER_HORIZONTAL, RENDER_MESH = 0, 1, 2, 3, 4
SPRITE_NONE, SPRITE_ANIMATE_OVER_LIFE, SPRITE_RANDOM_STATIC = 0, 1, 2

SHAPE_NAMES = {SHAPE_POINT: "Point", SHAPE_SPHERE: "Sphere", SHAPE_BOX: "Box", SHAPE_CONE: "Cone"}
SIM_NAMES = {SIM_WORLD: "World", SIM_LOCAL: "Local"}
RENDER_NAMES = {
    RENDER_BILLBOARD: "BillboardFacing",
    RENDER_VELOCITY_ALIGNED: "VelocityAligned",
    RENDER_STRETCHED: "Stretched",
    RENDER_HORIZONTAL: "HorizontalBillboard",
    RENDER_MESH: "Mesh",
}
SPRITE_NAMES = {SPRITE_NONE: "None", SPRITE_ANIMATE_OVER_LIFE: "AnimateOverLife", SPRITE_RANDOM_STATIC: "RandomStatic"}


# =========================================================================================
# Curves
# =========================================================================================

@dataclass
class FloatKey:
    time: float = 0.0
    value: float = 1.0
    in_tangent: float = 0.0
    out_tangent: float = 0.0
    tangent_mode: int = 0


@dataclass
class ColorKey:
    time: float = 0.0
    color: Sequence[float] = (1.0, 1.0, 1.0, 1.0)
    in_tangent: Sequence[float] = (0.0, 0.0, 0.0, 0.0)
    out_tangent: Sequence[float] = (0.0, 0.0, 0.0, 0.0)
    tangent_mode: int = 0


def float_curve(*points) -> List[FloatKey]:
    """Build a float curve from ``(time, value)`` pairs, or from bare values spread evenly."""
    if not points:
        return [FloatKey(0.0, 1.0), FloatKey(1.0, 1.0)]
    if isinstance(points[0], (tuple, list)):
        return [FloatKey(float(t), float(v)) for t, v in points]
    n = len(points)
    if n == 1:
        return [FloatKey(0.0, float(points[0])), FloatKey(1.0, float(points[0]))]
    return [FloatKey(i / (n - 1), float(v)) for i, v in enumerate(points)]


def color_curve(*points) -> List[ColorKey]:
    """Build a colour curve from ``(time, (r,g,b,a))`` pairs, or from bare RGBA tuples."""
    if not points:
        return [ColorKey(0.0, (1, 1, 1, 1)), ColorKey(1.0, (1, 1, 1, 0))]
    if len(points[0]) == 2 and isinstance(points[0][1], (tuple, list)):
        return [ColorKey(float(t), tuple(float(c) for c in col)) for t, col in points]
    n = len(points)
    if n == 1:
        return [ColorKey(0.0, tuple(points[0])), ColorKey(1.0, tuple(points[0]))]
    return [ColorKey(i / (n - 1), tuple(float(c) for c in col)) for i, col in enumerate(points)]


# =========================================================================================
# Emitter parameters
# =========================================================================================

@dataclass
class Burst:
    time: float = 0.0
    count: int = 10


@dataclass
class Emitter:
    """One emitter's module stack. Field defaults match ``EmitterParameters`` in C++."""

    # Emitter module
    name: str = "Emitter"
    enabled: bool = True
    simulation_space: int = SIM_WORLD
    loop: bool = True
    duration: float = 1.0
    start_delay: float = 0.0
    warmup_time: float = 0.0
    inherit_velocity: float = 0.0

    # Emission module
    spawn_rate: float = 10.0
    max_particles: int = 100
    bursts: List[Burst] = field(default_factory=list)

    # Shape module
    shape: int = SHAPE_POINT
    shape_extents: Sequence[float] = (0.0, 0.0, 0.0)

    # Spawn module
    min_lifetime: float = 1.0
    max_lifetime: float = 2.0
    min_velocity: Sequence[float] = (0.0, 1.0, 0.0)
    max_velocity: Sequence[float] = (0.0, 2.0, 0.0)
    min_start_speed: float = 0.0
    max_start_speed: float = 0.0
    min_start_size: float = 1.0
    max_start_size: float = 1.0
    min_start_rotation: float = 0.0
    max_start_rotation: float = 0.0
    min_angular_velocity: float = 0.0
    max_angular_velocity: float = 0.0

    # Update / forces module
    gravity: Sequence[float] = (0.0, -9.81, 0.0)
    drag: float = 0.0
    orbital_speed: float = 0.0
    radial_acceleration: float = 0.0
    attractor_position: Sequence[float] = (0.0, 0.0, 0.0)
    attractor_strength: float = 0.0
    noise_amplitude: float = 0.0
    noise_frequency: float = 1.0

    # Over-life curves
    size_over_life: List[FloatKey] = field(default_factory=lambda: float_curve(1.0))
    color_over_lifetime: List[ColorKey] = field(default_factory=color_curve)

    # Sprite sheet
    sprite_sheet_columns: int = 1
    sprite_sheet_rows: int = 1
    sprite_animation: int = SPRITE_NONE
    sprite_animation_fps: float = 0.0

    # Render module
    render_mode: int = RENDER_BILLBOARD
    length_scale: float = 1.0
    material_name: str = ""
    mesh_name: str = ""


@dataclass
class ParticleSystem:
    emitters: List[Emitter] = field(default_factory=list)


# =========================================================================================
# Binary IO helpers
# =========================================================================================

class _Writer:
    def __init__(self) -> None:
        self.buf = bytearray()

    def u8(self, v):
        self.buf += struct.pack("<B", int(v) & 0xFF)

    def u16(self, v):
        self.buf += struct.pack("<H", int(v))

    def u32(self, v):
        self.buf += struct.pack("<I", int(v))

    def f32(self, v):
        self.buf += struct.pack("<f", float(v))

    def vec3(self, v):
        for c in tuple(v)[:3]:
            self.f32(c)

    def string(self, s):
        data = (s or "").encode("utf-8")
        self.u16(len(data))
        self.buf += data

    def float_curve(self, keys):
        self.u32(len(keys))
        for k in keys:
            self.f32(k.time)
            self.f32(k.value)
            self.f32(k.in_tangent)
            self.f32(k.out_tangent)
            self.u8(k.tangent_mode)

    def color_curve(self, keys):
        self.u32(len(keys))
        for k in keys:
            self.f32(k.time)
            for c in tuple(k.color)[:4]:
                self.f32(c)
            for c in tuple(k.in_tangent)[:4]:
                self.f32(c)
            for c in tuple(k.out_tangent)[:4]:
                self.f32(c)
            self.u8(k.tangent_mode)


class _Reader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.pos = 0

    def take(self, n):
        if self.pos + n > len(self.data):
            raise EOFError("read past end of file at %d (+%d)" % (self.pos, n))
        out = self.data[self.pos:self.pos + n]
        self.pos += n
        return out

    def u8(self):
        return struct.unpack("<B", self.take(1))[0]

    def u16(self):
        return struct.unpack("<H", self.take(2))[0]

    def u32(self):
        return struct.unpack("<I", self.take(4))[0]

    def f32(self):
        return struct.unpack("<f", self.take(4))[0]

    def vec3(self):
        return (self.f32(), self.f32(), self.f32())

    def vec4(self):
        return (self.f32(), self.f32(), self.f32(), self.f32())

    def string(self, length_type=16):
        n = self.u16() if length_type == 16 else self.u8()
        return self.take(n).decode("utf-8", errors="replace")

    def float_curve(self):
        count = self.u32()
        return [FloatKey(self.f32(), self.f32(), self.f32(), self.f32(), self.u8())
                for _ in range(count)]

    def color_curve(self):
        count = self.u32()
        return [ColorKey(self.f32(), self.vec4(), self.vec4(), self.vec4(), self.u8())
                for _ in range(count)]


def _chunk(magic: bytes, payload: bytes) -> bytes:
    return magic + struct.pack("<I", len(payload)) + payload


# =========================================================================================
# Serialization
# =========================================================================================

def _write_emitter(e: Emitter) -> bytes:
    w = _Writer()

    w.string(e.name)
    w.u8(1 if e.enabled else 0)
    w.u8(e.simulation_space)
    w.u8(1 if e.loop else 0)
    w.f32(e.duration)
    w.f32(e.start_delay)
    w.f32(e.warmup_time)
    w.f32(e.inherit_velocity)

    w.f32(e.spawn_rate)
    w.u32(e.max_particles)
    w.u32(len(e.bursts))
    for b in e.bursts:
        w.f32(b.time)
        w.u32(b.count)

    w.u8(e.shape)
    w.vec3(e.shape_extents)

    w.f32(e.min_lifetime)
    w.f32(e.max_lifetime)
    w.vec3(e.min_velocity)
    w.vec3(e.max_velocity)
    w.f32(e.min_start_speed)
    w.f32(e.max_start_speed)
    w.f32(e.min_start_size)
    w.f32(e.max_start_size)
    w.f32(e.min_start_rotation)
    w.f32(e.max_start_rotation)
    w.f32(e.min_angular_velocity)
    w.f32(e.max_angular_velocity)

    w.vec3(e.gravity)
    w.f32(e.drag)
    w.f32(e.orbital_speed)
    w.f32(e.radial_acceleration)
    w.vec3(e.attractor_position)
    w.f32(e.attractor_strength)
    w.f32(e.noise_amplitude)
    w.f32(e.noise_frequency)

    w.u32(e.sprite_sheet_columns)
    w.u32(e.sprite_sheet_rows)
    w.u8(e.sprite_animation)
    w.f32(e.sprite_animation_fps)

    w.u8(e.render_mode)
    w.f32(e.length_scale)
    w.string(e.material_name)

    w.float_curve(e.size_over_life)
    w.color_curve(e.color_over_lifetime)

    # Trailing field appended after v2.0; readers stop at the chunk boundary when absent.
    w.string(e.mesh_name)

    return _chunk(MAGIC_EMITTER, bytes(w.buf))


def dumps(system: ParticleSystem) -> bytes:
    out = bytearray()
    out += _chunk(MAGIC_VERSION, struct.pack("<I", VERSION_2_0))
    out += _chunk(MAGIC_SYSTEM, struct.pack("<I", len(system.emitters)))
    for e in system.emitters:
        out += _write_emitter(e)
    return bytes(out)


def save(system: ParticleSystem, path: str) -> None:
    with open(path, "wb") as f:
        f.write(dumps(system))


def _read_emitter(r: _Reader, chunk_end: int) -> Emitter:
    e = Emitter()
    e.name = r.string()
    e.enabled = r.u8() != 0
    e.simulation_space = r.u8()
    e.loop = r.u8() != 0
    e.duration = r.f32()
    e.start_delay = r.f32()
    e.warmup_time = r.f32()
    e.inherit_velocity = r.f32()

    e.spawn_rate = r.f32()
    e.max_particles = r.u32()
    burst_count = r.u32()
    e.bursts = [Burst(r.f32(), r.u32()) for _ in range(burst_count)]

    e.shape = r.u8()
    e.shape_extents = r.vec3()

    e.min_lifetime = r.f32()
    e.max_lifetime = r.f32()
    e.min_velocity = r.vec3()
    e.max_velocity = r.vec3()
    e.min_start_speed = r.f32()
    e.max_start_speed = r.f32()
    e.min_start_size = r.f32()
    e.max_start_size = r.f32()
    e.min_start_rotation = r.f32()
    e.max_start_rotation = r.f32()
    e.min_angular_velocity = r.f32()
    e.max_angular_velocity = r.f32()

    e.gravity = r.vec3()
    e.drag = r.f32()
    e.orbital_speed = r.f32()
    e.radial_acceleration = r.f32()
    e.attractor_position = r.vec3()
    e.attractor_strength = r.f32()
    e.noise_amplitude = r.f32()
    e.noise_frequency = r.f32()

    e.sprite_sheet_columns = r.u32()
    e.sprite_sheet_rows = r.u32()
    e.sprite_animation = r.u8()
    e.sprite_animation_fps = r.f32()

    e.render_mode = r.u8()
    e.length_scale = r.f32()
    e.material_name = r.string()

    e.size_over_life = r.float_curve()
    e.color_over_lifetime = r.color_curve()

    if r.pos < chunk_end:
        e.mesh_name = r.string()

    return e


def _read_legacy_params(r: _Reader) -> Emitter:
    e = Emitter()
    e.spawn_rate = r.f32()
    e.max_particles = r.u32()
    e.shape = r.u8()
    e.shape_extents = r.vec3()
    e.min_lifetime = r.f32()
    e.max_lifetime = r.f32()
    e.min_velocity = r.vec3()
    e.max_velocity = r.vec3()
    e.gravity = r.vec3()
    start_size = r.f32()
    end_size = r.f32()
    e.sprite_sheet_columns = r.u32()
    e.sprite_sheet_rows = r.u32()
    e.sprite_animation = SPRITE_ANIMATE_OVER_LIFE if r.u8() else SPRITE_NONE
    e.material_name = r.string(length_type=8)

    ref = start_size or end_size or 1.0
    e.min_start_size = ref
    e.max_start_size = ref
    e.size_over_life = float_curve((0.0, start_size / ref), (1.0, end_size / ref))
    return e


def loads(data: bytes) -> ParticleSystem:
    r = _Reader(data)
    system = ParticleSystem()
    version = VERSION_1_0
    legacy = None

    while r.pos + 8 <= len(data):
        magic = r.take(4)
        size = r.u32()
        chunk_end = r.pos + size

        if magic == MAGIC_VERSION:
            version = r.u32()
        elif magic == MAGIC_SYSTEM:
            r.u32()
        elif magic == MAGIC_EMITTER:
            system.emitters.append(_read_emitter(r, chunk_end))
        elif magic == MAGIC_LEGACY_PARAMS:
            legacy = _read_legacy_params(r)
        elif magic == MAGIC_LEGACY_COLOR:
            # Legacy COLR wraps a chunked colour curve: REVC(version) + CKEY(keys).
            if legacy is not None:
                sub = _Reader(data[r.pos:chunk_end])
                while sub.pos + 8 <= len(sub.data):
                    sub_magic = sub.take(4)
                    sub_size = sub.u32()
                    sub_end = sub.pos + sub_size
                    if sub_magic == b"YEKC":
                        legacy.color_over_lifetime = sub.color_curve()
                    sub.pos = sub_end
        r.pos = chunk_end

    if version == VERSION_1_0:
        if legacy is None:
            raise ValueError("legacy particle file missing PARM chunk")
        system.emitters = [legacy]

    if not system.emitters:
        raise ValueError("particle file contained no emitters")
    return system


def load(path: str) -> ParticleSystem:
    with open(path, "rb") as f:
        return loads(f.read())
