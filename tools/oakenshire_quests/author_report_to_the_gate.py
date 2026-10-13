# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author quest 66 "Report to the Gate": Guard Emrik sends a player who has finished the
Oakenshire farm quests to Guard-Corporal Rowan Hale at the west gate (bug 6acc8c0d).

    py tools/oakenshire_quests/author_report_to_the_gate.py            # validate only
    py tools/oakenshire_quests/author_report_to_the_gate.py --apply    # write editor data

Idempotent: re-running replaces quest 66 and re-asserts the same wiring.

Design notes that are easy to undo by accident:

* The quest needs BOTH the final boar quest (6, "The Alpha of the Herd") and the cellar rat
  quest (10, "Rat Troubles"). A quest has a single prevquestid, so the second prerequisite
  is the nextquestid back-link 10 -> 66: GamePlayerS::GetQuestStatus requires prevquestid to be
  rewarded AND (IsQuestLockedByBackLinks) at least one quest naming 66 as its nextquestid.
  nextquestid has no other runtime effect.
* A plain "go to x" breadcrumb: minimum reward, rewardxp == rewardclassxp == 45 like the
  trainer breadcrumbs Emrik already hands out (quests 3, 4, 23). No money, no items.
* Human-only (racemask 0x80000000) and class-independent, like the rest of Oakenshire.
* Wiring is authoritative for quest 66: only Emrik (u17) offers it, only Rowan Hale (u46)
  ends it.
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
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-quest-creator/scripts"))
from proto_runtime import find_protoc  # noqa: E402

OUT = ROOT / "generated/oakenshire_quests"

QUEST_ID = 66
QUEST_NAME = "Report to the Gate"
BOAR_FINAL_QUEST = 6      # The Alpha of the Herd
RAT_QUEST = 10            # Rat Troubles
GUARD_EMRIK = 17
ROWAN_HALE = 46
HUMAN_RACEMASK = 0x80000000
REWARD_XP = 45

DE, EN, FR, RU = 1, 2, 3, 4


def loc(de, en, fr, ru):
    return [{"locale": DE, "value": de}, {"locale": EN, "value": en},
            {"locale": FR, "value": fr}, {"locale": RU, "value": ru}]


NAME = ("Meldung am Tor", QUEST_NAME, "Au rapport à la porte", "Доклад у ворот")

DETAILS = (
    "Haldors Felder sind wieder ruhig, und wie ich höre, ist der Keller des alten Harbin endlich "
    "frei von Ratten. So etwas spricht sich in einem Lager wie diesem herum, $N.\n\n"
    "Innerhalb der Palisade gibt es nicht mehr viel zu tun für jemanden mit Eurem Mut. Der wahre "
    "Ärger wartet jenseits der Mauern. Wachkorporal Rowan Hale hält das Westtor von Oakenshire, "
    "und der Wache fehlt es immer an fähigen Händen. Folgt der Weststraße durch die Stadt und "
    "meldet Euch bei ihm.",

    "Haldor's fields are quiet again, and I hear Old Harbin's cellar is finally free of rats. "
    "Word gets around a camp like this, $N.\n\n"
    "There's not much left inside the palisade for someone with your nerve. The real trouble "
    "waits beyond the walls. Guard-Corporal Rowan Hale holds Oakenshire's west gate, and the "
    "Watch is always short of capable hands. Follow the Westroad through town and report to him.",

    "Les champs de Haldor sont de nouveau calmes, et j'ai entendu dire que la cave du vieux "
    "Harbin est enfin débarrassée de ses rats. Ce genre de nouvelle circule vite dans un camp "
    "comme celui-ci, $N.\n\n"
    "Il ne reste plus grand-chose à faire à l'intérieur de la palissade pour quelqu'un de votre "
    "trempe. Les vrais ennuis vous attendent au-delà des murs. Le caporal-garde Rowan Hale tient "
    "la porte ouest d'Oakenshire, et la Garde manque toujours de bras capables. Suivez la route "
    "de l'ouest à travers la ville et présentez-vous à lui.",

    "На полях Хальдора снова тихо, и, говорят, подвал Старого Харбина наконец-то избавлен от "
    "крыс. В таком лагере слухи расходятся быстро, $N.\n\n"
    "Внутри частокола для человека с вашей смелостью дел почти не осталось. Настоящие "
    "неприятности ждут за стенами. Капрал гвардии Роуэн Хейл охраняет западные ворота "
    "Океншира, а Дозору всегда не хватает умелых рук. Идите по Западной дороге через город и "
    "доложите ему.",
)

OBJECTIVES = (
    "Meldet Euch bei Wachkorporal Rowan Hale am Westtor von Oakenshire.",
    "Report to Guard-Corporal Rowan Hale at Oakenshire's west gate.",
    "Présentez-vous au caporal-garde Rowan Hale à la porte ouest d'Oakenshire.",
    "Доложите капралу гвардии Роуэну Хейлу у западных ворот Океншира.",
)

OFFER_REWARD = (
    "Emrik schickt Euch? Dann habt Ihr Grimtusks Herde zerschlagen und Harbins Keller "
    "gesäubert. Gut.\n\n"
    "Die Straße hinter diesem Tor ist kein Bauernhof, $N. Haltet Eure Klinge griffbereit und die "
    "Augen offen, dann wird die Wache reichlich Verwendung für Euch finden.",

    "Emrik sent you? Then you're the one who broke Grimtusk's herd and cleared Harbin's cellar. "
    "Good.\n\n"
    "The road beyond this gate is no farmyard, $N. Keep your blade close and your eyes open, and "
    "the Watch will find plenty of use for you.",

    "Emrik vous envoie ? Alors c'est vous qui avez dispersé le troupeau de Grimtusk et nettoyé la "
    "cave de Harbin. Bien.\n\n"
    "La route au-delà de cette porte n'a rien d'une basse-cour, $N. Gardez votre lame à portée de "
    "main et les yeux ouverts, et la Garde saura vous trouver de quoi faire.",

    "Вас прислал Эмрик? Значит, это вы разогнали стадо Мрачноклыка и очистили подвал Харбина. "
    "Хорошо.\n\n"
    "Дорога за этими воротами — не скотный двор, $N. Держите клинок под рукой и смотрите в оба, "
    "и Дозор найдёт вам достойное дело.",
)


def quest_definition():
    """Base strings are English; every text field carries all four locales."""
    return {
        "id": QUEST_ID,
        "name": NAME[1],
        "minlevel": 4,
        "questlevel": 4,
        "detailstext": DETAILS[1],
        "objectivestext": OBJECTIVES[1],
        "offerrewardtext": OFFER_REWARD[1],
        "requestitemstext": "",
        "endtext": "",
        "prevquestid": BOAR_FINAL_QUEST,
        "type": 0,
        "requiredraces": HUMAN_RACEMASK,
        "rewardxp": REWARD_XP,
        "internalname": "Oakenshire - Breadcrumb - Report to the Gate",
        "rewardclassxp": REWARD_XP,
        "name_loc": loc(*NAME),
        "detailstext_loc": loc(*DETAILS),
        "objectivestext_loc": loc(*OBJECTIVES),
        "offerrewardtext_loc": loc(*OFFER_REWARD),
    }


def _require(condition, message):
    if not condition:
        raise SystemExit(message)


def load_type(schema_dir, protos, message_name, protoc=None):
    with tempfile.TemporaryDirectory(prefix="report_to_gate_") as tmp:
        desc = Path(tmp) / "d.pb"
        subprocess.run([str(protoc or find_protoc(ROOT)), f"-I{schema_dir}",
                        f"--descriptor_set_out={desc}", "--include_imports", *protos],
                       cwd=schema_dir, check=True)
        file_set = descriptor_pb2.FileDescriptorSet.FromString(desc.read_bytes())
    pool = descriptor_pool.DescriptorPool()
    for entry in file_set.file:
        pool.Add(entry)
    return message_factory.GetMessageClass(pool.FindMessageTypeByName(message_name))


EDITOR_TYPES = {
    "quests": ("quests.proto", "mmo.proto.Quests"),
    "units": ("units.proto", "mmo.proto.Units"),
}


def editor_path(name):
    return ROOT / f"data/editor/data/{name}.data"


def load_editor(name, protoc=None):
    proto, message = EDITOR_TYPES[name]
    message_type = load_type(ROOT / "src/shared/proto_data", [proto], message, protoc)
    return message_type.FromString(editor_path(name).read_bytes())


def by_id(dataset, entry_id):
    return next((entry for entry in dataset.entry if entry.id == entry_id), None)


def upsert_quest(quests):
    target = by_id(quests, QUEST_ID)
    if target is None:
        target = quests.entry.add()
    else:
        _require(target.name == QUEST_NAME,
                 f"quest {QUEST_ID} is {target.name!r}, refusing to overwrite it with {QUEST_NAME!r}")
    target.Clear()
    json_format.ParseDict(quest_definition(), target)


def link_rat_quest(quests):
    rats = by_id(quests, RAT_QUEST)
    _require(rats is not None and rats.name == "Rat Troubles", f"quest {RAT_QUEST} is not 'Rat Troubles'")
    _require(rats.nextquestid in (0, QUEST_ID),
             f"quest {RAT_QUEST} already unlocks quest {rats.nextquestid}, refusing to repoint it")
    rats.nextquestid = QUEST_ID


def wire_units(units):
    emrik = by_id(units, GUARD_EMRIK)
    rowan = by_id(units, ROWAN_HALE)
    _require(emrik is not None and emrik.name == "Guard Emrik", f"unit {GUARD_EMRIK} is not Guard Emrik")
    _require(rowan is not None and rowan.name == "Guard-Corporal Rowan Hale",
             f"unit {ROWAN_HALE} is not Guard-Corporal Rowan Hale")
    for unit in units.entry:
        for field, owner in (("quests", emrik), ("end_quests", rowan)):
            ids = getattr(unit, field)
            if unit is owner:
                if QUEST_ID not in ids:
                    ids.append(QUEST_ID)
            elif QUEST_ID in ids:
                kept = [quest_id for quest_id in ids if quest_id != QUEST_ID]
                del ids[:]
                ids.extend(kept)


def validate(quests, units):
    ids = [quest.id for quest in quests.entry]
    _require(len(ids) == len(set(ids)), "duplicate quest ids")
    for prerequisite in (BOAR_FINAL_QUEST, RAT_QUEST):
        _require(prerequisite in ids, f"prerequisite quest {prerequisite} missing")
    _require(quests.IsInitialized(), "quests dataset is missing required fields")
    _require(units.IsInitialized(), "units dataset is missing required fields")


def plan(protoc=None):
    quests = load_editor("quests", protoc)
    units = load_editor("units", protoc)
    upsert_quest(quests)
    link_rat_quest(quests)
    wire_units(units)
    validate(quests, units)
    return quests, units


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--protoc", default=None, help="protoc to use instead of the build tree's")
    args = parser.parse_args()

    quests, units = plan(args.protoc)
    if not args.apply:
        print(f"validated quest {QUEST_ID} {QUEST_NAME!r} and its wiring")
        return

    OUT.mkdir(parents=True, exist_ok=True)
    backup = OUT / ("backup_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    backup.mkdir()
    for name in EDITOR_TYPES:
        shutil.copy2(editor_path(name), backup / f"editor_{name}.data")

    for name, dataset in (("quests", quests), ("units", units)):
        editor_path(name).write_bytes(dataset.SerializeToString())

    print(f"wrote quest {QUEST_ID} {QUEST_NAME!r} and its wiring to the editor data. Backup: {backup}")


if __name__ == "__main__":
    main()
