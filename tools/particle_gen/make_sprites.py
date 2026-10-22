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
IMPORTER = os.path.join(REPO_ROOT, ".agents", "skills", "mmo-item-designer", "scripts", "import_item_icon.py")
if not os.path.exists(IMPORTER):
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


def slash(size=256, centre=1.0, arc=1.7, thickness=0.18):
    """Crescent blade slash for melee impacts (`RENDER_BILLBOARD`, rolled by `rotation`).

    An arc whose middle passes through the centre of the quad (its circle is centred
    `centre` to the left), thickest in the middle and tapering to points at both tips, with a
    bright cutting edge and a softer wake behind it. Because the cut crosses the quad centre,
    two of them with different rolls cross each other at the impact point (an X), and a
    random roll keeps every cut on the target. The curve is what reads as a swing.
    """
    xx, yy = _axes(size, size)
    cx = xx + centre
    r = np.sqrt(cx * cx + yy * yy)
    theta = np.arctan2(yy, cx)
    half = arc / 2.0
    along = np.clip(1.0 - np.abs(theta) / half, 0.0, 1.0)      # 1 mid-arc, 0 at the tips
    width = thickness * np.sin(along * np.pi / 2.0) ** 0.8
    d = r - centre
    edge = np.clip(1.0 - np.abs(d - width * 0.30) / (width * 0.35 + 1e-4), 0.0, 1.0) ** 1.3
    wake = np.clip(1.0 - np.abs(d + width * 0.20) / (width + 1e-4), 0.0, 1.0) ** 2.0
    a = np.clip(edge + 0.55 * wake, 0.0, 1.0) * (along > 0.0)
    rr = np.sqrt(xx * xx + yy * yy)
    a *= 1.0 - _smooth(0.92, 1.0, rr)
    rgba = np.ones((size, size, 4))
    rgba[..., 3] = a
    return rgba


def smoke(size=256, seed=7, lobes=9):
    """Soft cloudy smoke puff: overlapping lobes with a wispy, uneven edge.

    A round glow blob reads as light; smoke needs a lumpy silhouette and inner variation so
    a cloud of them reads as billowing. Deterministic (fixed seed) so rebuilding does not
    churn the texture bytes.
    """
    rng = np.random.default_rng(seed)
    xx, yy = _axes(size, size)
    a = np.zeros_like(xx)
    for _ in range(lobes):
        cx, cy = rng.uniform(-0.28, 0.28, 2)
        rad = rng.uniform(0.32, 0.52)
        rr2 = ((xx - cx) ** 2 + (yy - cy) ** 2) / (rad * rad)
        a += np.exp(-2.2 * rr2) * rng.uniform(0.5, 1.0)
    # Low-frequency turbulence so the inside is not a flat fill.
    noise = np.zeros_like(xx)
    for freq, amp in ((3.0, 0.5), (7.0, 0.3), (13.0, 0.2)):
        px, py = rng.uniform(0, 6.28, 2)
        noise += amp * np.sin(xx * freq + px) * np.cos(yy * freq * 1.3 + py)
    a = 1.0 - np.exp(-0.75 * a * (0.65 + 0.35 * noise))
    r = np.sqrt(xx * xx + yy * yy)
    a = np.clip(a, 0.0, 1.0) * (1.0 - _smooth(0.85, 1.0, r))
    rgba = np.ones((size, size, 4))
    rgba[..., 3] = a
    return rgba


def _value_noise(rng, width, height, cells_x, cells_y):
    grid = rng.uniform(0.0, 1.0, (cells_y + 3, cells_x + 3))
    gx = np.linspace(0.0, cells_x, width, endpoint=False) + 1.0
    gy = np.linspace(0.0, cells_y, height, endpoint=False) + 1.0
    x0 = np.floor(gx).astype(int)
    y0 = np.floor(gy).astype(int)
    fx = gx - x0
    fy = gy - y0
    fx = fx * fx * (3.0 - 2.0 * fx)
    fy = fy * fy * (3.0 - 2.0 * fy)
    a = grid[np.ix_(y0, x0)]
    b = grid[np.ix_(y0, x0 + 1)]
    c = grid[np.ix_(y0 + 1, x0)]
    d = grid[np.ix_(y0 + 1, x0 + 1)]
    top = a + (b - a) * fx[None, :]
    bottom = c + (d - c) * fx[None, :]
    return top + (bottom - top) * fy[:, None]


def _fbm(rng, width, height, base_x, base_y, octaves=4):
    total = np.zeros((height, width))
    amp, norm = 1.0, 0.0
    for o in range(octaves):
        total += amp * _value_noise(rng, width, height, base_x * 2 ** o, base_y * 2 ** o)
        norm += amp
        amp *= 0.5
    return total / norm


def fire_lick(width=128, height=256, variants=4, seed=11):
    """Soft, ragged flame licks: a 4x1 sheet of 1:2 frames (use SPRITE_RANDOM_STATIC).

    Unlike the teardrop `flame`, the body is a wide soft column whose edge and top are eroded
    by fractal noise and bent by a low-frequency warp, so a cluster of them reads as a
    natural fire rather than a ring of hard spikes. Deterministic (fixed seed).
    """
    rng = np.random.default_rng(seed)
    frames = []
    for _ in range(variants):
        xx, yy = _axes(width, height)
        yy = -yy
        t = (yy + 1.0) * 0.5  # 0 bottom, 1 top
        warp = _fbm(rng, width, height, 2, 4) - 0.5
        detail = _fbm(rng, width, height, 4, 8)
        lean = rng.uniform(-0.12, 0.12)
        cx = lean * t * t + 0.65 * warp * t ** 1.2
        half = 0.85 * np.sqrt(np.clip(1.0 - ((0.25 - t) / 0.25) ** 2, 0.0, 1.0)) * (t < 0.25) \
            + (t >= 0.25) * (0.85 * np.clip(1.0 - (t - 0.25) / 0.72, 0.0, 1.0) ** 0.75)
        half = np.clip(half, 0.0, None) + 1e-4
        d = np.abs(xx - cx) / half
        edge = d + 0.55 * (detail - 0.5) * (0.3 + t)
        body = np.clip(1.0 - edge, 0.0, 1.0) ** 1.1
        body *= 0.45 + 0.9 * detail
        a = body * _smooth(0.0, 0.16, t) * (1.0 - _smooth(0.62, 0.95, t + 0.35 * (detail - 0.5)))
        a = np.clip(a * 1.25, 0.0, 1.0)
        a *= 1.0 - _smooth(0.9, 1.0, np.abs(xx))
        frames.append(a)
    sheet = np.concatenate(frames, axis=1)
    rgba = np.ones((height, width * variants, 4))
    rgba[..., 3] = sheet
    return rgba


def crosshair(size=256):
    """Hunter's mark: a broken ring with four inward-pointing ticks and a centre dot.

    For a debuff marker hovering over a target. The gaps in the ring and the ticks are what
    make it read as a reticle rather than as another shockwave ring.
    """
    xx, yy = _axes(size, size)
    r = np.sqrt(xx * xx + yy * yy)
    theta = np.arctan2(yy, xx)
    ring_band = np.clip(1.0 - np.abs(r - 0.72) / 0.06, 0.0, 1.0) ** 1.4
    gaps = _smooth(0.10, 0.22, np.abs(np.sin(theta * 2.0)))   # four gaps on the axes
    a = ring_band * gaps
    ax = np.minimum(np.abs(xx), np.abs(yy))
    ticks = np.clip(1.0 - ax / 0.035, 0.0, 1.0) * _smooth(0.34, 0.40, r) * (1.0 - _smooth(0.84, 0.90, r))
    a += ticks
    a += np.clip(1.0 - r / 0.09, 0.0, 1.0) ** 1.2
    a = np.clip(a, 0.0, 1.0) * (1.0 - _smooth(0.94, 1.0, r))
    rgba = np.ones((size, size, 4))
    rgba[..., 3] = a
    return rgba


def skull(size=256):
    """A grinning skull for dread and fear effects (`RENDER_BILLBOARD`, no roll).

    Cranium, cheekbones and a narrower jaw with a row of teeth; the eye sockets and the nose
    are cut out of the alpha, so a tinted skull reads as a spectral face with hollow eyes. A
    faint halo around the silhouette keeps it soft at low alpha without blurring the holes.
    """
    xx, yy = _axes(size, size)
    yy = -yy  # +y up
    px = 2.0 / size

    def ellipse(cx, cy, rx, ry):
        d = np.sqrt(((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2)
        return np.clip((1.0 - d) / (px * 3.0 / min(rx, ry)), 0.0, 1.0)

    cranium = ellipse(0.0, 0.22, 0.58, 0.58)
    cheeks = ellipse(0.0, -0.12, 0.50, 0.30)
    jaw = ellipse(0.0, -0.46, 0.30, 0.24)
    head = np.maximum(np.maximum(cranium, cheeks), jaw)
    # Hollow temples between cheekbones and jaw, so the jaw reads as a separate bone.
    for side in (-1.0, 1.0):
        head *= _smooth(0.9, 1.0, np.sqrt(((xx - 0.40 * side) / 0.14) ** 2 + ((yy + 0.40) / 0.16) ** 2))
    # Teeth: a band across the jaw broken by vertical gaps, under a dark mouth line.
    teeth_gaps = np.clip(np.abs(np.sin(xx * 26.0)) / 0.35, 0.0, 1.0)
    teeth_band = _smooth(0.0, 0.02, -0.34 - yy) * _smooth(0.0, 0.02, yy + 0.54) * (np.abs(xx) < 0.22)
    head *= 1.0 - teeth_band * (1.0 - teeth_gaps)
    head *= 1.0 - np.clip(1.0 - np.abs(yy + 0.33) / 0.03, 0.0, 1.0) * (np.abs(xx) < 0.25)
    # Eye sockets: large and round, their tops cut by a brow line that drops towards the
    # nose -- the scowl.
    for side in (-1.0, 1.0):
        ex, ey = 0.23 * side, 0.0
        rx = xx - ex
        socket = np.sqrt((rx / 0.19) ** 2 + ((yy - ey) / 0.18) ** 2)
        brow = ey + 0.10 + 0.45 * rx * side          # lower on the inner side
        hole = (1.0 - _smooth(0.85, 1.0, socket)) * _smooth(-0.01, 0.01, brow - yy)
        head *= 1.0 - hole
    nose = (np.abs(xx) < 0.075 * (yy + 0.22) / 0.14) * (yy > -0.22) * (yy < -0.08)
    head *= 1.0 - nose.astype(float)
    # Slightly thinner inside than at the rim, so a tinted skull glows at its edges.
    head *= 0.80 + 0.20 * np.clip(np.sqrt(xx * xx + (yy - 0.05) ** 2) / 0.55, 0.0, 1.0)
    r = np.sqrt(xx * xx + yy * yy)
    # The halo only lives outside the silhouette, so the cut-out eyes stay hollow.
    outside = 1.0 - np.maximum(np.maximum(cranium, cheeks), jaw)
    halo = 0.22 * np.clip(1.0 - r / 0.95, 0.0, 1.0) ** 2.0 * outside
    a = np.clip(np.maximum(head, halo), 0.0, 1.0) * (1.0 - _smooth(0.92, 1.0, r))
    rgba = np.ones((size, size, 4))
    rgba[..., 3] = a
    return rgba


def wisp(width=128, height=256):
    """A ghostly wisp: a soft round head trailing a long S-curved tail that thins to nothing.

    Billboarded with a random roll a cloud of these reads as restless spirits or tendrils of
    shadow curling off a body; stretched along velocity the head leads and the tail streams
    behind. Head at the top of the image.
    """
    xx, yy = _axes(width, height)
    yy = -yy
    t = (1.0 - yy) * 0.5                     # 0 at the head (top), 1 at the tail tip
    centre_x = 0.30 * np.sin(t * np.pi * 1.5) * t
    half = 0.62 * (1.0 - t) ** 1.25 + 0.04
    body = np.clip(1.0 - np.abs(xx - centre_x) / half, 0.0, 1.0) ** 1.2
    head_d = np.sqrt((xx / 0.62) ** 2 + ((yy - 0.62) / 0.34) ** 2)
    head = np.clip(1.0 - head_d, 0.0, 1.0) ** 0.8
    a = np.maximum(body * (yy < 0.62), head)
    a *= 0.75 + 0.25 * np.cos(t * 14.0 + xx * 3.0)   # faint striations along the tail
    a *= _smooth(0.0, 0.10, 1.0 - t + 0.02) * (1.0 - _smooth(0.94, 1.0, t))
    a *= 1.0 - _smooth(0.94, 1.0, np.abs(xx))
    rgba = np.ones((height, width, 4))
    rgba[..., 3] = np.clip(a, 0.0, 1.0)
    return rgba


def void_sigil(size=512):
    """Occult ground circle for shadow magic (`RENDER_HORIZONTAL`).

    Unlike the holy `sigil` (concentric bands, a four-point star) this one is broken and
    sharp: an outer ring cut into arcs, a seven-pointed star of straight chords, inward
    thorns, and small rune notches -- drawn darkness rather than consecrated light.
    """
    xx, yy = _axes(size, size)
    r = np.sqrt(xx * xx + yy * yy)
    theta = np.arctan2(yy, xx)

    def band(radius, width):
        return np.clip(1.0 - np.abs(r - radius) / width, 0.0, 1.0) ** 1.5

    arcs = _smooth(0.12, 0.30, np.abs(np.sin(theta * 3.5)))          # seven gaps
    a = band(0.88, 0.035) * arcs + 0.75 * band(0.80, 0.014)
    # Heptagram {7/3}: chords between every third vertex of a heptagon at radius 0.78.
    n, step, rad = 7, 3, 0.78
    for i in range(n):
        a0 = 2.0 * np.pi * i / n + np.pi / 2.0
        a1 = 2.0 * np.pi * ((i + step) % n) / n + np.pi / 2.0
        x0, y0 = rad * np.cos(a0), rad * np.sin(a0)
        x1, y1 = rad * np.cos(a1), rad * np.sin(a1)
        dx, dy = x1 - x0, y1 - y0
        length2 = dx * dx + dy * dy
        tt = np.clip(((xx - x0) * dx + (yy - y0) * dy) / length2, 0.0, 1.0)
        dist = np.sqrt((xx - x0 - tt * dx) ** 2 + (yy - y0 - tt * dy) ** 2)
        a += 0.85 * np.clip(1.0 - dist / 0.018, 0.0, 1.0) ** 1.3
    # Inward thorns from the outer ring, between the star points.
    thorn_phase = np.abs(np.sin(theta * 7.0 + np.pi / 2.0))
    thorn_width = 0.16 * np.clip((r - 0.62) / 0.24, 0.0, 1.0)
    a += 0.8 * np.clip(1.0 - thorn_phase / (thorn_width + 1e-4), 0.0, 1.0) * _smooth(0.62, 0.66, r) * (1.0 - _smooth(0.84, 0.87, r))
    # Rune notches riding a thin inner ring, and a hollow centre eye.
    notches = np.clip(1.0 - np.abs(np.sin(theta * 14.0)) / 0.18, 0.0, 1.0) * np.clip(1.0 - np.abs(r - 0.30) / 0.04, 0.0, 1.0)
    a += 0.7 * band(0.30, 0.012) + 0.6 * notches + 0.8 * band(0.12, 0.03)
    a = np.clip(a, 0.0, 1.0) * (1.0 - _smooth(0.94, 1.0, r))
    rgba = np.ones((size, size, 4))
    rgba[..., 3] = a
    return rgba


SPRITES = {
    "T_Particle_Glow": glow,
    "T_Particle_Beam": beam,
    "T_Particle_Ring": ring,
    "T_Particle_Sigil": sigil,
    "T_Particle_Rays": rays,
    "T_Particle_Feather": feather,
    "T_Particle_Flame": flame,
    "T_Particle_Slash": slash,
    "T_Particle_Smoke": smoke,
    "T_Particle_Crosshair": crosshair,
    "T_Particle_Skull": skull,
    "T_Particle_Wisp": wisp,
    "T_Particle_VoidSigil": void_sigil,
    "T_Particle_FireLick": fire_lick,
}


# Material instances that put a sprite on Particle_Alpha_Tex.hmat. Only the texture differs
# between them; the ATTR chunk (translucent, depth-write off) is what keeps a particle from
# rendering as an opaque quad -- see the particle-authoring notes on Additive.hmat.
MATERIALS = {
    "Particle_Sigil": "T_Particle_Sigil",
    "Particle_Rays": "T_Particle_Rays",
    "Particle_Feather": "T_Particle_Feather",
    "Particle_Flame": "T_Particle_Flame",
    "Particle_Slash": "T_Particle_Slash",
    "Particle_Smoke": "T_Particle_Smoke",
    "Particle_Crosshair": "T_Particle_Crosshair",
    "Particle_Skull": "T_Particle_Skull",
    "Particle_Wisp": "T_Particle_Wisp",
    "Particle_VoidSigil": "T_Particle_VoidSigil",
    "Particle_FireLick": "T_Particle_FireLick",
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
