# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Generate the moon disc drawn by Models/Sky.hmat and import it as Textures/T_Sky_Moon.htex.

A full moon: soft dark maria from layered value noise, a few rayed craters, gentle limb darkening.
RGB is premultiplied by the disc alpha, so the sky can add it without any blending and the corners
of the texture stay black.

Usage:
  python tools/sky_moon/make_moon.py [--keep-png]

Requires texconv.exe through the item icon importer, like tools/particle_gen/make_sprites.py.
"""

from __future__ import annotations

import os
import subprocess
import sys

import numpy as np
from PIL import Image

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT_DIR = os.path.join(REPO_ROOT, "data", "client", "Textures")
IMPORTER = os.path.join(REPO_ROOT, ".claude", "skills", "mmo-item-designer", "scripts", "import_item_icon.py")
SIZE = 512


def value_noise(rng, size, cells):
	grid = rng.random((cells + 1, cells + 1))
	coords = np.linspace(0.0, cells, size, endpoint=False)
	i = coords.astype(int)
	f = coords - i
	f = f * f * (3.0 - 2.0 * f)
	x0, x1 = i[None, :], i[None, :] + 1
	y0, y1 = i[:, None], i[:, None] + 1
	fx, fy = f[None, :], f[:, None]
	top = grid[y0, x0] * (1.0 - fx) + grid[y0, x1] * fx
	bottom = grid[y1, x0] * (1.0 - fx) + grid[y1, x1] * fx
	return top * (1.0 - fy) + bottom * fy


def fbm(rng, size, base_cells, octaves):
	total = np.zeros((size, size))
	amplitude, weight = 1.0, 0.0
	for octave in range(octaves):
		total += value_noise(rng, size, base_cells * 2 ** octave) * amplitude
		weight += amplitude
		amplitude *= 0.5
	return total / weight


def smoothstep(edge0, edge1, x):
	t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
	return t * t * (3.0 - 2.0 * t)


def moon():
	rng = np.random.default_rng(7)
	axis = (np.arange(SIZE) + 0.5) / SIZE * 2.0 - 1.0
	x, y = np.meshgrid(axis, axis)
	r = np.sqrt(x * x + y * y)
	radius = 0.96

	# Maria: large soft dark patches, biased to the upper left like the familiar near side.
	maria = smoothstep(0.5, 0.68, fbm(rng, SIZE, 3, 4) + 0.12 * (-x - y) * 0.5)
	albedo = 0.92 - 0.32 * maria

	# Fine surface grain.
	albedo *= 0.9 + 0.1 * fbm(rng, SIZE, 24, 3)

	# Craters: darker floor, a rim lit from the upper left and shadowed opposite.
	for index in range(18):
		cx, cy = rng.uniform(-0.8, 0.8, 2)
		cr = rng.uniform(0.02, 0.07)
		dx, dy = (x - cx) / cr, (y - cy) / cr
		d = np.sqrt(dx * dx + dy * dy)
		lit = (-dx - dy) / np.maximum(d, 1e-4) * 0.7071
		ring = np.exp(-((d - 1.0) * 3.0) ** 2)
		albedo *= 1.0 - 0.07 * smoothstep(1.0, 0.5, d)
		albedo += 0.05 * ring * lit

	# One young crater with soft bright ejecta, like Tycho.
	cx, cy = 0.18, 0.55
	d = np.sqrt((x - cx) ** 2 + (y - cy) ** 2) / 0.04
	angle = np.arctan2(y - cy, x - cx)
	rays = 0.5 + 0.5 * np.cos(angle * 9.0 + 1.3) * np.cos(angle * 4.0)
	albedo += 0.05 * rays * np.exp(-np.maximum(d - 1.0, 0.0) * 0.18) * smoothstep(0.8, 1.4, d)
	albedo += 0.06 * np.exp(-((d - 1.0) * 2.5) ** 2)

	# Limb darkening on a sphere.
	mu = np.sqrt(np.clip(1.0 - (r / radius) ** 2, 0.0, 1.0))
	shade = 0.72 + 0.28 * mu
	value = np.clip(albedo * shade, 0.0, 1.0)

	alpha = smoothstep(radius, radius - 0.012, r)
	rgb = np.stack([value * 0.97, value * 0.99, value * 1.0], axis=-1) * alpha[..., None]
	return np.concatenate([rgb, alpha[..., None]], axis=-1)


def main():
	png = os.path.join(OUT_DIR, "T_Sky_Moon.png")
	htex = os.path.join(OUT_DIR, "T_Sky_Moon.htex")
	Image.fromarray((np.clip(moon(), 0.0, 1.0) * 255).astype(np.uint8), "RGBA").save(png)
	subprocess.run([sys.executable, IMPORTER, png, htex, "--format", "dxt5"], check=True)
	if "--keep-png" not in sys.argv:
		os.remove(png)
	print("wrote %s" % htex)


if __name__ == "__main__":
	main()
