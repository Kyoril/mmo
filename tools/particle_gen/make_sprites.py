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


SPRITES = {
    "T_Particle_Glow": glow,
    "T_Particle_Beam": beam,
    "T_Particle_Ring": ring,
}


def main():
    keep_png = "--keep-png" in sys.argv
    for name, builder in SPRITES.items():
        png = _write(name, builder())
        htex = os.path.join(OUT_DIR, name + ".htex")
        # DXT5 is forced: alpha is the whole point of these sprites and 'auto' would pick
        # DXT1 for any sprite whose alpha happened to be fully opaque.
        subprocess.run([sys.executable, IMPORTER, png, htex, "--format", "dxt5"], check=True)
        if not keep_png:
            os.remove(png)
        print("wrote %s" % htex)


if __name__ == "__main__":
    main()
