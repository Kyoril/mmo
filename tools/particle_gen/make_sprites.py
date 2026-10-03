# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Generate the soft particle sprites used by the particle materials, then import them as
``.htex``.

The engine only has two blend states (opaque and standard src-alpha), so a particle's
softness has to live in the texture's **alpha channel** -- there is no additive blending to
hide a hard quad edge. Every sprite here is therefore white RGB with a smooth alpha falloff
that reaches exactly 0 at the border, and must be encoded as DXT5 (DXT1 has no usable
alpha; a DXT1 particle sprite renders as an opaque black square).

    python tools/particle_gen/make_sprites.py

Requires ``texconv.exe`` (DirectX SDK or FFXIV TexTools) -- the same importer the item
icon pipeline uses.
"""

from __future__ import annotations

import os
import struct
import subprocess
import sys

import numpy as np
from PIL import Image

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT_DIR = os.path.join(REPO_ROOT, "data", "client", "Particles")
IMPORTER = os.path.join(REPO_ROOT, ".claude", "skills", "mmo-item-designer", "scripts", "import_item_icon.py")


def _write(name, rgba):
    """Save an RGBA float array (0..1) as a PNG and return its path."""
    path = os.path.join(OUT_DIR, name + ".png")
    Image.fromarray((np.clip(rgba, 0.0, 1.0) * 255).astype(np.uint8), "RGBA").save(path)
    return path


def _axes(width, height):
    """Normalized [-1,1] coordinate grids."""
    x = (np.arange(width) + 0.5) / width * 2.0 - 1.0
    y = (np.arange(height) + 0.5) / height * 2.0 - 1.0
    return np.meshgrid(x, y)


def glow(size=128, core=0.10, falloff=2.4):
    """Soft radial blob: a small saturated core inside a wide smooth falloff.

    The workhorse sprite -- ground flares, sparks, generic puffs. `core` is the fraction of
    the radius held at full alpha; without it the centre reads as washed-out rather than hot.
    """
    xx, yy = _axes(size, size)
    r = np.sqrt(xx * xx + yy * yy)
    a = np.clip((1.0 - r) / (1.0 - core), 0.0, 1.0) ** falloff
    a[r <= core] = 1.0
    rgba = np.ones((size, size, 4))
    rgba[..., 3] = a
    return rgba


def beam(width=64, height=256, edge=1.8, taper=0.30):
    """Vertical soft streak for `RENDER_STRETCHED` particles.

    U runs across the quad and V along the stretch axis, so the horizontal profile is the
    beam's thickness and the vertical profile tapers both ends. Symmetric top-to-bottom, so
    it does not matter which way the velocity points.
    """
    xx, yy = _axes(width, height)
    across = np.clip(1.0 - np.abs(xx), 0.0, 1.0) ** edge
    ends = np.clip((1.0 - np.abs(yy)) / taper, 0.0, 1.0)
    ends = ends * ends * (3.0 - 2.0 * ends)  # smoothstep, so the tips fade instead of cutting
    rgba = np.ones((height, width, 4))
    rgba[..., 3] = across * ends
    return rgba


def ring(size=256, radius=0.62, thickness=0.30):
    """Soft annulus for expanding ground shockwaves."""
    xx, yy = _axes(size, size)
    r = np.sqrt(xx * xx + yy * yy)
    a = np.clip(1.0 - np.abs(r - radius) / thickness, 0.0, 1.0) ** 2.0
    a *= np.clip((1.0 - r) / 0.25, 0.0, 1.0)  # hard-clip the outside edge to 0 at the border
    rgba = np.ones((size, size, 4))
    rgba[..., 3] = a
    return rgba


def _smooth(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def sigil(size=512):
    """Holy rune circle for ground glyphs (`RENDER_HORIZONTAL`).

    Two concentric bands with a ring of short radial ticks between them and a soft
    four-pointed star in the middle. Read from above at character scale it is a
    consecration circle; the thin strokes are what make it read as *drawn* light rather
    than as a second shockwave ring. Fades to 0 at the border like every sprite here.
    """
    xx, yy = _axes(size, size)
    r = np.sqrt(xx * xx + yy * yy)
    theta = np.arctan2(yy, xx)
    px = 2.0 / size  # one texel in normalized units, keeps strokes antialiased

    def band(radius, width):
        return np.clip(1.0 - np.abs(r - radius) / width, 0.0, 1.0) ** 1.5

    a = band(0.90, 0.040) + 0.8 * band(0.80, 0.018) + 0.9 * band(0.52, 0.030)
    # 24 radial ticks between the outer and inner bands.
    tick = np.abs(np.sin(theta * 12.0))
    tick = np.clip(1.0 - tick / 0.10, 0.0, 1.0) * _smooth(0.56, 0.60, r) * (1.0 - _smooth(0.74, 0.78, r))
    a += 0.85 * tick
    # Eight small dots riding the middle band.
    dot_phase = np.abs(np.sin(theta * 4.0 + np.pi / 8.0))
    dots = np.clip(1.0 - dot_phase / 0.12, 0.0, 1.0) * np.clip(1.0 - np.abs(r - 0.665) / 0.035, 0.0, 1.0)
    a += dots
    # Four-pointed star in the centre: thin along the axes, fading outwards.
    ax = np.minimum(np.abs(xx), np.abs(yy))
    star = np.clip(1.0 - ax / (0.05 * (1.0 - r / 0.45) + px), 0.0, 1.0) * (1.0 - _smooth(0.30, 0.45, r))
    a += 0.9 * star + 0.5 * np.clip(1.0 - r / 0.10, 0.0, 1.0)
    a = np.clip(a, 0.0, 1.0) * (1.0 - _smooth(0.94, 1.0, r))
    rgba = np.ones((size, size, 4))
    rgba[..., 3] = a
    return rgba


def rays(size=256, count=12):
    """Sunburst: soft tapered light rays around a hot centre, for divine flashes.

    Billboarded at an impact it reads as radiance bursting outward, which a round glow blob
    cannot do without additive blending. Alternating long/short rays keep it from looking
    like a gear.
    """
    xx, yy = _axes(size, size)
    r = np.sqrt(xx * xx + yy * yy)
    theta = np.arctan2(yy, xx)
    phase = np.abs(np.sin(theta * count / 2.0))
    width = 0.22 + 0.45 * (1.0 - r)
    ray = np.clip(1.0 - phase / width, 0.0, 1.0) ** 1.6
    long_mask = (np.cos(theta * count) > 0).astype(float)
    reach = 0.65 + 0.33 * long_mask
    ray *= np.clip(1.0 - r / reach, 0.0, 1.0) ** 1.2
    core = np.clip(1.0 - r / 0.28, 0.0, 1.0) ** 1.5
    a = np.clip(ray + core, 0.0, 1.0) * (1.0 - _smooth(0.92, 1.0, r))
    rgba = np.ones((size, size, 4))
    rgba[..., 3] = a
    return rgba


def feather(width=128, height=256):
    """A soft feather: curved quill with barbs fanning off it, tip pointing up.

    Used for slow tumbling feathers in the resurrection and blessing effects. Barbs are
    drawn as alpha striations so the feather still reads at 0.3 alpha.
    """
    xx, yy = _axes(width, height)
    yy = -yy  # tip at the top of the image
    t = (yy + 1.0) * 0.5                       # 0 at the quill base, 1 at the tip
    spine_x = 0.10 * np.sin(t * np.pi * 0.9)   # gentle curve
    dx = xx - spine_x
    half_width = 0.70 * np.sin(np.clip(t, 0.0, 1.0) * np.pi) ** 0.8 * (t > 0.08)
    vane = np.clip(1.0 - np.abs(dx) / (half_width + 1e-4), 0.0, 1.0)
    barbs = 0.82 + 0.18 * np.cos((t * 38.0 + np.abs(dx) * 9.0) * np.pi)
    a = (vane ** 0.6) * barbs
    quill = np.clip(1.0 - np.abs(dx) / 0.035, 0.0, 1.0) * (t < 0.97)
    a = np.clip(np.maximum(a, quill), 0.0, 1.0)
    a *= _smooth(0.0, 0.06, t) * (1.0 - _smooth(0.94, 1.0, t))
    rgba = np.ones((height, width, 4))
    rgba[..., 3] = a
    return rgba


def flame(width=128, height=256):
    """Teardrop flame tongue, round at the bottom and licking to a point at the top.

    For `RENDER_BILLBOARD` holy fire: rising tongues with this silhouette read as flame
    where round glow blobs read as smoke.
    """
    xx, yy = _axes(width, height)
    yy = -yy
    t = (yy + 1.0) * 0.5
    half_width = np.where(t < 0.3, np.sqrt(np.clip(1.0 - ((0.3 - t) / 0.3) ** 2, 0.0, 1.0)) * 0.8,
                          0.8 * (1.0 - (t - 0.3) / 0.7) ** 1.4)
    offset = 0.12 * np.sin(t * np.pi * 1.6) * t
    body = np.clip(1.0 - np.abs(xx - offset) / (half_width + 1e-4), 0.0, 1.0) ** 0.9
    a = body * _smooth(0.0, 0.12, t) * (1.0 - _smooth(0.92, 1.0, t))
    rgba = np.ones((height, width, 4))
    rgba[..., 3] = np.clip(a, 0.0, 1.0)
    return rgba


SPRITES = {
    "T_Particle_Glow": glow,
    "T_Particle_Beam": beam,
    "T_Particle_Ring": ring,
    "T_Particle_Sigil": sigil,
    "T_Particle_Rays": rays,
    "T_Particle_Feather": feather,
    "T_Particle_Flame": flame,
}


# Material instances that put a sprite on Particle_Alpha_Tex.hmat. Only the texture differs
# between them; the ATTR chunk (translucent, depth-write off) is what keeps a particle from
# rendering as an opaque quad -- see the particle-authoring notes on Additive.hmat.
MATERIALS = {
    "Particle_Sigil": "T_Particle_Sigil",
    "Particle_Rays": "T_Particle_Rays",
    "Particle_Feather": "T_Particle_Feather",
    "Particle_Flame": "T_Particle_Flame",
}


def _chunk(tag, payload):
    return tag + struct.pack("<I", len(payload)) + payload


def _short_string(text):
    data = text.encode("ascii")
    return struct.pack("<B", len(data)) + data


def write_material_instance(name, texture):
    """Write Particles/<name>.hmi, byte-compatible with the hand-made Particle_Ring.hmi."""
    asset = "Particles/%s.hmi" % name
    tex_path = ("Particles/%s.htex" % texture).encode("ascii")
    data = b"HMIT" + struct.pack("<II", 4, 0x100)
    data += _chunk(b"NAME", _short_string(asset))
    data += _chunk(b"PRNT", _short_string("Particles/Particle_Alpha_Tex.hmat"))
    data += _chunk(b"ATTR", bytes([1, 0, 0, 3, 0, 1]))
    data += _chunk(b"TPAR", struct.pack("<B", 1) + _short_string("Texture")
                   + struct.pack("<H", len(tex_path)) + tex_path)
    path = os.path.join(OUT_DIR, name + ".hmi")
    with open(path, "wb") as handle:
        handle.write(data)
    return path


def main():
    keep_png = "--keep-png" in sys.argv
    # Naming sprites rebuilds only those, so adding a sprite does not re-encode (and churn
    # the bytes of) every existing one: `make_sprites.py T_Particle_Sigil --keep-png`.
    wanted = [a for a in sys.argv[1:] if not a.startswith("--")]
    for name, builder in SPRITES.items():
        if wanted and name not in wanted:
            continue
        png = _write(name, builder())
        htex = os.path.join(OUT_DIR, name + ".htex")
        # DXT5 is forced: alpha is the whole point of these sprites and 'auto' would pick
        # DXT1 for any sprite whose alpha happened to be fully opaque.
        subprocess.run([sys.executable, IMPORTER, png, htex, "--format", "dxt5"], check=True)
        if not keep_png:
            os.remove(png)
        print("wrote %s" % htex)

    for name, texture in MATERIALS.items():
        if wanted and texture not in wanted:
            continue
        print("wrote %s" % write_material_instance(name, texture))


if __name__ == "__main__":
    main()
