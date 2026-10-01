#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the offline rasterizer, asset images and site previews.

	python tools/tests/test_world_dressing_render.py
"""

import math
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402

from worldkit.assets import GeometryCache, build_catalog  # noqa: E402
from worldkit.foliage import load_world_foliage  # noqa: E402
from worldkit.materials import TextureCache, is_masked  # noqa: E402
from worldkit.meshrender import Camera, DrawMesh, outline, render  # noqa: E402
from worldkit.paths import client_root  # noqa: E402
from worldkit import previews  # noqa: E402
from worldkit.previews import _compass, asset_views, contact_sheet, site_meshes, site_previews, terrain_meshes  # noqa: E402
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

	def test_alpha_test_drops_transparent_texels(self):
		texture = np.full((2, 2, 4), 255, np.uint8)
		texture[:, 0, 3] = 0                                    # left half fully transparent
		uvs = np.array([[0.0, 1.0], [1.0, 1.0], [1.0, 0.0], [0.0, 0.0]])
		cut = DrawMesh(QUAD, QUAD_TRIS, uvs=uvs, texture=texture, object_id=5, alpha_test=True)
		rgb, ids = render([cut], FRONT, background=(9, 9, 9))
		self.assertEqual(ids[32, 24], -1)                       # transparent half leaves the background...
		self.assertEqual(rgb[32, 24].tolist(), [9, 9, 9])
		self.assertEqual(ids[32, 40], 5)                        # ...the opaque half is drawn
		_, ids = render([DrawMesh(QUAD, QUAD_TRIS, uvs=uvs, texture=texture, object_id=5)], FRONT)
		self.assertEqual(ids[32, 24], 5)                        # without alpha_test it is an opaque square

	def test_alpha_tested_texels_do_not_write_depth(self):
		texture = np.full((2, 2, 4), 255, np.uint8)
		texture[:, 0, 3] = 0
		uvs = np.array([[0.0, 1.0], [1.0, 1.0], [1.0, 0.0], [0.0, 0.0]])
		leaf = DrawMesh(QUAD + [0.0, 0.0, 1.0], QUAD_TRIS, uvs=uvs, texture=texture, object_id=1, alpha_test=True)
		behind = DrawMesh(QUAD, QUAD_TRIS, color=(0, 200, 0), object_id=2)
		_, ids = render([leaf, behind], FRONT)
		self.assertEqual(ids[32, 24], 2)                        # visible through the cut-out
		self.assertEqual(ids[32, 40], 1)

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


class CompassTests(unittest.TestCase):
	def test_labels_for_known_directions(self):
		self.assertEqual(_compass(0.0, -1.0), "north")           # looking along -Z
		self.assertEqual(_compass(1.0, 0.0), "east")             # looking along +X
		self.assertEqual(_compass(0.0, 1.0), "south")            # looking along +Z
		self.assertEqual(_compass(-1.0, 0.0), "west")
		self.assertEqual(_compass(1.0, -1.0), "north-east")
		self.assertEqual(_compass(-1.0, 1.0), "south-west")


class MaskedMaterialTests(unittest.TestCase):
	@staticmethod
	def _parser(types):
		"""A fake material parser: file name -> (material type or None, parent)."""
		def parse(path, _source):
			material_type, parent = types[path.name]
			info = {"parent": parent}
			if material_type is not None:
				info["attributes"] = {"material_type": material_type}
			return info, [], b""
		return parse

	def test_masked_type_is_found_on_the_instance_or_nearest_parent(self):
		parse = self._parser({"leaf.hmat": ("Masked", None), "bark.hmat": ("Opaque", None),
							  "leaf.hmi": (None, "leaf.hmat"), "bark.hmi": ("Opaque", "leaf.hmat")})
		anywhere = lambda path: True
		client = Path("client")
		self.assertTrue(is_masked("leaf.hmat", client, parse, anywhere))
		self.assertTrue(is_masked("leaf.hmi", client, parse, anywhere))      # inherits Masked from its parent
		self.assertFalse(is_masked("bark.hmat", client, parse, anywhere))
		self.assertFalse(is_masked("bark.hmi", client, parse, anywhere))     # own ATTR wins over the parent
		self.assertFalse(is_masked("missing.hmi", client, parse, lambda path: False))


class SitePreviewTests(unittest.TestCase):
	def _build(self, tmp):
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
		world_foliage = load_world_foliage("P", repo)
		return (snapshot, WorldQuery(snapshot), build_catalog(repo), GeometryCache(repo), TextureCache(client_root(repo)),
				[i for f in world_foliage.values() for i in f.instances])

	@staticmethod
	def _item(**extra):
		item = {"role": "r", "asset": "Models/Test/Cube.hmsh", "store": "wobj", "position": [50.0, 0.0, 50.0],
				"yaw": 30.0, "tilt": [0.0, 0.0], "scale": 1.5, "collides": True}
		item.update(extra)
		return item

	def test_previews_show_the_new_item_outlined(self):
		with tempfile.TemporaryDirectory() as tmp:
			snapshot, query, catalog, geometry, textures, instances = self._build(tmp)
			images = site_previews(query, catalog, geometry, textures, (50.0, 50.0), 20.0, snapshot.entities, instances,
								   [self._item()], count=2, size=(320, 200))
		self.assertEqual(len(images), 2)
		label, image = images[0]
		self.assertIn("looking", label)
		pixels = np.asarray(image)
		self.assertTrue((pixels == [255, 220, 60]).all(axis=2).any())   # the new item is outlined

	def test_terrain_and_water_meshes(self):
		with tempfile.TemporaryDirectory() as tmp:
			_, query, *_ = self._build(tmp)
			dry = {m.object_id: m for m in terrain_meshes(query, 50.0, 50.0, 10.0)}
			wet = {m.object_id: m for m in terrain_meshes(query, 5.0, 5.0, 10.0)}
		self.assertGreater(len(dry[-2].indices), 0)                      # ground
		self.assertNotIn(-3, dry)                                        # no water away from the water chunk
		self.assertIn(-2, wet)
		self.assertIn(-3, wet)
		water = wet[-3].positions
		self.assertTrue(np.allclose(water[:, 1], 1.0))                   # ground 0 + depth 1
		self.assertLessEqual(float(water[:, 0].max()), 34.0)            # confined to the 33.3 m water chunk
		self.assertLessEqual(float(water[:, 2].max()), 34.0)

	def test_applied_ids_are_not_drawn_twice(self):
		with tempfile.TemporaryDirectory() as tmp:
			snapshot, query, catalog, geometry, textures, instances = self._build(tmp)
			self.assertEqual([e.unique_id for e in snapshot.entities], [9])
			self.assertEqual([i.unique_id for i in instances], [77])

			def ids_drawn(items):
				meshes, _ = site_meshes(query, catalog, geometry, textures, (50.0, 50.0), 20.0, snapshot.entities, instances, items)
				return {m.object_id for m in meshes}

			plain = ids_drawn([self._item()])
			self.assertIn(-4, plain)                                     # existing entity drawn
			self.assertIn(-5, plain)                                     # existing foliage instance drawn
			self.assertIn(-2, plain)                                     # terrain
			applied_entity = ids_drawn([self._item(unique_id="0x9")])
			self.assertNotIn(-4, applied_entity)
			self.assertIn(-5, applied_entity)
			applied_foliage = ids_drawn([self._item(unique_id="0x4d")])
			self.assertNotIn(-5, applied_foliage)
			self.assertIn(-4, applied_foliage)
			self.assertNotIn(-4, ids_drawn([self._item(unique_id=9)]))   # integer ids work too
			self.assertIn(1000, applied_entity)                          # the item itself is still drawn

	def test_camera_stays_above_the_ground_at_the_eye(self):
		class Hill:
			"""Flat at the site, a 30 m plateau further out where the cameras stand."""
			def height_at(self, x, z):
				return 0.0 if math.hypot(x - 50.0, z - 50.0) < 25.0 else 30.0

			def hole_at(self, x, z):
				return False

			def terrain_kind_at(self, x, z):
				return "grass"

			def water_depth_at(self, x, z):
				return 0.0

		cameras = []
		real_render = previews.render

		def spy(meshes, camera, background=(28, 28, 28)):
			cameras.append(camera)
			return real_render(meshes, camera, background=background)

		with mock.patch.object(previews, "render", spy):
			site_previews(Hill(), {}, None, None, (50.0, 50.0), 20.0, [], [], [], count=3, size=(32, 20))
		self.assertEqual(len(cameras), 3)
		for camera in cameras:
			self.assertGreaterEqual(camera.eye[1], 32.0 - 1e-6)          # 30 m ground + 2 m clearance, not 0 + 0.9 * radius


if __name__ == "__main__":
	unittest.main()
