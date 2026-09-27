#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for worldkit spawn flattening, pack detection and placement lint.

	python tools/tests/test_world_lint.py
"""

import random
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures  # noqa: F401,E402

from worldkit.data import load_game_data, load_proto_modules  # noqa: E402
from worldkit.spawns import SpawnRecord, find_packs, is_grid_pack, records_from_npc_draft, spawn_key, unit_spawn_records  # noqa: E402


def rec(x, z, entry=1, name="", y=0.0, kind="unit", active=True):
	return SpawnRecord(spawn_key(kind, 0, entry, name, x, z), 0, kind, entry, 0, 0, name, x, y, z, active, 0, 30000, ())


class SpawnRecordTests(unittest.TestCase):
	def test_key_and_prefix(self):
		record = rec(12.4, -7.6, name="Barrowfield - Barrow Skeleton 07")
		self.assertEqual(record.key, "unit:0:1:Barrowfield - Barrow Skeleton 07:12:-8")
		self.assertEqual(record.poi_prefix, "Barrowfield")
		self.assertIsNone(rec(0, 0, name="Cellar Rat").poi_prefix)

	def test_multi_location_spawn(self):
		mods = load_proto_modules()
		map_entry = mods["maps"].MapEntry(id=0, name="M", directory="M")
		spawn = map_entry.unitspawns.add(unitentry=9, positionx=1, positiony=2, positionz=3, name="A")
		spawn.locations.add(positionx=10, positiony=0, positionz=20)
		spawn.locations.add(positionx=30, positiony=0, positionz=40)
		records = unit_spawn_records(map_entry)
		self.assertEqual([(r.x, r.z, r.location) for r in records], [(10.0, 20.0, 0), (30.0, 40.0, 1)])

	def test_draft_records(self):
		doc = {"unit": {"id": 42, "minlevel": 5, "maxlevel": 6}, "spawns": [
			{"map_id": 0, "spawn": {"unitentry": 42, "name": "Mirewater - Bog Rat 01", "positionx": 1.0,
			 "positiony": 2.0, "positionz": 3.0, "movement": "RANDOM", "respawndelay": "45000"}}]}
		(record,) = records_from_npc_draft(doc)
		self.assertEqual((record.entry, record.movement, record.respawn_delay_ms, record.poi_prefix), (42, 1, 45000, "Mirewater"))

	def test_grid_pack_detection(self):
		grid = [rec(x * 14.0, z * 14.0) for x in range(3) for z in range(3)]
		rng = random.Random(7)
		scatter = [rec(rng.uniform(0, 60), rng.uniform(0, 60)) for _ in range(9)]
		(pack,) = find_packs(grid)
		self.assertTrue(is_grid_pack(pack))
		self.assertFalse(any(is_grid_pack(p) for p in find_packs(scatter)))
		self.assertFalse(is_grid_pack(grid[:4]))  # too small to call a grid

	def test_packs_split_by_entry_and_distance(self):
		packs = find_packs([rec(0, 0), rec(10, 0), rec(500, 0), rec(5, 0, entry=2)])
		self.assertEqual(sorted(len(p) for p in packs), [1, 1, 2])


class GameDataTests(unittest.TestCase):
	def test_loads_live_data(self):
		data = load_game_data()
		self.assertIn(0, data.maps)
		self.assertEqual(data.map_by_directory("Development").id, 0)
		self.assertTrue(data.units)
		levels = data.unit_levels()
		_, (lo, hi) = next(iter(levels.items()))
		self.assertLessEqual(lo, hi)


if __name__ == "__main__":
	unittest.main()
