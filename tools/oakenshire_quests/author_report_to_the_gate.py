# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author quest 66 "Report to the Gate": Guard Emrik sends a player who has dealt with Farmer
Haldor's young boars to Guard-Corporal Rowan Hale at the west gate (bug 6acc8c0d; re-gated as an
early breadcrumb in the Oakenshire first-hour pass, 2026-10-18).

    py tools/oakenshire_quests/author_report_to_the_gate.py            # validate only
    py tools/oakenshire_quests/author_report_to_the_gate.py --apply    # write editor data

Idempotent: re-running replaces quest 66 and re-asserts the same wiring.

Design notes that are easy to undo by accident:

* The quest needs only the first boar quest (1, "The Boar Problem"), from level 2, so new
  players reach the west gate (Rowan Hale, Lina Groveroot) early instead of after the whole
  farm chain. No quest may name 66 as its nextquestid: GamePlayerS::GetQuestStatus also
  requires (IsQuestLockedByBackLinks) at least one such back-link quest to be rewarded, so a
  back-link would silently add a second prerequisite. The old 10 -> 66 link is removed.
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
BOAR_FIRST_QUEST = 1      # The Boar Problem
MIN_LEVEL = 2
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
    "Mit Haldors Ebern seid Ihr gut fertig geworden, $N. So etwas spricht sich in einem Lager "
    "wie diesem herum.\n\n"
    "Wenn Ihr mehr ehrliche Arbeit sucht: Der Wache fehlt es immer an fähigen Händen. "
    "Wachkorporal Rowan Hale hält das Westtor von Oakenshire. Folgt der Weststraße durch die "
    "Stadt und meldet Euch bei ihm. Lina Hainwurz, unsere Kräuterkundige, ist meist ganz in "
    "seiner Nähe, und wie ich höre, könnte auch sie etwas Hilfe gebrauchen.",

    "You handled Haldor's boars well, $N. Word gets around a camp like this.\n\n"
    "If you want more honest work, the Watch is always short of capable hands. Guard-Corporal "
    "Rowan Hale holds Oakenshire's west gate; follow the Westroad through town and report to "
    "him. Lina Groveroot, our herbalist, is usually somewhere near him, and from what I hear "
    "she could use some help as well.",

    "Vous vous êtes bien occupé des sangliers de Haldor, $N. Ce genre de nouvelle circule vite "
    "dans un camp comme celui-ci.\n\n"
    "Si vous cherchez encore du travail honnête, la Garde manque toujours de bras capables. Le "
    "caporal-garde Rowan Hale tient la porte ouest d'Oakenshire ; suivez la route de l'ouest à "
    "travers la ville et présentez-vous à lui. Lina Racine-de-Bosquet, notre herboriste, se "
    "trouve généralement non loin de lui, et d'après ce que j'entends, elle aurait elle aussi "
    "besoin d'aide.",

    "Вы хорошо разобрались с кабанами Хальдора, $N. В таком лагере слухи расходятся быстро.\n\n"
    "Если вам нужна честная работа, Дозору всегда не хватает умелых рук. Капрал гвардии Роуэн "
    "Хейл охраняет западные ворота Океншира. Идите по Западной дороге через город и доложите "
    "ему. Лина Рощекорень, наша травница, обычно где-то рядом с ним, и, говорят, ей тоже не "
    "помешала бы помощь.",
)

OBJECTIVES = (
    "Meldet Euch bei Wachkorporal Rowan Hale am Westtor von Oakenshire.",
    "Report to Guard-Corporal Rowan Hale at Oakenshire's west gate.",
    "Présentez-vous au caporal-garde Rowan Hale à la porte ouest d'Oakenshire.",
    "Доложите капралу гвардии Роуэну Хейлу у западных ворот Океншира.",
)

OFFER_REWARD = (
    "Emrik schickt Euch? Gut. Die Wache kann immer noch ein Paar Hände gebrauchen.\n\n"
    "Banditen haben sich südlich der Stadt eingenistet und sind dreist genug geworden, dieses "
    "Tor zu beobachten. Haltet Eure Klinge griffbereit und die Augen offen, $N, dann wird die "
    "Wache reichlich Verwendung für Euch finden. Und wenn Lina Euch zu sich winkt, hört sie an. "
    "Diese Schläger haben ihr etwas gestohlen.",

    "Emrik sent you? Good. The Watch can always use another pair of hands.\n\n"
    "Bandits have dug in south of town, and they have grown bold enough to watch this gate. "
    "Keep your blade close and your eyes open, $N, and the Watch will find plenty of use for "
    "you. And if Lina waves you over, hear her out. Those thugs took something of hers.",

    "Emrik vous envoie ? Bien. La Garde a toujours besoin de bras supplémentaires.\n\n"
    "Des bandits se sont installés au sud de la ville, et ils sont devenus assez audacieux pour "
    "surveiller cette porte. Gardez votre lame à portée de main et les yeux ouverts, $N, et la "
    "Garde saura vous trouver de quoi faire. Et si Lina vous fait signe, écoutez-la. Ces voyous "
    "lui ont pris quelque chose.",

    "Вас прислал Эмрик? Хорошо. Дозору всегда пригодится ещё одна пара рук.\n\n"
    "Бандиты засели к югу от города и обнаглели настолько, что следят за этими воротами. "
    "Держите клинок под рукой и смотрите в оба, $N, и Дозор найдёт вам достойное дело. А если "
    "Лина позовёт вас, выслушайте её. Эти головорезы кое-что у неё отняли.",
)


def quest_definition():
    """Base strings are English; every text field carries all four locales."""
    return {
        "id": QUEST_ID,
        "name": NAME[1],
        "minlevel": MIN_LEVEL,
        "questlevel": MIN_LEVEL,
        "detailstext": DETAILS[1],
        "objectivestext": OBJECTIVES[1],
        "offerrewardtext": OFFER_REWARD[1],
        "requestitemstext": "",
        "endtext": "",
        "prevquestid": BOAR_FIRST_QUEST,
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


def unlink_back_links(quests):
    """Drops every nextquestid back-link to this quest (each would add a prerequisite)."""
    for quest in quests.entry:
        if quest.id != QUEST_ID and quest.nextquestid == QUEST_ID:
            quest.nextquestid = 0


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
    _require(BOAR_FIRST_QUEST in ids, f"prerequisite quest {BOAR_FIRST_QUEST} missing")
    _require(quests.IsInitialized(), "quests dataset is missing required fields")
    _require(units.IsInitialized(), "units dataset is missing required fields")


def plan(protoc=None):
    quests = load_editor("quests", protoc)
    units = load_editor("units", protoc)
    upsert_quest(quests)
    unlink_back_links(quests)
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
