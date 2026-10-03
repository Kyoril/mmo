# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Rewrites the Fire Barrage (150) tooltip so it shows real numbers.

Fire Barrage itself deals no damage: its PeriodicTriggerSpell effect casts Fire Barrage
Projectile (152) once per tick. The tooltip therefore reads the projectile's damage through
a spell reference ("$152s0"), the wave count through "$t0" and the channel total through
"$o0", which totals the triggered spell's damage over all ticks. See
src/shared/game_client/spell_text_formatter.h for the placeholder syntax.

    py tools/spell_text/author_fire_barrage_text.py            # dry run, print the texts
    py tools/spell_text/author_fire_barrage_text.py --apply    # write editor + ClientDB

Idempotent. ClientDB receives a byte copy of the editor spells.data, like the other
authoring scripts.
"""

import argparse
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from spell_text_data import ROOT, client_path, editor_path, load_spells  # noqa: E402

FIRE_BARRAGE = 150
PROJECTILE = 152

# LocalizedString.locale: 1 = deDE, 2 = enUS, 3 = frFR, 4 = ruRU
DESCRIPTIONS = {
    1: "Kanalisiert über $D $t0 Flammenwellen auf den Feind. Jede Welle schleudert feurige "
       "Geschosse, die $152s0 Feuerschaden verursachen, insgesamt $o0 Feuerschaden.",
    2: "Channels $t0 waves of flame at the enemy over $D. Each wave hurls fiery projectiles "
       "that deal $152s0 fire damage, $o0 fire damage in total.",
    3: "Canalise $t0 vagues de flammes sur l'ennemi pendant $D. Chaque vague projette des "
       "projectiles enflammés qui infligent $152s0 points de dégâts de feu, soit $o0 points "
       "de dégâts de feu au total.",
    4: "Направляет во врага $t0 волны пламени в течение $D. Каждая волна выпускает огненные "
       "снаряды, нанося $152s0 ед. урона от огня, всего $o0 ед. урона от огня.",
}


def _require(condition, message):
    """Like assert, but survives ``python -O`` -- these guards gate writing game data."""
    if not condition:
        raise SystemExit(message)


def apply_texts(spells):
    by_id = {spell.id: spell for spell in spells.entry}
    barrage = by_id.get(FIRE_BARRAGE)
    _require(barrage is not None and barrage.name == "Fire Barrage", "spell 150 is not Fire Barrage")
    _require(PROJECTILE in by_id, "Fire Barrage Projectile (152) is missing")
    _require(barrage.effects[0].triggerspell == PROJECTILE,
             "Fire Barrage effect 0 no longer triggers the projectile; the texts would lie")

    barrage.description = DESCRIPTIONS[2]
    existing = {entry.locale: entry for entry in barrage.description_loc}
    for locale, text in DESCRIPTIONS.items():
        entry = existing.get(locale)
        if entry is None:
            entry = barrage.description_loc.add()
            entry.locale = locale
        entry.value = text


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[1])
    parser.add_argument("--apply", action="store_true", help="write editor + ClientDB")
    args = parser.parse_args()

    spells = load_spells()
    apply_texts(spells)
    for locale, text in DESCRIPTIONS.items():
        print(f"[{locale}] {text}")

    if not args.apply:
        print("dry run, nothing written (pass --apply)")
        return

    editor_path().write_bytes(spells.SerializeToString())
    shutil.copyfile(editor_path(), client_path())
    print(f"wrote Fire Barrage tooltip to {editor_path().relative_to(ROOT)} + ClientDB")


if __name__ == "__main__":
    main()
