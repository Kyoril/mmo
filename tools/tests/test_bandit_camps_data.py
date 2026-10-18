# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Guards the Forest Bandit camps (Forest Bandit grounds near Oakenshire and at (-101, 137)).

* The Bandit Mage casts its own Frostbolt (275): an NPC-only copy of the player spell with no rank,
  no Ability flag, less damage and a cooldown, while the player's Frostbolt (3) keeps its numbers.
* Both bandit units drop the Red Armbands quest loot, so mixed spawn points keep quest 24 working
  whichever unit they roll.
* Camp spawn points mostly roll Forest Bandits; mages stay the rarer unit.
* Packs are spaced beyond the 8 m call-for-help radius (CreatureAI::CallForHelp), so a pull brings
  its own pack only, and Lina's crates (quest 9) are within aggro reach of at most one camp guard.
* ClientDB mirrors the editor spells blob.

Skips rather than fails when protoc or the data submodules are unavailable.
"""

import math
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-npc-designer/scripts"))

EDITOR = ROOT / "data/editor/data"
CLIENT_DB = ROOT / "data/client/ClientDB"

FOREST_BANDIT = 37
BANDIT_MAGE = 39
PLAYER_FROSTBOLT = 3
BANDIT_FROSTBOLT = 275
ARMBAND_LOOT = 21
ABILITY = 0x10

# CreatureAI::CallForHelp radius and the idle-state ally assist distance.
ASSIST_RADIUS = 8.0
# CreatureAIIdleState aggro distance for a level 4 bandit against a level 2 player (quest 9's minimum).
CRATE_AGGRO = 22.0
CAMPS = {
    "Forest Bandit grounds": ((245.0, 765.0), 60.0),
    "Forest Bandit grounds (-101, 137)": ((-101.0, 137.0), 75.0),
}
LINAS_CRATE = 2


class BanditCampsDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            from proto_runtime import load_modules
            modules = load_modules(ROOT)
        except (FileNotFoundError, OSError, SystemExit) as exc:
            raise unittest.SkipTest(f"protoc unavailable: {exc}")
        if not (EDITOR / "maps.data").is_file():
            raise unittest.SkipTest("data/editor is not checked out")

        def load(name, cls_name, key):
            message = getattr(modules[key], cls_name)()
            message.ParseFromString((EDITOR / f"{name}.data").read_bytes())
            return message

        cls.spells = {e.id: e for e in load("spells", "Spells", "spells").entry}
        cls.units = {e.id: e for e in load("units", "Units", "units").entry}
        maps = load("maps", "Maps", "maps")
        cls.map0 = next(m for m in maps.entry if m.id == 0)

    def camp_spawns(self, camp):
        (cx, cz), radius = CAMPS[camp]
        found = []
        for spawn in self.map0.unitspawns:
            ids = {spawn.unitentry} | {e.unitentry for e in spawn.unitentries}
            if ids & {FOREST_BANDIT, BANDIT_MAGE} and math.hypot(spawn.positionx - cx, spawn.positionz - cz) <= radius:
                found.append(spawn)
        return found

    def test_bandit_mage_casts_its_own_frostbolt(self):
        spells = {s.spellid for s in self.units[BANDIT_MAGE].creaturespells}
        self.assertIn(BANDIT_FROSTBOLT, spells)
        self.assertNotIn(PLAYER_FROSTBOLT, spells)

    def test_bandit_frostbolt_is_npc_only_and_weaker(self):
        npc = self.spells[BANDIT_FROSTBOLT]
        player = self.spells[PLAYER_FROSTBOLT]
        self.assertEqual((npc.rank, npc.baseid), (0, 0))
        self.assertFalse(npc.attributes[0] & ABILITY)
        self.assertTrue(player.attributes[0] & ABILITY)
        self.assertEqual((player.rank, player.baseid), (1, PLAYER_FROSTBOLT))
        self.assertGreaterEqual(npc.cooldown, 4000)
        self.assertLess(npc.effects[0].basepoints + npc.effects[0].diesides,
                        player.effects[0].basepoints + player.effects[0].diesides)

    def test_both_bandits_drop_the_armbands(self):
        for unit_id in (FOREST_BANDIT, BANDIT_MAGE):
            self.assertIn(ARMBAND_LOOT, self.units[unit_id].unitlootentries, self.units[unit_id].name)

    def test_mages_are_the_rarer_unit(self):
        for camp in CAMPS:
            bandits = mages = 0.0
            for spawn in self.camp_spawns(camp):
                entries = [(e.unitentry, e.weight) for e in spawn.unitentries if e.weight > 0] or [(spawn.unitentry, 1)]
                total = sum(w for _, w in entries)
                if entries[0][0] == FOREST_BANDIT and len(entries) > 1:
                    self.assertEqual(spawn.unitentry, FOREST_BANDIT, spawn.name)
                bandits += sum(w for u, w in entries if u == FOREST_BANDIT) / total
                mages += sum(w for u, w in entries if u == BANDIT_MAGE) / total
            self.assertGreater(mages, 0.0, camp)
            self.assertLess(mages * 3.0, bandits, f"{camp}: expected {bandits:.1f} bandits and {mages:.1f} mages")

    def test_packs_are_spaced_beyond_the_assist_radius(self):
        # Spawn names end in NN; a pack is a set of points closer than the assist radius.
        # Every point must be either inside one pack or farther than the assist radius from all others,
        # and no pack may grow beyond three bandits (one pull plus at most two helpers).
        for camp in CAMPS:
            spawns = self.camp_spawns(camp)
            packs = []
            for spawn in spawns:
                close = [p for p in packs if any(math.hypot(spawn.positionx - o.positionx, spawn.positionz - o.positionz)
                                                 <= ASSIST_RADIUS for o in p)]
                merged = [spawn]
                for pack in close:
                    packs.remove(pack)
                    merged += pack
                packs.append(merged)
            for pack in packs:
                self.assertLessEqual(len(pack), 3, f"{camp}: {[s.name for s in pack]}")

    def test_linas_crates_sit_outside_the_camp_centre(self):
        crates = [s for s in self.map0.objectspawns if s.objectentry == LINAS_CRATE]
        self.assertTrue(crates)
        spawns = self.camp_spawns("Forest Bandit grounds")
        for crate in crates:
            x, z = crate.location.positionx, crate.location.positionz
            near = [s for s in spawns if math.hypot(s.positionx - x, s.positionz - z) < CRATE_AGGRO]
            self.assertLessEqual(len(near), 1, f"crate at ({x:.0f}, {z:.0f}): {[s.name for s in near]}")
            for spawn in near:
                self.assertEqual(len(spawn.unitentries), 0, f"{spawn.name} guards a crate: keep it a plain Forest Bandit")
                self.assertEqual(spawn.unitentry, FOREST_BANDIT)

    def test_client_db_mirrors_editor_spells(self):
        if not (CLIENT_DB / "spells.data").is_file():
            self.skipTest("data/client is not checked out")
        self.assertEqual((EDITOR / "spells.data").read_bytes(), (CLIENT_DB / "spells.data").read_bytes())


if __name__ == "__main__":
    unittest.main()
