#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for worldkit spawn flattening, pack detection and placement lint.

	python tools/tests/test_world_lint.py
"""

import json
import random
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures  # noqa: E402

from worldkit.atlas import empty_atlas  # noqa: E402
from worldkit.baseline import load_baseline, save_baseline, split  # noqa: E402
from worldkit.constants import CELL_SIZE  # noqa: E402
from worldkit.data import load_game_data, load_proto_modules  # noqa: E402
from worldkit.lint import lint_records, naming_violations  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402
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


def lint_world(repo: Path):
	outer = np.zeros((129, 129), np.float32)
	outer[:, 100:] = (np.arange(29, dtype=np.float32) * CELL_SIZE * 2.0)[None, :]  # steep far-east strip
	inner = ((outer[:-1, :-1] + outer[:-1, 1:] + outer[1:, :-1] + outer[1:, 1:]) * 0.25).astype(np.float32)
	world_fixtures.make_world(repo, "L", {(32, 32): dict(outer=outer, inner=inner, areas=np.full((16, 16), 8, np.uint32))})
	return build_snapshot("L", repo=repo)


class LintTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls._tmp = tempfile.TemporaryDirectory()
		snapshot = lint_world(Path(cls._tmp.name))
		atlas = empty_atlas(0)
		atlas.add_zone(areaId=8, name="Briarwatch March", levelMin=5, levelMax=10, status="placeholder")
		atlas.add_poi(name="Barrowfield", kind="camp", center=[100.0, 100.0], radius=40.0, levelMin=8, levelMax=9,
					  status="placeholder", source="agent")
		cls.query = WorldQuery(snapshot, atlas=atlas)

	@classmethod
	def tearDownClass(cls):
		cls._tmp.cleanup()

	def rules(self, records, levels=None):
		return {(v.rule, v.severity) for v in lint_records(records, self.query, levels or {})}

	def test_clean_spawn(self):
		self.assertEqual(self.rules([rec(200.0, 200.0, y=0.2)]), set())

	def test_height_delta(self):
		self.assertEqual(self.rules([rec(200.0, 200.0, y=-3.0)]), {("height_delta", "error")})
		self.assertEqual(self.rules([rec(200.0, 200.0, y=1.0)]), {("height_delta", "warning")})

	def test_slope_edge_and_no_terrain(self):
		self.assertIn(("steep_slope", "error"), self.rules([rec(480.0, 200.0, y=self.query.height_at(480.0, 200.0))]))
		self.assertIn(("terrain_edge", "error"), self.rules([rec(2.0, 200.0)]))
		self.assertEqual(self.rules([rec(-50.0, 200.0)]), {("no_terrain", "error")})

	def test_grid_pack(self):
		grid = [rec(150 + x * 14.0, 150 + z * 14.0) for x in range(3) for z in range(3)]
		self.assertIn(("grid_pattern", "warning"), self.rules(grid))

	def test_poi_and_level_band(self):
		inside = rec(100.0, 100.0, entry=7, name="Barrowfield - Barrow Skeleton 01")
		outside = rec(300.0, 300.0, entry=7, name="Barrowfield - Barrow Skeleton 02")
		self.assertEqual(self.rules([inside], {7: (8, 9)}), set())
		self.assertIn(("outside_poi", "warning"), self.rules([outside], {7: (8, 9)}))
		self.assertIn(("level_band", "warning"), self.rules([inside], {7: (2, 3)}))
		self.assertIn(("level_band", "warning"), self.rules([outside], {7: (20, 21)}))  # zone band 5-10
		# A level-10 trainer inside a level 8-9 camp is fine: bands describe what players fight.
		service = lint_records([inside], self.query, {7: (10, 10)}, service_units={7})
		self.assertEqual([v.rule for v in service], [])

	def test_naming(self):
		self.assertEqual([v.rule for v in naming_violations([rec(0, 0, name="Bog Rat")])], ["spawn_name"])
		self.assertEqual(naming_violations([rec(0, 0, name="Mirewater - Bog Rat 01")]), [])


class BaselineTests(unittest.TestCase):
	def test_sections_and_maps_are_independent(self):
		class Item:
			def __init__(self, key, message="m"):
				self.key, self.message = key, message
		with tempfile.TemporaryDirectory() as tmp:
			path = Path(tmp) / "baseline.json"
			save_baseline("placement", 0, [Item("a|unit:0:1"), Item("b|unit:0:2")], path)
			save_baseline("placement", 2, [Item("a|unit:2:1")], path)
			save_baseline("reachability", 0, [Item("no_turn_in|quest:5")], path)
			save_baseline("placement", 0, [Item("a|unit:0:1")], path)   # map 0 re-baselined, one fixed
			placement = load_baseline("placement", path)
			self.assertEqual(set(placement), {"a|unit:0:1", "a|unit:2:1"})
			self.assertEqual(set(load_baseline("reachability", path)), {"no_turn_in|quest:5"})
			new, known = split([Item("a|unit:0:1"), Item("c|unit:0:9")], placement)
			self.assertEqual(([i.key for i in new], [i.key for i in known]), (["c|unit:0:9"], ["a|unit:0:1"]))
			self.assertEqual(json.loads(path.read_text())["version"], 1)


class SkillCheckTests(unittest.TestCase):
	def test_npc_draft_on_real_map(self):
		from worldkit.skill_checks import npc_draft_findings
		doc = {"unit": {"id": 999001, "name": "Test Rat", "minlevel": 5, "maxlevel": 6}, "spawns": [
			{"map_id": 0, "spawn": {"unitentry": 999001, "name": "Bog Rat", "positionx": -330.0, "positiony": -500.0, "positionz": 230.0}}]}
		errors, warnings = npc_draft_findings(doc)
		self.assertTrue(any("below the ground" in e for e in errors), errors)
		self.assertTrue(any("spawn name" in w for w in warnings), warnings)

	def test_quest_draft_without_level_is_quiet(self):
		from worldkit.skill_checks import quest_draft_warnings
		self.assertEqual(quest_draft_warnings({"quest": {"id": 1}}), [])


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
