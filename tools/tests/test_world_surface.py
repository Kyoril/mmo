#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for worldkit.surface: height/slope of the rendered terrain surface.

The key case is test_raised_inner_vertex_is_not_averaged: a sculpted inner vertex must show up in
the sampled height. Averaging the corners would put the spawn on a surface the renderer never draws.

	python tools/tests/test_world_surface.py
"""

import math
import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures  # noqa: F401,E402  (puts tools/world on sys.path)

from worldkit.constants import CELL_SIZE  # noqa: E402
from worldkit.surface import max_cell_slopes, sample  # noqa: E402


def tilted_page():
	"""Height equals page-local x (a 45 degree ramp rising along +x)."""
	xs = np.arange(129, dtype=np.float32) * CELL_SIZE
	outer = np.tile(xs, (129, 1))
	inner = np.tile((np.arange(128, dtype=np.float32) + 0.5) * CELL_SIZE, (128, 1))
	return outer, inner


class SurfaceTests(unittest.TestCase):
	def test_flat(self):
		outer = np.full((129, 129), 2.0, np.float32)
		inner = np.full((128, 128), 2.0, np.float32)
		height, slope = sample(outer, inner, 100.0, 200.0)
		self.assertAlmostEqual(height, 2.0, places=5)
		self.assertAlmostEqual(slope, 0.0, places=5)

	def test_tilted_plane(self):
		outer, inner = tilted_page()
		for lx in (0.0, 13.7, 250.1, 533.0):
			height, slope = sample(outer, inner, lx, 77.7)
			self.assertAlmostEqual(height, lx, places=2)
			self.assertAlmostEqual(slope, 45.0, places=2)

	def test_raised_inner_vertex_is_not_averaged(self):
		outer = np.zeros((129, 129), np.float32)
		inner = np.zeros((128, 128), np.float32)
		inner[0, 0] = 4.0
		centre, _ = sample(outer, inner, 0.5 * CELL_SIZE, 0.5 * CELL_SIZE)
		self.assertAlmostEqual(centre, 4.0, places=5)
		# (u=0.25, v=0.5) lies in the left triangle, where the plane rises 8 m per cell width in u
		left, slope = sample(outer, inner, 0.25 * CELL_SIZE, 0.5 * CELL_SIZE)
		self.assertAlmostEqual(left, 2.0, places=5)
		self.assertAlmostEqual(slope, math.degrees(math.atan(8.0 / CELL_SIZE)), places=4)

	def test_max_cell_slopes(self):
		outer, inner = tilted_page()
		slopes = max_cell_slopes(outer, inner)
		self.assertEqual(slopes.shape, (128, 128))
		self.assertTrue(np.allclose(slopes, 45.0, atol=0.01))

	def test_clamps_to_page(self):
		outer, inner = tilted_page()
		height, _ = sample(outer, inner, 9999.0, -5.0)
		self.assertAlmostEqual(height, 128 * CELL_SIZE, places=2)


if __name__ == "__main__":
	unittest.main()
