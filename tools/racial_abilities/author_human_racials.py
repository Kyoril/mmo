# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the human racial spells (248-250) and the Human race's racialSpells list.

    py tools/racial_abilities/author_human_racials.py            # validate only
    py tools/racial_abilities/author_human_racials.py --apply    # write editor + ClientDB

Idempotent: re-running replaces the same ids. ClientDB receives a byte copy of the editor
blobs, exactly what mmo_edit's ExportToClient does, so the two trees cannot drift.

Design notes that are easy to undo by accident:

* racemask 0x80000000 is Human (race id 0): masks are ``1 << (id - 1)`` and the data already
  relies on id 0 wrapping to bit 31 (see the Mage classmask).
* classmask stays 0. With no class bit and no Proficiency effect, GamePlayerS keeps the spells
  active across class switches.
* Passives carry HiddenAura: two permanent buffs would only clutter the buff bar.
* Call of the Watch uses ApplyAreaAura. AuraContainer::HandleAreaAuraTick propagates it to
  party members within the *rangetype* radius (5 = 30 yd), not the effect radius.
"""

import argparse
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path

from google.protobuf import descriptor_pb2, descriptor_pool, json_format, message_factory

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-spell-designer/scripts"))
from proto_runtime import find_protoc  # noqa: E402

OUT = ROOT / "generated/racial_abilities"

HUMAN_RACE_ID = 0
HUMAN_RACEMASK = 0x80000000
VERSATILITY, HARD_WORK, CALL_OF_THE_WATCH = 248, 249, 250
SPELL_IDS = (VERSATILITY, HARD_WORK, CALL_OF_THE_WATCH)

# Ids used by the visualization and sound authored in later steps (see author_racial_visuals.py).
VIS_ID = 78
SOUND_ID = 135

# Effect / aura type values (src/shared/game/spell.h, src/shared/game/aura.h).
APPLY_AURA, APPLY_AREA_AURA = 6, 49
MOD_DAMAGE_DONE_PCT, MOD_DAMAGE_TAKEN_PCT, MOD_STAT_PCT, MOD_HEALTH_REGEN_PCT = 23, 24, 31, 38

# Attribute words (spell_attributes / spell_attributes_b).
ONLY_ONE_STACK, ABILITY, PASSIVE = 0x08, 0x10, 0x40
HIDDEN_AURA, IGNORE_LOS = 0x02, 0x80

DE, EN, FR, RU = 1, 2, 3, 4

ICON_DIR = "Interface/Icons/Spells/"


def loc(de, en, fr, ru):
    return [{"locale": DE, "value": de}, {"locale": EN, "value": en},
            {"locale": FR, "value": fr}, {"locale": RU, "value": ru}]


def texts(name, description, auratext):
    """Base strings are English; every field carries all four locales."""
    return {
        "name": name[1], "description": description[1], "auratext": auratext[1],
        "name_loc": loc(*name), "description_loc": loc(*description),
        "auratext_loc": loc(*auratext),
    }


def common(spell_id, icon):
    return {"id": spell_id, "baseid": spell_id, "rank": 1, "baselevel": 1, "spelllevel": 1,
            "racemask": HUMAN_RACEMASK, "classmask": 0, "positive": 1,
            "icon": ICON_DIR + icon}


def spell_definitions():
    versatility = common(VERSATILITY, "T_Icon_Gold_17.htex")
    versatility.update({
        "attributes": [PASSIVE | ONLY_ONE_STACK, IGNORE_LOS | HIDDEN_AURA],
        "effects": [{"index": i, "type": APPLY_AURA, "aura": MOD_STAT_PCT, "basepoints": 2,
                     "miscvaluea": i} for i in range(5)],
    })
    versatility.update(texts(
        ("Vielseitigkeit", "Versatility", "Polyvalence", "Разносторонность"),
        ("Erhöht Stärke, Beweglichkeit, Ausdauer, Intelligenz und Willenskraft um $s0%.",
         "Increases Strength, Agility, Stamina, Intellect and Spirit by $s0%.",
         "Augmente la Force, l'Agilité, l'Endurance, l'Intelligence et l'Esprit de $s0%.",
         "Увеличивает силу, ловкость, выносливость, интеллект и дух на $s0%."),
        ("Alle Werte um $s0% erhöht.", "All stats increased by $s0%.",
         "Toutes les caractéristiques augmentées de $s0%.",
         "Все характеристики увеличены на $s0%.")))

    hard_work = common(HARD_WORK, "T_Icon_Gold_07.htex")
    hard_work.update({
        "attributes": [PASSIVE | ONLY_ONE_STACK, IGNORE_LOS | HIDDEN_AURA],
        "effects": [{"index": 0, "type": APPLY_AURA, "aura": MOD_HEALTH_REGEN_PCT,
                     "basepoints": 10}],
    })
    hard_work.update(texts(
        ("Harte Arbeit gewohnt", "Used to Hard Work", "Habitué au dur labeur",
         "Привычка к тяжёлому труду"),
        ("Erhöht die Lebensregeneration um $s0%.", "Increases health regeneration by $s0%.",
         "Augmente la régénération de la vie de $s0%.",
         "Увеличивает скорость восстановления здоровья на $s0%."),
        ("Lebensregeneration um $s0% erhöht.", "Health regeneration increased by $s0%.",
         "Régénération de la vie augmentée de $s0%.",
         "Восстановление здоровья увеличено на $s0%.")))

    call = common(CALL_OF_THE_WATCH, "T_Icon_Gold_84.htex")
    call.update({
        "attributes": [ABILITY | ONLY_ONE_STACK, IGNORE_LOS],
        "effects": [
            {"index": 0, "type": APPLY_AREA_AURA, "aura": MOD_DAMAGE_DONE_PCT,
             "basepoints": 5, "radius": 30.0},
            {"index": 1, "type": APPLY_AREA_AURA, "aura": MOD_DAMAGE_TAKEN_PCT,
             "basepoints": -5, "radius": 30.0},
        ],
        "duration": 10000,
        "cooldown": 120000,
        "rangetype": 5,
    })
    call.update(texts(
        ("Ruf der Wache", "Call of the Watch", "Appel de la Garde", "Зов Стражи"),
        ("Ruft die Wache zur Pflicht: Du und deine Gruppenmitglieder im Umkreis von 30 Metern "
         "verursacht $D lang $s0% mehr Schaden und erleidet 5% weniger Schaden.",
         "Calls upon the duty of the Watch: you and party members within 30 yards deal $s0% "
         "more damage and take 5% less damage for $D.",
         "Fait appel au devoir de la Garde : vous et les membres du groupe à moins de 30 mètres "
         "infligez $s0% de dégâts supplémentaires et subissez 5% de dégâts en moins pendant $D.",
         "Взывает к долгу Стражи: вы и члены группы в радиусе 30 метров наносите на $s0% "
         "больше урона и получаете на 5% меньше урона в течение $D."),
        ("Verursachter Schaden um $s0% erhöht, erlittener Schaden um 5% verringert.",
         "Damage dealt increased by $s0%, damage taken reduced by 5%.",
         "Dégâts infligés augmentés de $s0%, dégâts subis réduits de 5%.",
         "Наносимый урон увеличен на $s0%, получаемый урон уменьшен на 5%.")))

    return [versatility, hard_work, call]


def _require(condition, message):
    """Like assert, but survives ``python -O`` -- these guards gate writing game data."""
    if not condition:
        raise SystemExit(message)


def load_type(schema_dir, protos, message_name):
    with tempfile.TemporaryDirectory(prefix="racials_") as tmp:
        desc = Path(tmp) / "d.pb"
        subprocess.run([str(find_protoc(ROOT)), f"-I{schema_dir}",
                        f"--descriptor_set_out={desc}", "--include_imports", *protos],
                       cwd=schema_dir, check=True)
        file_set = descriptor_pb2.FileDescriptorSet.FromString(desc.read_bytes())
    pool = descriptor_pool.DescriptorPool()
    for entry in file_set.file:
        pool.Add(entry)
    return message_factory.GetMessageClass(pool.FindMessageTypeByName(message_name))


EDITOR_TYPES = {
    "spells": ("spells.proto", "mmo.proto.Spells"),
    "races": ("races.proto", "mmo.proto.Races"),
}


def editor_path(name):
    return ROOT / f"data/editor/data/{name}.data"


def client_path(name):
    return ROOT / f"data/client/ClientDB/{name}.data"


def load_editor(name):
    proto, message = EDITOR_TYPES[name]
    message_type = load_type(ROOT / "src/shared/proto_data", [proto], message)
    return message_type.FromString(editor_path(name).read_bytes())


def upsert_spells(dataset):
    by_id = {spell.id: spell for spell in dataset.entry}
    for draft in spell_definitions():
        visualization_id = 0
        target = by_id.get(draft["id"])
        if target is None:
            target = dataset.entry.add()
        else:
            _require(target.name == draft["name"],
                     f"spell {draft['id']} is {target.name!r}, refusing to overwrite it "
                     f"with {draft['name']!r}")
            visualization_id = target.visualization_id
        target.Clear()
        json_format.ParseDict(draft, target)
        # The spell -> visualization link is owned by author_racial_visuals.py; keep it on re-runs.
        if visualization_id:
            target.visualization_id = visualization_id


def set_racial_spells(races):
    human = next((race for race in races.entry if race.id == HUMAN_RACE_ID), None)
    _require(human is not None, "race 0 (Human) is missing")
    _require(human.name == "Human", f"race 0 is {human.name!r}, expected 'Human'")
    del human.racialSpells[:]
    human.racialSpells.extend(SPELL_IDS)


def validate(spells, races):
    ids = [spell.id for spell in spells.entry]
    _require(len(ids) == len(set(ids)), "duplicate spell ids")
    for spell_id in SPELL_IDS:
        _require(spell_id in ids, f"spell {spell_id} missing after upsert")
    for draft in spell_definitions():
        _require((ROOT / "data/client" / draft["icon"]).is_file(),
                 f"missing icon {draft['icon']}")
    _require(spells.IsInitialized(), "spells dataset is missing required fields")
    _require(races.IsInitialized(), "races dataset is missing required fields")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    spells = load_editor("spells")
    races = load_editor("races")
    upsert_spells(spells)
    set_racial_spells(races)
    validate(spells, races)

    if not args.apply:
        print(f"validated racial spells {SPELL_IDS} and Human racialSpells")
        return

    OUT.mkdir(parents=True, exist_ok=True)
    backup = OUT / ("backup_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    backup.mkdir()
    for name in EDITOR_TYPES:
        shutil.copy2(editor_path(name), backup / f"editor_{name}.data")
        shutil.copy2(client_path(name), backup / f"client_{name}.data")

    for name, dataset in (("spells", spells), ("races", races)):
        blob = dataset.SerializeToString()
        editor_path(name).write_bytes(blob)
        client_path(name).write_bytes(blob)

    print(f"wrote racial spells {SPELL_IDS} and Human racialSpells to editor + ClientDB. "
          f"Backup: {backup}")


if __name__ == "__main__":
    main()
