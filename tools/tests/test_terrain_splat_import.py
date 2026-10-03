# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Integration coverage for terrain_tool's optional lossless splat import.

Requires a built Release terrain_tool, numpy and Pillow. Uses a temporary asset
root exclusively; live world pages are never modified by these tests.
"""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/world"))
from worldkit.formats.tile import parse_tile

TOOL = ROOT / "bin/Release/terrain_tool.exe"


@unittest.skipUnless(TOOL.exists(), "Build Release terrain_tool first")
class TerrainSplatImportTests(unittest.TestCase):
	def setUp(self):
		self.temp = tempfile.TemporaryDirectory()
		self.addCleanup(self.temp.cleanup)
		self.root = Path(self.temp.name)
		world = self.root / "Worlds/Collision"
		world.mkdir(parents=True)
		shutil.copyfile(ROOT / "data/client/Worlds/Collision/Collision.hwld", world / "Collision.hwld")
		self.pages = world / "Collision/Terrain"

	def inputs(self, pages_x=1):
		heights = np.tile(np.arange(pages_x * 128 + 1, dtype=np.uint16), (129, 1))
		Image.fromarray(heights).save(self.root / "height.png")
		meta = {"world": "Collision", "pageRect": {"x0": 34, "z0": 32, "x1": 33 + pages_x, "z1": 32},
			"minY": 0, "maxY": 140, "material": "Models/Terrain/Collision_Rock_PoC.hmi"}
		(self.root / "height.json").write_text(json.dumps(meta))
		z, x = np.mgrid[:1009, :pages_x * 1008 + 1]
		weights = np.zeros((1009, pages_x * 1008 + 1, 4), dtype=np.uint8)
		weights[:, :, 0] = x % 64
		weights[:, :, 1] = z % 64
		weights[:, :, 2] = (x + z) % 64
		weights[:, :, 3] = 255 - weights[:, :, :3].sum(axis=2)
		Image.fromarray(weights).save(self.root / "splat.png")
		return weights

	def run_import(self, splat=True):
		args = [str(TOOL), "import", "--data", str(self.root), "--heightmap", str(self.root / "height.png"),
			"--meta", str(self.root / "height.json")]
		if splat:
			args += ["--splat", str(self.root / "splat.png")]
		return subprocess.run(args, capture_output=True, text=True)

	def test_multi_page_weights_channel_order_and_shared_seam(self):
		weights = self.inputs(2)
		result = self.run_import()
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		left = parse_tile(self.pages / "34_32.tile")
		right = parse_tile(self.pages / "35_32.tile")
		for i, page in enumerate((left, right)):
			expected = weights[:, i * 1008:i * 1008 + 1009].astype(np.uint32)
			packed = sum(expected[:, :, channel] << (channel * 8) for channel in range(4))
			np.testing.assert_array_equal(page.layers, packed)
		np.testing.assert_array_equal(left.layers[:, -1], right.layers[:, 0])
		np.testing.assert_array_equal(left.outer[:, -1], right.outer[:, 0])

	def test_invalid_splats_leave_existing_page_unchanged(self):
		weights = self.inputs()
		self.assertEqual(self.run_import().returncode, 0)
		path = self.pages / "34_32.tile"
		before = path.read_bytes()
		bad_sum = weights.copy()
		bad_sum[4, 7] = 0
		for name, pixels in (("wrong size", weights[:128]), ("RGB", weights[:, :, :3]), ("zero sum", bad_sum)):
			with self.subTest(name=name):
				Image.fromarray(pixels).save(self.root / "splat.png")
				self.assertNotEqual(self.run_import().returncode, 0)
				self.assertEqual(path.read_bytes(), before)

	def test_import_without_splat_keeps_default_base_layer(self):
		self.inputs()
		result = self.run_import(splat=False)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		page = parse_tile(self.pages / "34_32.tile")
		self.assertTrue(np.all(page.layers == 255))


if __name__ == "__main__":
	unittest.main()
