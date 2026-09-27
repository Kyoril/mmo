#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the atlas bootstrap (placeholder places derived from spawns, givers and props) and the
canon extractor used as bible source material.

	python tools/tests/test_world_bootstrap.py
"""

import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402

from worldkit.atlas import validate  # noqa: E402
from worldkit.bootstrap import bootstrap_atlas, cluster  # noqa: E402
from worldkit.data import GameData, load_proto_modules  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402


class BootstrapTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls._tmp = tempfile.TemporaryDirectory()
		repo = Path(cls._tmp.name)
		fx.make_world(repo, "B", {(32, 32): dict(areas=np.full((16, 16), 8, np.uint32))},
					  entities=[dict(position=(400.0 + i * 3, 0.0, 400.0), unique_id=i + 1) for i in range(6)])
		snapshot = build_snapshot("B", repo=repo)
		m = load_proto_modules()
		zone = m["zones"].ZoneEntry(id=8, name="Briarwatch March")
		haldor = m["units"].UnitEntry(id=1, name="Farmer Haldor", minlevel=5, maxlevel=5)
		haldor.quests.append(10)
		skeleton = m["units"].UnitEntry(id=2, name="Barrow Skeleton", minlevel=8, maxlevel=9)
		quest = m["quests"].QuestEntry(id=10, name="Q", questlevel=6)
		map_entry = m["maps"].MapEntry(id=0, name="Dev", directory="B")
		map_entry.unitspawns.add(unitentry=1, positionx=100, positiony=0, positionz=100)
		for i in range(4):
			map_entry.unitspawns.add(unitentry=2, name=f"Barrowfield - Barrow Skeleton 0{i}", positionx=250 + i * 10, positiony=0, positionz=250)
		cls.data = GameData(units={1: haldor, 2: skeleton}, maps={0: map_entry}, quests={10: quest}, zones={8: zone},
							objects={}, items={}, unit_loot={}, object_loot={}, modules=m)
		cls.atlas = bootstrap_atlas(cls.data, map_entry, WorldQuery(snapshot))

	@classmethod
	def tearDownClass(cls):
		cls._tmp.cleanup()

	def test_valid_and_all_placeholders(self):
		self.assertEqual(validate(self.atlas.doc), [])
		for key in ("zones", "pois", "roads"):
			for entry in self.atlas.doc[key]:
				self.assertEqual(entry["status"], "placeholder")
				self.assertTrue(entry.get("ask"))

	def test_zone_band_from_quests(self):
		zone = self.atlas.zone(8)
		self.assertEqual((zone["name"], zone["levelMin"], zone["levelMax"]), ("Briarwatch March", 6, 6))

	def test_named_group_becomes_place(self):
		poi = self.atlas.poi_by_name("Barrowfield")
		self.assertEqual(poi["kind"], "camp")
		self.assertEqual(poi["center"], [265.0, 250.0])
		self.assertEqual((poi["levelMin"], poi["levelMax"]), (8, 9))

	def test_known_places_props_and_roads(self):
		kinds = {p["kind"] for p in self.atlas.pois}
		self.assertIn("landmark", kinds)
		camp = self.atlas.poi_by_name("Forest Camp")   # anchored on Farmer Haldor
		self.assertEqual(camp["center"], [100.0, 100.0])
		self.assertTrue(self.atlas.poi_by_name("Haven")["ask"].startswith("PARKED"))
		self.assertEqual({r["name"] for r in self.atlas.roads}, {"Westroad", "Northroad"})

	def test_cluster(self):
		groups = cluster([(0, 0, "a"), (10, 0, "b"), (100, 0, "c")], 20.0)
		self.assertEqual(sorted(sorted(g) for g in groups), [["a", "b"], ["c"]])


if __name__ == "__main__":
	unittest.main()
