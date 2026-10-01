#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the offline rasterizer, asset images and site previews.

	python tools/tests/test_world_dressing_render.py
"""

import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402

from worldkit.assets import GeometryCache, build_catalog  # noqa: E402
from worldkit.foliage import load_world_foliage  # noqa: E402
from worldkit.materials import TextureCache  # noqa: E402
from worldkit.meshrender import Camera, DrawMesh, outline, render  # noqa: E402
from worldkit.paths import client_root  # noqa: E402
from worldkit.previews import asset_views, contact_sheet, site_previews  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402

QUAD = np.array([[-1.0, -1.0, 0.0], [1.0, -1.0, 0.0], [1.0, 1.0, 0.0], [-1.0, 1.0, 0.0]])
QUAD_TRIS = np.array([[0, 1, 2], [0, 2, 3]])
FRONT = Camera(eye=(0.0, 0.0, 5.0), target=(0.0, 0.0, 0.0), width=64, height=64, ortho_height=4.0)


class RasterizerTests(unittest.TestCase):
	def test_quad_covers_the_centre_not_the_corner(self):
		rgb, ids = render([DrawMesh(QUAD, QUAD_TRIS, color=(200, 0, 0), object_id=3)], FRONT)
		self.assertEqual(ids[32, 32], 3)
		self.assertEqual(ids[1, 1], -1)
		self.assertGreater(rgb[32, 32, 0], 50)
		self.assertEqual(rgb[32, 32, 1], 0)
		self.assertEqual(rgb[1, 1].tolist(), [28, 28, 28])

	def test_depth_keeps_the_nearer_quad(self):
		near = DrawMesh(QUAD + [0.0, 0.0, 1.0], QUAD_TRIS, color=(0, 200, 0), object_id=1)
		far = DrawMesh(QUAD, QUAD_TRIS, color=(200, 0, 0), object_id=2)
		_, ids = render([near, far], FRONT)
		self.assertEqual(ids[32, 32], 1)
		_, ids = render([far, near], FRONT)
		self.assertEqual(ids[32, 32], 1)

	def test_texture_quadrants_and_perspective(self):
		texture = np.zeros((2, 2, 4), np.uint8)
		texture[0, 0] = (255, 0, 0, 255)
		texture[0, 1] = (0, 255, 0, 255)
		texture[1, 0] = (0, 0, 255, 255)
		texture[1, 1] = (255, 255, 0, 255)
		uvs = np.array([[0.0, 1.0], [1.0, 1.0], [1.0, 0.0], [0.0, 0.0]])
		camera = Camera(eye=(0.0, 0.0, 4.0), target=(0.0, 0.0, 0.0), width=64, height=64)
		rgb, _ = render([DrawMesh(QUAD, QUAD_TRIS, uvs=uvs, texture=texture)], camera)
		top_left, bottom_right = rgb[26, 26], rgb[38, 38]
		self.assertGreater(top_left[0], top_left[1])        # red texel in the upper left
		self.assertGreater(bottom_right[1], bottom_right[2])  # yellow texel in the lower right

	def test_deterministic_and_outline(self):
		mesh = DrawMesh(QUAD, QUAD_TRIS, color=(100, 100, 100), object_id=7)
		a, ids = render([mesh], FRONT)
		b, _ = render([mesh], FRONT)
		self.assertTrue(np.array_equal(a, b))
		marked = outline(a, ids, {7})
		self.assertTrue((marked == [255, 220, 60]).all(axis=2).any())
		self.assertEqual(marked[32, 32].tolist(), a[32, 32].tolist())


class AssetImageTests(unittest.TestCase):
	def test_sheet_and_views(self):
		with tempfile.TemporaryDirectory() as tmp:
			repo = Path(tmp)
			models = repo / "data" / "client" / "Models" / "Test"
			models.mkdir(parents=True)
			(models / "Cube.hmsh").write_bytes(fx.cube_hmsh())
			catalog = build_catalog(repo)
			geometry, textures = GeometryCache(repo), TextureCache(client_root(repo))
			sheet = contact_sheet(["Models/Test/Cube.hmsh"], catalog, geometry, textures, title="Test")
			views = asset_views("Models/Test/Cube.hmsh", catalog, geometry, textures)
		self.assertGreater(sheet.width, 100)
		self.assertGreater(len(set(np.asarray(sheet.convert("RGB")).reshape(-1, 3)[:, 0].tolist())), 3)
		self.assertEqual(views.width, 3 * 320)


class SitePreviewTests(unittest.TestCase):
	def test_previews_show_the_new_item_outlined(self):
		with tempfile.TemporaryDirectory() as tmp:
			repo = Path(tmp)
			models = repo / "data" / "client" / "Models" / "Test"
			models.mkdir(parents=True)
			(models / "Cube.hmsh").write_bytes(fx.cube_hmsh())
			fx.make_world(repo, "P", {(32, 32): dict(water={0: (1, 0xFFFFFFFFFFFFFFFF)}, water_heights=np.full((129, 129), 1.0, np.float32))},
						  entities=[dict(asset="Models/Test/Cube.hmsh", position=(60.0, 0.0, 50.0), unique_id=9)])
			foliage = repo / "data" / "client" / "Worlds" / "P" / "P" / "Foliage"
			foliage.mkdir(parents=True)
			(foliage / f"{(32 << 8) | 32}.hfol").write_bytes(fx.hfol_bytes(["Models/Test/Cube.hmsh"], [(77, 0, (45.0, 0.0, 45.0), (1.0, 0.0, 0.0, 0.0), (1.0, 1.0, 1.0), True)]))
			snapshot = build_snapshot("P", repo=repo)
			catalog = build_catalog(repo)
			world_foliage = load_world_foliage("P", repo)
			item = {"role": "r", "asset": "Models/Test/Cube.hmsh", "store": "wobj", "position": [50.0, 0.0, 50.0],
					"yaw": 30.0, "tilt": [0.0, 0.0], "scale": 1.5, "collides": True}
			images = site_previews(WorldQuery(snapshot), catalog, GeometryCache(repo), TextureCache(client_root(repo)),
								   (50.0, 50.0), 20.0, snapshot.entities, [i for f in world_foliage.values() for i in f.instances],
								   [item], count=2, size=(320, 200))
		self.assertEqual(len(images), 2)
		label, image = images[0]
		self.assertIn("looking", label)
		pixels = np.asarray(image)
		self.assertTrue((pixels == [255, 220, 60]).all(axis=2).any())   # the new item is outlined


if __name__ == "__main__":
	unittest.main()
