#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""End-to-end test of a dressing pass on a synthetic world: plan, check, apply, packet, undo.

	python tools/tests/test_world_dressing_commands.py
"""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402
from test_world_dressing_lint import RULES, lint_world  # noqa: E402
from test_world_dressing_templates import TEMPLATE  # noqa: E402

from worldkit.assets import build_catalog  # noqa: E402
from worldkit.atlas import empty_atlas  # noqa: E402
from worldkit.dress_commands import DressSession, check_pass, plan_pass  # noqa: E402
from worldkit.dressing import apply_pass, undo_pass  # noqa: E402
from worldkit.foliage import load_world_foliage  # noqa: E402
from worldkit.packet import write_packet  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402
from worldkit.terrain_kinds import TerrainKinds  # noqa: E402


class MapEntry:
	id, name, directory, instancetype = 0, "Lint World", "L", 0


class CommandTests(unittest.TestCase):
	def test_plan_check_apply_packet_undo(self):
		import json
		with tempfile.TemporaryDirectory() as tmp:
			repo = Path(tmp)
			lint_world(repo)
			folder = repo / "tools" / "world" / "templates"
			folder.mkdir(parents=True)
			(folder / "t.json").write_text(json.dumps(TEMPLATE), encoding="utf-8")
			atlas = empty_atlas(0)
			atlas.add_poi(id="site", name="Site", kind="camp", center=[50.0, 60.0], radius=30.0, status="canon", source="user")
			query = WorldQuery(build_snapshot("L", repo=repo), atlas=atlas, kinds=TerrainKinds({"Models/Terrain/B.hmi": ["grass", "road", "rock", "dirt"]}))
			session = DressSession(MapEntry(), query, build_catalog(repo), RULES, load_world_foliage("L", repo), [], repo)
			doc = plan_pass(session, "site", "t", seed=4, render=False)
			self.assertEqual(doc["status"], "planned")
			self.assertTrue(doc["items"])
			self.assertEqual([v for v in doc["checks"]["placement"] if v["severity"] == "error"], [])
			doc = check_pass(session, doc, render=False)
			doc = apply_pass(doc, repo, probe=lambda: False)
			packet = write_packet(doc, {}, repo)
			readme = (packet / "README.md").read_text(encoding="utf-8")
			self.assertIn("dress.py undo", readme)
			self.assertIn("nothing", readme)          # the asset gap is listed
			self.assertEqual(len(undo_pass(doc, repo, probe=lambda: False).removed), len(doc["items"]))


if __name__ == "__main__":
	unittest.main()
