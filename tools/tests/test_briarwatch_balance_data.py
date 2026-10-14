# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Guards the Briarwatch March retune and Fireball rank 2.

* The authoring script finds nothing left to change: the creature values, Fireball ranks and the
  mage trainer entry in the editor data are the ones the design names.
* Fireball rank 2 is tied to the Fireball talent through baseid, and keeps rank 1's family flags so
  the fire talents apply to it.
* ClientDB mirrors the editor spells blob.

Skips rather than fails when protoc or the data submodules are unavailable.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/briarwatch_balance"))


class BriarwatchBalanceDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            import author_briarwatch_balance as abb
            cls.abb = abb
            cls.log, cls.units, cls.spells, cls.trainers = abb.plan_changes()
        except (FileNotFoundError, OSError, SystemExit) as exc:
            raise unittest.SkipTest(f"data or protoc unavailable: {exc}")

    def test_data_matches_the_design(self):
        self.assertEqual(self.log, [])

    def test_fireball_rank_2_continues_the_talent_spell(self):
        rank1 = self.abb.by_id(self.spells, self.abb.FIREBALL)
        rank2 = self.abb.by_id(self.spells, self.abb.FIREBALL_R2)
        self.assertIsNotNone(rank2)
        # Spell 4 belongs to the talent tree script; rank 2 continuing it makes it rank 1.
        self.assertEqual((rank1.rank, rank1.baseid), (1, self.abb.FIREBALL))
        self.assertEqual((rank2.rank, rank2.baseid), (2, self.abb.FIREBALL))
        self.assertEqual(rank2.familyflags, rank1.familyflags)

    def test_client_db_mirrors_editor_spells(self):
        editor = (self.abb.EDITOR / "spells.data").read_bytes()
        client = (self.abb.CLIENT_DB / "spells.data").read_bytes()
        self.assertEqual(editor, client)


if __name__ == "__main__":
    unittest.main()
