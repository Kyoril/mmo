#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for geometry helpers, texture resolution, asset tags and the asset catalog.

	python tools/tests/test_world_dressing_assets.py
"""

import math
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402

from worldkit.assets import build_catalog, select_assets  # noqa: E402
from worldkit.geometry import quat_from_yaw_tilt, quat_to_matrix, trs_matrix, yaw_of_quat  # noqa: E402
from worldkit.materials import load_texture, resolve_base_texture  # noqa: E402
from worldkit.paths import client_root  # noqa: E402
from worldkit.tags import TagRules, load_tag_rules  # noqa: E402


class GeometryTests(unittest.TestCase):
	def test_yaw_round_trip_and_forward(self):
		for yaw in (0.0, 30.0, 135.0, -60.0):
			q = quat_from_yaw_tilt(yaw)
			self.assertAlmostEqual((yaw_of_quat(q) - yaw + 180.0) % 360.0 - 180.0, 0.0, places=4)
		forward = quat_to_matrix(quat_from_yaw_tilt(90.0)) @ np.array([0.0, 0.0, 1.0])
		self.assertTrue(np.allclose(forward, [1.0, 0.0, 0.0], atol=1e-6))   # yaw 90 faces +X (east)

	def test_tilt_maps_up_to_the_terrain_normal(self):
		normal = np.array([-0.3, 1.0, 0.2])
		normal /= np.linalg.norm(normal)
		roll = -math.degrees(math.asin(normal[0]))
		pitch = math.degrees(math.atan2(normal[2], normal[1]))
		up = quat_to_matrix(quat_from_yaw_tilt(40.0, pitch, roll)) @ np.array([0.0, 1.0, 0.0])
		self.assertTrue(np.allclose(up, normal, atol=1e-6))

	def test_trs_matrix(self):
		m = trs_matrix((10.0, 0.0, 5.0), quat_from_yaw_tilt(90.0), (2.0, 2.0, 2.0))
		self.assertTrue(np.allclose(m @ np.array([0.0, 0.0, 1.0, 1.0]), [12.0, 0.0, 5.0, 1.0], atol=1e-6))


class MaterialTests(unittest.TestCase):
	def test_override_beats_parent_and_names_are_ranked(self):
		materials = {
			"Mat/Child.hmi": {"parent": "Mat/Base.hmat", "textures": [], "texture_parameters": [{"name": "BaseColor", "texture": "Tex/Child_D.htex"}]},
			"Mat/Base.hmat": {"parent": None, "textures": ["Tex/Direct.htex"], "texture_parameters": [
				{"name": "Normal", "texture": "Tex/N.htex"}, {"name": "BaseColor", "texture": "Tex/Base_D.htex"}]},
			"Mat/Plain.hmat": {"parent": None, "textures": ["Tex\\Direct.htex"], "texture_parameters": []},
			"Mat/Hinted.hmat": {"parent": None, "textures": [], "texture_parameters": [{"name": "Grass_BaseColor", "texture": "Tex/G.htex"}]},
		}

		def parse(path, _):
			key = path.relative_to(Path("/c")).as_posix()
			return materials[key], [], b""

		client = Path("/c")
		exists = lambda path: path.relative_to(client).as_posix() in materials  # noqa: E731
		self.assertEqual(resolve_base_texture("Mat/Child.hmi", client, parse=parse, exists=exists), "Tex/Child_D.htex")
		self.assertEqual(resolve_base_texture("Mat/Base.hmat", client, parse=parse, exists=exists), "Tex/Base_D.htex")
		self.assertEqual(resolve_base_texture("Mat/Plain.hmat", client, parse=parse, exists=exists), "Tex/Direct.htex")
		self.assertEqual(resolve_base_texture("Mat/Hinted.hmat", client, parse=parse, exists=exists), "Tex/G.htex")
		self.assertIsNone(resolve_base_texture("Mat/Missing.hmi", client, parse=parse, exists=exists))

	def test_load_uncompressed_texture(self):
		pixels = np.zeros((2, 2, 4), np.uint8)
		pixels[0, 1] = (255, 0, 0, 255)
		with tempfile.TemporaryDirectory() as tmp:
			(Path(tmp) / "t.htex").write_bytes(fx.htex_rgba_bytes(pixels))
			image = load_texture("t.htex", Path(tmp))
		self.assertEqual(image.shape, (2, 2, 4))
		self.assertEqual(image[0, 1].tolist(), [255, 0, 0, 255])

	@fx.requires_live_data
	def test_shipped_tent_material(self):
		texture = resolve_base_texture("Textures/FalwynPlains/Props/CampTent.hmi", client_root())
		self.assertEqual(texture, "Textures/FalwynPlains/Props/Camp/T_hc_CampTent_D.htex")
		image = load_texture(texture, client_root())
		self.assertIsNotNone(image)
		self.assertEqual(image.shape[2], 4)


def asset_repo(tmp: str) -> Path:
	repo = Path(tmp)
	models = repo / "data" / "client" / "Models" / "Test"
	models.mkdir(parents=True)
	(models / "Cube.hmsh").write_bytes(fx.cube_hmsh())
	(models / "BigRock.hmsh").write_bytes(fx.cube_hmsh(collision=False, size=2.5))
	(models / "Shed.hwmo").write_bytes(fx.hwmo_bytes((-4.0, 0.0, -3.0), (4.0, 5.0, 3.0),
		[("Models/Test/Cube.hmsh", (0.0, 0.0, 0.0), (1.0, 0.0, 0.0, 0.0), (1.0, 1.0, 1.0))]))
	(models / "Broken.hmsh").write_bytes(b"MESH\x00\x01")
	return repo


class TagRuleTests(unittest.TestCase):
	def test_later_rules_override_and_tags_accumulate(self):
		rules = TagRules([
			{"match": "Models/Desert/Rocks/*", "tags": ["rock"], "sink": 0.4, "max_slope": 90, "may_overlap": ["rock"]},
			{"match": "*/SM_Rock_Cliff*", "tags": ["cliff_rock"], "sink": 0.8},
		])
		tags = rules.for_asset("Models/Desert/Rocks/SM_Rock_Cliff01.hmsh")
		self.assertEqual(tags.tags, frozenset({"rock", "cliff_rock"}))
		self.assertEqual((tags.sink, tags.max_slope, tags.may_overlap), (0.8, 90, frozenset({"rock"})))
		self.assertEqual(rules.for_asset("Models/Other/X.hmsh").tags, frozenset())

	def test_unknown_tags_and_fields_are_rejected(self):
		with self.assertRaises(ValueError):
			TagRules([{"match": "*", "tags": ["spaceship"]}])
		with self.assertRaises(ValueError):
			TagRules([{"match": "*", "colour": "red"}])

	def test_shipped_rules_load(self):
		self.assertTrue(load_tag_rules().rules)


class CatalogTests(unittest.TestCase):
	def setUp(self):
		self._tmp = tempfile.TemporaryDirectory()
		self.repo = asset_repo(self._tmp.name)

	def tearDown(self):
		self._tmp.cleanup()

	def test_entries(self):
		catalog = build_catalog(self.repo)
		cube = catalog["Models/Test/Cube.hmsh"]
		self.assertEqual((cube.kind, cube.has_collision, cube.submeshes, cube.size_class), ("mesh", True, 1, "medium"))
		self.assertEqual((cube.height, cube.base_offset, cube.footprint), (2.0, 0.0, (-1.0, -1.0, 1.0, 1.0)))
		self.assertEqual(catalog["Models/Test/BigRock.hmsh"].size_class, "large")
		shed = catalog["Models/Test/Shed.hwmo"]
		self.assertEqual((shed.kind, shed.references, shed.has_collision), ("wmo", ("Models/Test/Cube.hmsh",), True))
		self.assertIsNotNone(catalog["Models/Test/Broken.hmsh"].error)

	def test_cache_is_reused_and_invalidated(self):
		build_catalog(self.repo)
		path = self.repo / "data" / "client" / "Models" / "Test" / "Cube.hmsh"
		path.write_bytes(fx.cube_hmsh(size=0.5, collision=False))   # different size, so the cache stamp changes
		entry = build_catalog(self.repo)["Models/Test/Cube.hmsh"]
		self.assertEqual((entry.size_class, entry.has_collision), ("small", False))

	def test_select(self):
		catalog = build_catalog(self.repo)
		rules = TagRules([{"match": "Models/Test/*", "tags": ["rock"]}, {"match": "*/Shed*", "tags": ["building"]},
						  {"match": "*/BigRock*", "tags": ["prototype"]}])
		self.assertEqual(select_assets(catalog, rules, tags=["rock"], exclude=["building"]), ["Models/Test/Cube.hmsh"])
		self.assertEqual(select_assets(catalog, rules, tags=["rock"], exclude=["building"], size="large"), [])   # BigRock is a prototype
		self.assertEqual(select_assets(catalog, rules, allow=["Models/Test/BigRock.hmsh"]), ["Models/Test/BigRock.hmsh"])

	def test_characters_and_markers_are_never_picked_as_scenery(self):
		catalog = build_catalog(self.repo)
		rules = TagRules([{"match": "Models/Test/Cube*", "tags": ["rock", "character"]},
						  {"match": "Models/Test/BigRock*", "tags": ["rock", "marker"]}])
		self.assertEqual(select_assets(catalog, rules, tags=["rock"]), [])
		self.assertEqual(select_assets(catalog, rules, tags=["rock", "character"]), ["Models/Test/Cube.hmsh"])
		self.assertEqual(select_assets(catalog, rules, tags=["marker"]), ["Models/Test/BigRock.hmsh"])


if __name__ == "__main__":
	unittest.main()
