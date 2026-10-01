#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the dressing formats: .hfol foliage, the .wobj writer, .hmsh meshes and .hwmo world models.

	python tools/tests/test_world_dressing_formats.py
"""

import random
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402

from worldkit.formats.chunks import FormatError  # noqa: E402
from worldkit.formats.hfol import FoliageInstance, append_instances, parse_hfol, remove_instances, write_hfol  # noqa: E402
from worldkit.formats.wobj import entity_file, new_unique_id, parse_wobj, wobj_bytes  # noqa: E402
from worldkit.paths import foliage_dir  # noqa: E402

TREES = ["Models/Trees/A.hmsh", "Models/Trees/B.hmsh"]
IDENTITY = (1.0, 0.0, 0.0, 0.0)


def sample_instances(second_collides=False):
	return [
		(11, 0, (1.0, 2.0, 3.0), IDENTITY, (1.0, 1.0, 1.0), True),
		(12, 1, (4.5, 0.25, -7.0), (0.70710677, 0.0, 0.70710677, 0.0), (1.5, 1.5, 1.5), second_collides),
		(13, 0, (9.0, 1.0, 9.0), IDENTITY, (0.8, 0.8, 0.8), True),
	]


class FoliageFormatTests(unittest.TestCase):
	def test_round_trip_is_byte_identical(self):
		data = fx.hfol_bytes(TREES, sample_instances())
		self.assertEqual(write_hfol(parse_hfol(data)), data)

	def test_parse_fields(self):
		ff = parse_hfol(fx.hfol_bytes(TREES, sample_instances()))
		self.assertEqual(ff.meshes, TREES)
		second = ff.instances[1]
		self.assertEqual((second.unique_id, second.mesh, second.collides), (12, "Models/Trees/B.hmsh", False))
		self.assertAlmostEqual(second.position[0], 4.5)

	def test_version_1_is_upgraded_to_version_2(self):
		ff = parse_hfol(fx.hfol_bytes(TREES, sample_instances(), version=1))
		self.assertTrue(all(i.collides for i in ff.instances))
		self.assertEqual(write_hfol(ff), fx.hfol_bytes(TREES, sample_instances(second_collides=True)))

	def test_append_then_remove_restores_the_original_bytes(self):
		data = fx.hfol_bytes(TREES, sample_instances())
		new = [FoliageInstance(99, "Models/Trees/C.hmsh", (0.0, 0.0, 0.0), IDENTITY, (1.0, 1.0, 1.0)),
			   FoliageInstance(98, "Models/Trees/A.hmsh", (5.0, 0.0, 5.0), IDENTITY, (1.0, 1.0, 1.0))]
		grown = append_instances(parse_hfol(data), new)
		self.assertEqual(grown.meshes, TREES + ["Models/Trees/C.hmsh"])
		self.assertEqual([i.unique_id for i in grown.instances[-2:]], [99, 98])
		self.assertEqual(write_hfol(remove_instances(parse_hfol(write_hfol(grown)), {99, 98})), data)

	def test_remove_keeps_mesh_names_still_in_use(self):
		ff = parse_hfol(fx.hfol_bytes(TREES, sample_instances()))
		self.assertEqual(remove_instances(ff, {11}).meshes, TREES)
		self.assertEqual(remove_instances(ff, {12}).meshes, ["Models/Trees/A.hmsh"])

	def test_duplicate_ids_and_bad_mesh_indices_are_rejected(self):
		ff = parse_hfol(fx.hfol_bytes(TREES, sample_instances()))
		with self.assertRaises(ValueError):
			append_instances(ff, [FoliageInstance(11, "Models/Trees/A.hmsh", (0.0, 0.0, 0.0), IDENTITY, (1.0, 1.0, 1.0))])
		with self.assertRaises(FormatError):
			parse_hfol(fx.hfol_bytes(["A"], [(1, 5, (0.0, 0.0, 0.0), IDENTITY, (1.0, 1.0, 1.0), True)]))

	@fx.requires_live_data
	def test_shipped_foliage_round_trips(self):
		for path in sorted(foliage_dir("Development").glob("*.hfol")):
			data = path.read_bytes()
			ff = parse_hfol(data, str(path))
			if ff.version == 2:
				self.assertEqual(write_hfol(ff), data, path.name)


class EntityWriterTests(unittest.TestCase):
	def test_mesh_round_trip(self):
		with tempfile.TemporaryDirectory() as tmp:
			path = Path(tmp) / "1.wobj"
			path.write_bytes(wobj_bytes(kind="mesh", unique_id=0xABCDEF0123, asset="Models/Desert/Rocks/R.hmsh",
										position=(10.5, 2.25, -3.0), rotation=(0.9238795, 0.0, 0.3826834, 0.0),
										scale=(1.2, 1.2, 1.2), category="dressing/quarry"))
			entity = parse_wobj(path)
		self.assertEqual((entity.kind, entity.unique_id, entity.asset, entity.category, entity.material_overrides),
						 ("mesh", 0xABCDEF0123, "Models/Desert/Rocks/R.hmsh", "dressing/quarry", ()))
		self.assertAlmostEqual(entity.rotation[2], 0.3826834, places=6)
		self.assertAlmostEqual(entity.scale[0], 1.2, places=6)

	def test_layout_matches_the_editor_writer_fixture(self):
		for kind, asset in (("mesh", "Models/Test/Crate.hmsh"), ("wmo", "Models/Mine/Mine_01.hwmo")):
			mine = wobj_bytes(kind=kind, unique_id=7, asset=asset, position=(1.0, 2.0, 3.0), rotation=(1.0, 0.0, 0.0, 0.0),
							  scale=(1.0, 1.0, 1.0), name="Crate", category="Props")
			self.assertEqual(mine, fx.wobj_bytes(kind=kind, unique_id=7, asset=asset, name="Crate", category="Props"), kind)

	def test_entity_file_uses_the_editor_page_folders(self):
		path = entity_file("W", 42, 100.0, 600.0, repo=Path("/r"))
		self.assertEqual(path.name, "42.wobj")
		self.assertEqual(path.parent.name, str((32 << 8) | 33))   # x 100 -> page 32, z 600 -> page 33

	def test_unique_ids_are_fresh_and_nonzero(self):
		taken = {1, 2}
		rng = random.Random(3)
		ids = {new_unique_id(taken, rng) for _ in range(200)}
		self.assertEqual(len(ids), 200)
		self.assertTrue(ids.isdisjoint({0, 1, 2}))
		self.assertTrue(ids <= taken)


if __name__ == "__main__":
	unittest.main()
