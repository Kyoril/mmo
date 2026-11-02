# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Every acolyte ability has an icon of its own (spec: acolyte kit overhaul, "Presentation / Icons").

What counts as an ability ("a spell" for this test):

* A player-REACHABLE spell: one a player can see or use. That is a spell on a class trainer, in a
  class spell list, a talent rank, a spell of a pet entry (default, level or AI spells), an item
  spell, or a racial spell. Creature-only copies (for example creature spell 180 "Shadowbolt",
  which shares the player Shadowbolt's icon) are not abilities and are ignored on both sides.
* Ranks of one chain (same non-zero baseid) are ONE ability and share an icon.
* A spell triggered by exactly one other spell (a proc or follow-up effect, found through the
  effects' triggerspell) belongs to the triggering ability's group and may share its icon.
* The Duskling's spells 301-308 always count (Summon / Dismiss / Bind are cast from the pet UI).
* Retired spells (author_talent_trees.RETIRED_RANK_SPELLS, which includes the neutered Umbra path
  and Abyssal Reach) are ignored on both sides.

Rule: no acolyte ability group (classmask 4 spells, the Duskling's spells 301-308 and spells of the
Gloomling / Brood Horror units) may share an icon with any ability group of the rest of the
reachable catalog, nor with another acolyte group. Collisions entirely outside the acolyte are
printed as a report and do not fail.

Set MMO_ICON_TEST_DATA_DIR to a directory holding spells/trainers/classes/talents/pets/items/
races/units ".data" blobs to run the rules against other data (for example an older commit).
Skips when protoc or the data submodules are unavailable.
"""

import os
import sys
import unittest
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/talent_trees"))
sys.path.insert(0, str(ROOT / "tools/spell_text"))

ACOLYTE_MASK = 4
ACOLYTE_PET_UNITS = (123, 124)  # Gloomling, Brood Horror
DUSKLING_SPELLS = range(301, 309)

TYPES = {
    "spells": ("spells.proto", "mmo.proto.Spells"),
    "trainers": ("trainers.proto", "mmo.proto.Trainers"),
    "classes": ("classes.proto", "mmo.proto.Classes"),
    "talents": ("talents.proto", "mmo.proto.Talents"),
    "pets": ("pets.proto", "mmo.proto.Pets"),
    "items": ("items.proto", "mmo.proto.Items"),
    "races": ("races.proto", "mmo.proto.Races"),
    "units": ("units.proto", "mmo.proto.Units"),
}


def normalize_icon(path):
    return path.replace("\\", "/").lower()


def load_catalog(att, data_dir):
    """Parses the editor blobs of `data_dir` into a dict of protobuf messages."""
    catalog = {}
    for name, (proto, message) in TYPES.items():
        message_type = att.load_type(ROOT / "src/shared/proto_data", proto, message)
        catalog[name] = message_type.FromString((data_dir / f"{name}.data").read_bytes())
    return catalog


def reachable_spells(catalog):
    """Ids of every spell a player can see or use (trainer, class list, talent, pet, item, racial)."""
    ids = set()
    for trainer in catalog["trainers"].entry:
        ids.update(entry.spell for entry in trainer.spells)
    for cls in catalog["classes"].entry:
        ids.update(entry.spell for entry in cls.spells)
    for talent in catalog["talents"].entry:
        ids.update(talent.ranks)
    for pet in catalog["pets"].entry:
        ids.update(pet.default_spells)
        ids.update(entry.spell for entry in pet.level_spells)
        ids.update(entry.spellid for entry in pet.ai_spells)
    for item in catalog["items"].entry:
        ids.update(entry.spell for entry in item.spells if entry.spell)
    for race in catalog["races"].entry:
        ids.update(race.racialSpells)
    return ids


def build_groups(spells_by_id, abilities):
    """Union-find over abilities: rank chains (baseid) and sole-trigger proc pairs share a group."""
    parent = {spell_id: spell_id for spell_id in spells_by_id}

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a, b):
        parent[find(a)] = find(b)

    chains = defaultdict(list)
    for spell in spells_by_id.values():
        if spell.baseid:
            chains[spell.baseid].append(spell.id)
    for base, members in chains.items():
        for member in members:
            if base in parent:
                union(member, base)

    triggered_by = defaultdict(set)
    for spell in spells_by_id.values():
        for effect in spell.effects:
            if effect.triggerspell and effect.triggerspell in spells_by_id:
                triggered_by[effect.triggerspell].add(spell.id)
    for target, sources in triggered_by.items():
        # A proc spell triggered by exactly one other spell belongs to it, unless it is an
        # independent ability of its own (then it keeps its own group).
        if len(sources) == 1 and target not in abilities:
            union(target, next(iter(sources)))
    return find


def analyse(att, catalog):
    """Returns (acolyte_collisions, other_collisions) as lists of (icon, [(id, name), ...])."""
    spells_by_id = {spell.id: spell for spell in catalog["spells"].entry}
    retired = set(att.RETIRED_RANK_SPELLS)
    # Summon / Dismiss / Bind Duskling are cast from the pet UI and a bind item, so they count
    # even though no trainer or pet entry lists them.
    abilities = {spell_id for spell_id in reachable_spells(catalog) | set(DUSKLING_SPELLS)
                 if spell_id in spells_by_id and spell_id not in retired}
    find = build_groups(spells_by_id, abilities)

    acolyte_units = {unit.id: unit for unit in catalog["units"].entry
                     if unit.id in ACOLYTE_PET_UNITS}
    acolyte_extra = set(DUSKLING_SPELLS)
    for unit in acolyte_units.values():
        acolyte_extra.update(entry.spellid for entry in unit.creaturespells)

    def is_acolyte(spell):
        return bool(spell.classmask & ACOLYTE_MASK) or spell.id in acolyte_extra

    group_icon_users = defaultdict(lambda: defaultdict(set))  # icon -> group -> ability ids
    for spell_id in abilities:
        spell = spells_by_id[spell_id]
        if spell.icon:
            group_icon_users[normalize_icon(spell.icon)][find(spell_id)].add(spell_id)

    acolyte_collisions, other_collisions = [], []
    for icon, groups in sorted(group_icon_users.items()):
        if len(groups) < 2:
            continue
        members = sorted(spell_id for ids in groups.values() for spell_id in ids)
        described = [(spell_id, spells_by_id[spell_id].name) for spell_id in members]
        if any(is_acolyte(spells_by_id[spell_id]) for spell_id in members):
            acolyte_collisions.append((icon, described))
        else:
            other_collisions.append((icon, described))
    return acolyte_collisions, other_collisions


class AcolyteIconUniquenessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        override = os.environ.get("MMO_ICON_TEST_DATA_DIR")
        data_dir = Path(override) if override else ROOT / "data/editor/data"
        try:
            import author_talent_trees as att
            cls.att = att
            cls.catalog = load_catalog(att, data_dir)
        except (FileNotFoundError, OSError, SystemExit, ImportError) as exc:
            raise unittest.SkipTest(f"data or protoc unavailable: {exc}")
        cls.spells_by_id = {spell.id: spell for spell in cls.catalog["spells"].entry}
        cls.acolyte, cls.other = analyse(att, cls.catalog)

    def test_acolyte_abilities_have_an_icon(self):
        retired = set(self.att.RETIRED_RANK_SPELLS)
        missing = []
        for spell_id in reachable_spells(self.catalog) | set(DUSKLING_SPELLS):
            spell = self.spells_by_id.get(spell_id)
            if spell is None or spell_id in retired:
                continue
            if (spell.classmask & ACOLYTE_MASK or spell_id in DUSKLING_SPELLS) and not spell.icon:
                missing.append((spell_id, spell.name))
        self.assertEqual(missing, [], "acolyte abilities without an icon")

    def test_acolyte_icon_files_exist(self):
        client = ROOT / "data/client"
        if not (client / "Interface").is_dir():
            self.skipTest("data/client submodule is not checked out")
        missing = []
        for spell in self.spells_by_id.values():
            if spell.classmask & ACOLYTE_MASK and spell.icon and \
                    not (client / spell.icon).is_file():
                missing.append((spell.id, spell.name, spell.icon))
        self.assertEqual(missing, [], "icon files missing from data/client")

    def test_no_acolyte_ability_shares_an_icon(self):
        lines = [f"{icon}: " + ", ".join(f"{i} {n}" for i, n in members)
                 for icon, members in self.acolyte]
        self.assertEqual(lines, [], "acolyte abilities share icons with another ability")

    def test_report_collisions_outside_the_acolyte(self):
        if self.other:
            print(f"\n[icons] {len(self.other)} icon collisions entirely outside the acolyte "
                  "(report only):")
            for icon, members in self.other:
                print("  " + icon.rsplit("/", 1)[-1] + ": " +
                      ", ".join(f"{i} {n}" for i, n in members))


if __name__ == "__main__":
    unittest.main()
