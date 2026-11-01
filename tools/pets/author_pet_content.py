# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the first pet content: a necromancer that raises a skeleton swarm, the acolyte's bound
shadow, and a taming test kit.

    py tools/pets/author_pet_content.py            # validate only
    py tools/pets/author_pet_content.py --apply    # write editor + ClientDB

Idempotent: every row is replaced by id (refusing to overwrite a row of another name), the
trainer rows and the spawn are replaced by spell id / spawn name. ClientDB receives a byte
copy of the editor blobs it shares with the client (spells, maps), exactly what mmo_edit's
ExportToClient does. units, pets and trainers are server-only.

Ids (scanned free on 2026-11-01: spells end at 275 below the 1000+ talent block, units at 115,
there were no pets yet):

    spells 300-311, units 120-122, pets 1-2, tamable creature 44 (Young Forest Wolf).

Design notes that are easy to undo by accident:

* Raise Skeletons is a creature spell (in Ilsa Mourne's ``creaturespells``). SummonPet with
  basepoints 3, pet_max_count 5, ReplaceOldest and pet_level_source CharacterLevel, which is the
  caster's own level for an NPC caster. Spell duration 0: the skeletons live as long as she
  does. The effect targets the caster, so the spell is not harmful and the AI may "cast it at"
  its victim (the AI always passes its victim as the unit target).
* Bind Shadow is the trainer spell (acolyte trainer 4). It grants the stored pet and teaches
  Summon Shadow and Dismiss Shadow through LearnSpell effects, so the trainer lists one row and
  nobody owns a summon spell for a pet they do not have. The persistent pet cast check refuses
  it once the shadow is bound, so the LearnSpell effects never run twice.
* Pet abilities (Gloom Dart, Sapping Murk, Umbral Lash, Murkbind) have classmask 0, no
  reagents and no power cost: the shade casts them through its AI and its pet bar, and nothing
  may stop a creature from casting them.
* Tame Beast is a 5 s *cast*, not a channel: a Channeled spell applies its effects when the
  channel starts (SingleCastState), which would tame on the first frame.
* No class learns Tame Beast, Call Pet or Revive Pet (classmask 0, on no trainer). They are
  test spells, learned with the GM command ``learnspell``.
* All names are original. Placeholder models: the shade uses the undead NPC model at 0.8
  scale until it gets its own.
"""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path

from google.protobuf import descriptor_pb2, descriptor_pool, json_format, message_factory

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-spell-designer/scripts"))
sys.path.insert(0, str(ROOT / "tools/spell_text"))
from proto_runtime import find_protoc  # noqa: E402
import spell_text_data  # noqa: E402

OUT = ROOT / "generated/pets"

# --- Ids -----------------------------------------------------------------------------------
RAISE_SKELETONS = 300
BIND_SHADOW, SUMMON_SHADOW, DISMISS_SHADOW = 301, 302, 303
GLOOM_DART, SAPPING_MURK, UMBRAL_LASH, MURKBIND = 304, 305, 306, 307
SHADOW_LORE_MURKBIND = 308
TAME_BEAST, CALL_PET, REVIVE_PET = 309, 310, 311
SPELL_IDS = tuple(range(300, 312))

RISEN_SKELETON, GRAVECALLER, UMBRAL_SHADE = 120, 121, 122
UNIT_IDS = (RISEN_SKELETON, GRAVECALLER, UMBRAL_SHADE)

SHADE_PET, WOLF_PET = 1, 2
PET_IDS = (SHADE_PET, WOLF_PET)

TAMABLE_UNIT = 44              # Young Forest Wolf (levels 3-4), Briarwatch March and Oakenshire
TAMABLE_UNIT_NAME = "Young Forest Wolf"

ACOLYTE_TRAINER = 4            # "Human Starting Area - Acolyte Trainer", classid 3
ACOLYTE_CLASS_ID = 3
ACOLYTE_CLASSMASK = 1 << (ACOLYTE_CLASS_ID - 1)
TRAINER_ROWS = [
    # spell, cost (copper), required class level
    (BIND_SHADOW, 50, 3),
    (SHADOW_LORE_MURKBIND, 800, 8),
]

MAP_ID = 0
SPAWN_NAME = "Barrowfield - Ilsa Mourne 01"
# West edge of Barrowfield, behind the Gravebound Cultists and 64 m from Barrowfront Camp
# (worldkit query: placeable, slope 8.5 deg, dry).
SPAWN_POSITION = (-500.0, -0.317, 262.0)

# --- Enum values (src/shared/game/spell.h, aura.h, spell_target_map.h, pet_defines.h) --------
SCHOOL_DAMAGE, APPLY_AURA, LEARN_SPELL = 2, 6, 31
TAME_CREATURE, SUMMON_PET, LEARN_PET_SPELL = 42, 43, 44
SUMMON_PERSISTENT_PET, GRANT_PERSISTENT_PET, DISMISS_PET, REVIVE_PET_EFFECT = 58, 59, 60, 61
MOD_DAMAGE_DONE_PCT, MOD_ROOT = 23, 25
CASTER, TARGET_ENEMY = 0, 5

ABILITY, NEGATIVE, NOT_IN_COMBAT = 0x10, 0x04000000, 0x10000000

NAME_FIXED, NAME_RANDOM = 0, 1
LEVEL_OWN, LEVEL_MASTER = 0, 1
SOURCE_CREATURE, SOURCE_CHARACTER, SOURCE_CLASS = 0, 1, 2
REPLACE_OLDEST = 0

SHADOW = 5
RANGE_MEDIUM, RANGE_25 = 2, 4          # 35 yd, 25 yd

UNDEAD_FACTION = 4                     # "Undead Dungeon" (hostile)
WARRIOR_UNIT_CLASS, MAGE_UNIT_CLASS = 1, 2
SKELETON_MODEL, HUMAN_FEMALE_MODEL, UNDEAD_FEMALE_MODEL = 6, 3, 37

# Existing spells reused by the creatures.
STRIKE, DODGE, PARRY_LIKE_35, PASSIVE_36, SHADOWBOLT_R1 = 8, 37, 35, 36, 23

# Existing visualizations (author_acolyte_visuals.py and the Hollow Choir).
VIS_LAST_VIGIL, VIS_SHADOWBOLT, VIS_CORRUPTION, VIS_FEAR = 81, 130, 131, 132
VIS_UMBRAL_BLOOM, VIS_EMBRACE, VIS_GLOAMING, VIS_RESURRECTION = 134, 135, 136, 21

DE, EN, FR, RU = 1, 2, 3, 4
ICON_DIR = "Interface/Icons/Spells/"

SHADE_NAMES = ["Vesper", "Nyxa", "Morrow", "Sable", "Thren", "Quillon", "Ashmote", "Glimmer"]


def loc(de, en, fr, ru):
    return [{"locale": DE, "value": de}, {"locale": EN, "value": en},
            {"locale": FR, "value": fr}, {"locale": RU, "value": ru}]


def spell_texts(name, description, auratext=None):
    """Base strings are English; every field carries all four locales."""
    result = {"name": name[1], "name_loc": loc(*name),
              "description": description[1], "description_loc": loc(*description)}
    if auratext:
        result.update({"auratext": auratext[1], "auratext_loc": loc(*auratext)})
    return result


def spell(spell_id, icon, school=SHADOW, positive=1, classmask=0, attributes=(0, 0), **fields):
    draft = {"id": spell_id, "baseid": 0, "rank": 0, "baselevel": 1, "spelllevel": 1,
             "classmask": classmask, "positive": positive, "spellSchool": school,
             "icon": ICON_DIR + icon, "attributes": list(attributes), "cooldownflags": 3}
    draft.update(fields)
    return draft


def spell_definitions():
    result = []

    raise_skeletons = spell(
        RAISE_SKELETONS, "T_Icon_Unholy_12.htex", casttime=2000, maxlevel=60,
        visualization_id=VIS_LAST_VIGIL,
        effects=[{"index": 0, "type": SUMMON_PET, "basepoints": 3, "targeta": CASTER,
                  "radius": 4.0, "summonunit": RISEN_SKELETON, "pet_max_count": 5,
                  "pet_limit_policy": REPLACE_OLDEST, "pet_level_source": SOURCE_CHARACTER}])
    raise_skeletons.update(spell_texts(
        ("Skelette erheben", "Raise Skeletons", "Relever les squelettes", "Поднять скелетов"),
        ("Ruft drei Skelette aus der Erde, die für den Zaubernden kämpfen. Höchstens fünf "
         "dienen gleichzeitig; weitere ersetzen die ältesten.",
         "Calls three skeletons from the earth to fight for the caster. At most five serve at "
         "once; more replace the oldest.",
         "Fait surgir trois squelettes de la terre pour combattre aux côtés de l'incantateur. "
         "Cinq au plus servent à la fois ; les suivants remplacent les plus anciens.",
         "Призывает из земли трёх скелетов, сражающихся за заклинателя. Одновременно служат "
         "не более пяти; новые заменяют самых старых.")))
    result.append(raise_skeletons)

    bind_shadow = spell(
        BIND_SHADOW, "T_Icon_Shadow_101.htex", classmask=ACOLYTE_CLASSMASK,
        attributes=(ABILITY | NOT_IN_COMBAT, 0), casttime=3000, interruptflags=5,
        visualization_id=VIS_GLOAMING,
        effects=[
            {"index": 0, "type": GRANT_PERSISTENT_PET, "targeta": CASTER, "miscvaluea": SHADE_PET},
            {"index": 1, "type": LEARN_SPELL, "targeta": CASTER, "triggerspell": SUMMON_SHADOW},
            {"index": 2, "type": LEARN_SPELL, "targeta": CASTER, "triggerspell": DISMISS_SHADOW},
        ])
    bind_shadow.update(spell_texts(
        ("Schatten binden", "Bind Shadow", "Lier une ombre", "Связать тень"),
        ("Bindet ein Umbralschemen an deinen Willen. Es folgt dir fortan als Begleiter und "
         "lehrt dich Schatten beschwören und Schatten entlassen.",
         "Binds an umbral shade to your will. It follows you as a companion from now on and "
         "teaches you Summon Shadow and Dismiss Shadow.",
         "Lie une ombre umbrale à votre volonté. Elle vous accompagne désormais et vous "
         "enseigne Invoquer l'ombre et Renvoyer l'ombre.",
         "Подчиняет вашей воле умбральную тень. Отныне она сопровождает вас, а вы узнаёте "
         "заклинания «Призвать тень» и «Отпустить тень».")))
    result.append(bind_shadow)

    summon_shadow = spell(
        SUMMON_SHADOW, "T_Icon_Shadow_102.htex", classmask=ACOLYTE_CLASSMASK,
        attributes=(ABILITY, 0), casttime=3000, interruptflags=5, cost=40,
        visualization_id=VIS_EMBRACE,
        effects=[{"index": 0, "type": SUMMON_PERSISTENT_PET, "targeta": CASTER,
                  "miscvaluea": SHADE_PET}])
    summon_shadow.update(spell_texts(
        ("Schatten beschwören", "Summon Shadow", "Invoquer l'ombre", "Призвать тень"),
        ("Ruft dein gebundenes Umbralschemen an deine Seite.",
         "Calls your bound umbral shade to your side.",
         "Appelle votre ombre umbrale liée à vos côtés.",
         "Призывает к вам связанную умбральную тень.")))
    result.append(summon_shadow)

    dismiss_shadow = spell(
        DISMISS_SHADOW, "T_Icon_Shadow_104.htex", classmask=ACOLYTE_CLASSMASK,
        effects=[{"index": 0, "type": DISMISS_PET, "targeta": CASTER}])
    dismiss_shadow.update(spell_texts(
        ("Schatten entlassen", "Dismiss Shadow", "Renvoyer l'ombre", "Отпустить тень"),
        ("Schickt dein Umbralschemen in die Dunkelheit zurück, bis du es wieder rufst.",
         "Sends your umbral shade back into the dark until you call it again.",
         "Renvoie votre ombre umbrale dans les ténèbres jusqu'à ce que vous l'appeliez de "
         "nouveau.",
         "Отправляет умбральную тень обратно во тьму, пока вы снова её не призовёте.")))
    result.append(dismiss_shadow)

    gloom_dart = spell(
        GLOOM_DART, "T_Icon_Shadow_105.htex", positive=0, casttime=2000, speed=20.0,
        maxlevel=60, facing=1, interruptflags=5, rangetype=RANGE_MEDIUM,
        visualization_id=VIS_SHADOWBOLT,
        effects=[{"index": 0, "type": SCHOOL_DAMAGE, "basepoints": 10, "diesides": 5,
                  "pointsperlevel": 1.2, "targeta": TARGET_ENEMY}])
    gloom_dart.update(spell_texts(
        ("Düsterpfeil", "Gloom Dart", "Dard de pénombre", "Сумрачный дротик"),
        ("Schleudert einen Splitter verdichteter Düsternis auf das Ziel und verursacht $s0 "
         "Schattenschaden.",
         "Hurls a shard of condensed gloom at the target, dealing $s0 shadow damage.",
         "Projette un éclat de pénombre condensée sur la cible, infligeant $s0 points de "
         "dégâts d'ombre.",
         "Бросает в цель осколок сгущённого сумрака, нанося $s0 ед. урона от темной магии.")))
    result.append(gloom_dart)

    sapping_murk = spell(
        SAPPING_MURK, "T_Icon_Shadow_106.htex", positive=0, attributes=(NEGATIVE, 0),
        cooldown=15000, duration=8000, facing=1, rangetype=RANGE_MEDIUM,
        visualization_id=VIS_CORRUPTION,
        effects=[{"index": 0, "type": APPLY_AURA, "aura": MOD_DAMAGE_DONE_PCT,
                  "basepoints": -15, "targeta": TARGET_ENEMY}])
    sapping_murk.update(spell_texts(
        ("Zehrender Dunst", "Sapping Murk", "Brume affaiblissante", "Истощающая мгла"),
        ("Hüllt das Ziel in zehrenden Dunst und verringert seinen verursachten Schaden $D "
         "lang um 15%.",
         "Wraps the target in sapping murk, reducing the damage it deals by 15% for $D.",
         "Enveloppe la cible d'une brume affaiblissante qui réduit de 15% les dégâts qu'elle "
         "inflige pendant $D.",
         "Окутывает цель истощающей мглой, снижая наносимый ею урон на 15% на $D."),
        ("Verursachter Schaden um 15% verringert.", "Damage dealt reduced by 15%.",
         "Dégâts infligés réduits de 15%.", "Наносимый урон снижен на 15%.")))
    result.append(sapping_murk)

    umbral_lash = spell(
        UMBRAL_LASH, "T_Icon_Shadow_107.htex", positive=0, cooldown=8000, maxlevel=60,
        facing=1, rangetype=RANGE_MEDIUM, visualization_id=VIS_UMBRAL_BLOOM,
        effects=[{"index": 0, "type": SCHOOL_DAMAGE, "basepoints": 14, "diesides": 6,
                  "pointsperlevel": 1.4, "targeta": TARGET_ENEMY}])
    umbral_lash.update(spell_texts(
        ("Umbralpeitsche", "Umbral Lash", "Fouet umbral", "Умбральный хлыст"),
        ("Peitscht das Ziel mit einer Strähne aus Schatten und verursacht sofort $s0 "
         "Schattenschaden.",
         "Lashes the target with a strand of shadow, instantly dealing $s0 shadow damage.",
         "Fouette la cible d'un filament d'ombre, lui infligeant instantanément $s0 points "
         "de dégâts d'ombre.",
         "Хлещет цель нитью тени, мгновенно нанося $s0 ед. урона от темной магии.")))
    result.append(umbral_lash)

    murkbind = spell(
        MURKBIND, "T_Icon_Shadow_108.htex", positive=0, attributes=(NEGATIVE, 0),
        cooldown=20000, duration=4000, casttime=1000, facing=1, interruptflags=5,
        rangetype=RANGE_25, visualization_id=VIS_FEAR,
        effects=[{"index": 0, "type": APPLY_AURA, "aura": MOD_ROOT, "targeta": TARGET_ENEMY}])
    murkbind.update(spell_texts(
        ("Dunstfessel", "Murkbind", "Entrave de brume", "Путы мглы"),
        ("Fesselt das Ziel $D lang mit Schatten an den Boden.",
         "Binds the target to the ground with shadow for $D.",
         "Entrave la cible au sol avec des ombres pendant $D.",
         "Приковывает цель тенями к земле на $D."),
        ("Von Schatten gefesselt.", "Bound by shadow.", "Entravé par les ombres.",
         "Скован тенями.")))
    result.append(murkbind)

    shadow_lore = spell(
        SHADOW_LORE_MURKBIND, "T_Icon_Shadow_109.htex", classmask=ACOLYTE_CLASSMASK,
        casttime=1500, interruptflags=5,
        effects=[{"index": 0, "type": LEARN_PET_SPELL, "targeta": CASTER,
                  "triggerspell": MURKBIND}])
    shadow_lore.update(spell_texts(
        ("Schattenkunde: Dunstfessel", "Shadow Lore: Murkbind", "Savoir de l'ombre : Entrave "
         "de brume", "Знание теней: Путы мглы"),
        ("Lehrt dein beschworenes Umbralschemen Dunstfessel.",
         "Teaches your summoned umbral shade Murkbind.",
         "Enseigne Entrave de brume à votre ombre umbrale invoquée.",
         "Обучает призванную умбральную тень заклинанию «Путы мглы».")))
    result.append(shadow_lore)

    tame_beast = spell(
        TAME_BEAST, "T_Icon_Nature_101.htex", school=0, positive=0, casttime=5000,
        interruptflags=5, rangetype=RANGE_25,
        effects=[{"index": 0, "type": TAME_CREATURE, "targeta": TARGET_ENEMY}])
    tame_beast.update(spell_texts(
        ("Bestie zähmen", "Tame Beast", "Apprivoiser une bête", "Приручить зверя"),
        ("Zähmt die Zielkreatur, die dir danach als Begleiter folgt. Testzauber.",
         "Tames the target creature, which follows you as a companion afterwards. Test spell.",
         "Apprivoise la créature ciblée, qui vous accompagne ensuite. Sort de test.",
         "Приручает выбранное существо, которое затем сопровождает вас. Тестовое "
         "заклинание.")))
    result.append(tame_beast)

    call_pet = spell(
        CALL_PET, "T_Icon_Nature_102.htex", school=0, casttime=1500, interruptflags=5,
        effects=[{"index": 0, "type": SUMMON_PERSISTENT_PET, "targeta": CASTER,
                  "miscvaluea": 0}])
    call_pet.update(spell_texts(
        ("Begleiter rufen", "Call Pet", "Appeler le familier", "Позвать питомца"),
        ("Ruft den zuletzt beschworenen Begleiter herbei. Testzauber.",
         "Calls the companion you summoned last. Test spell.",
         "Appelle le dernier compagnon que vous avez invoqué. Sort de test.",
         "Призывает последнего призванного спутника. Тестовое заклинание.")))
    result.append(call_pet)

    revive_pet = spell(
        REVIVE_PET, "T_Icon_Nature_103.htex", school=0, casttime=5000, interruptflags=5,
        visualization_id=VIS_RESURRECTION,
        effects=[{"index": 0, "type": REVIVE_PET_EFFECT, "basepoints": 50, "targeta": CASTER,
                  "miscvaluea": 0}])
    revive_pet.update(spell_texts(
        ("Begleiter wiederbeleben", "Revive Pet", "Ranimer le familier", "Оживить питомца"),
        ("Belebt deinen toten Begleiter mit $s0% seiner Gesundheit wieder. Testzauber.",
         "Revives your dead companion with $s0% of its health. Test spell.",
         "Ranime votre compagnon mort avec $s0% de sa vie. Sort de test.",
         "Оживляет погибшего спутника с $s0% здоровья. Тестовое заклинание.")))
    result.append(revive_pet)

    return result


def unit_names(name, subname=None):
    result = {"name": name[1], "name_loc": loc(*name)}
    if subname:
        result.update({"subname": subname[1], "subname_loc": loc(*subname)})
    return result


def unit_definitions():
    skeleton = {
        "id": RISEN_SKELETON, "minlevel": 8, "maxlevel": 10, "factionTemplate": UNDEAD_FACTION,
        "maleModel": SKELETON_MODEL, "femaleModel": SKELETON_MODEL, "type": 0, "family": 0,
        "minlevelhealth": 180, "maxlevelhealth": 220, "minmeleedmg": 7.0, "maxmeleedmg": 11.0,
        "regeneration": 3, "unitClassId": WARRIOR_UNIT_CLASS, "baseArmor": 84,
        "armorPerLevel": 50.0, "useStatBasedSystem": True,
        "creaturespells": [{"spellid": PARRY_LIKE_35}, {"spellid": DODGE},
                           {"spellid": PASSIVE_36}, {"spellid": STRIKE, "mincooldown": 0}],
    }
    skeleton.update(unit_names(("Erhobenes Skelett", "Risen Skeleton", "Squelette relevé",
                                "Поднятый скелет")))

    gravecaller = {
        "id": GRAVECALLER, "minlevel": 10, "maxlevel": 10, "factionTemplate": UNDEAD_FACTION,
        "maleModel": HUMAN_FEMALE_MODEL, "femaleModel": HUMAN_FEMALE_MODEL, "type": 0,
        "family": 0, "minlevelhealth": 260, "maxlevelhealth": 260, "minmeleedmg": 9.0,
        "maxmeleedmg": 13.0, "minlevelxp": 140, "maxlevelxp": 140, "regeneration": 3,
        "unitClassId": MAGE_UNIT_CLASS, "baseDamageVariance": 0.18, "damagePerLevel": 1.0,
        "baseArmor": 175, "armorPerLevel": 42.0, "useStatBasedSystem": True,
        "unitlootentries": [16, 18, 20, 19, 25],
        "creaturespells": [
            {"spellid": RAISE_SKELETONS, "priority": 200, "mininitialcooldown": 500,
             "maxinitialcooldown": 2000, "mincooldown": 25000, "maxcooldown": 30000,
             "maxrange": 40.0},
            {"spellid": SHADOWBOLT_R1, "priority": 100, "mincooldown": 3000,
             "maxcooldown": 5000},
            {"spellid": DODGE},
        ],
    }
    gravecaller.update(unit_names(
        ("Ilsa Mourne", "Ilsa Mourne", "Ilsa Mourne", "Ильза Морн"),
        ("Gräberruferin", "Gravecaller", "Invocatrice des tombes", "Призывательница могил")))

    shade = {
        "id": UMBRAL_SHADE, "minlevel": 1, "maxlevel": 60, "factionTemplate": UNDEAD_FACTION,
        "maleModel": UNDEAD_FEMALE_MODEL, "femaleModel": UNDEAD_FEMALE_MODEL, "scale": 0.8,
        "type": 0, "family": 0, "minlevelhealth": 60, "maxlevelhealth": 600,
        "minmeleedmg": 3.0, "maxmeleedmg": 5.0, "regeneration": 3,
        "unitClassId": MAGE_UNIT_CLASS, "baseDamageVariance": 0.15, "damagePerLevel": 0.8,
        "baseArmor": 60, "armorPerLevel": 30.0, "useStatBasedSystem": True,
        "creaturespells": [{"spellid": DODGE}],
    }
    shade.update(unit_names(("Umbralschemen", "Umbral Shade", "Ombre umbrale",
                             "Умбральная тень")))

    return [skeleton, gravecaller, shade]


def pet_definitions():
    shade = {
        "id": SHADE_PET, "name": "Umbral Shade",
        "name_loc": loc("Umbralschemen", "Umbral Shade", "Ombre umbrale", "Умбральная тень"),
        "unit_entry": UMBRAL_SHADE, "name_mode": NAME_RANDOM, "random_names": SHADE_NAMES,
        "allow_player_naming": True, "level_mode": LEVEL_MASTER, "level_source": SOURCE_CLASS,
        "default_spells": [GLOOM_DART, SAPPING_MURK],
        "level_spells": [{"spell": UMBRAL_LASH, "level": 6}],
        "ai_spells": [
            {"spellid": GLOOM_DART, "priority": 50, "mincooldown": 0, "maxcooldown": 0},
            {"spellid": SAPPING_MURK, "priority": 150, "mincooldown": 15000,
             "maxcooldown": 18000},
            {"spellid": UMBRAL_LASH, "priority": 120, "mincooldown": 8000, "maxcooldown": 10000},
            {"spellid": MURKBIND, "priority": 100, "mincooldown": 20000, "maxcooldown": 25000},
        ],
    }
    wolf = {
        "id": WOLF_PET, "name": "Wolf",
        "name_loc": loc("Wolf", "Wolf", "Loup", "Волк"),
        "unit_entry": TAMABLE_UNIT, "name_mode": NAME_FIXED, "allow_player_naming": True,
        "level_mode": LEVEL_OWN, "level_source": SOURCE_CHARACTER,
    }
    return [shade, wolf]


def spawn_definition():
    x, y, z = SPAWN_POSITION
    return {"name": SPAWN_NAME, "respawn": True, "respawndelay": 60000, "positionx": x,
            "positiony": y, "positionz": z, "rotation": 1.57, "unitentry": GRAVECALLER,
            "movement": "STATIONARY", "maxcount": 1}


# --- Data access -----------------------------------------------------------------------------

def _require(condition, message):
    """Like assert, but survives ``python -O`` -- these guards gate writing game data."""
    if not condition:
        raise SystemExit(message)


EDITOR_TYPES = {
    "spells": ("spells.proto", "mmo.proto.Spells"),
    "units": ("units.proto", "mmo.proto.Units"),
    "pets": ("pets.proto", "mmo.proto.Pets"),
    "trainers": ("trainers.proto", "mmo.proto.Trainers"),
    "maps": ("maps.proto", "mmo.proto.Maps"),
}
# Managers mmo_edit's ExportToClient copies into ClientDB.
CLIENT_SHARED = ("spells", "maps")

_TYPE_CACHE = {}


def load_type(name):
    if name in _TYPE_CACHE:
        return _TYPE_CACHE[name]
    proto, message_name = EDITOR_TYPES[name]
    schema_dir = ROOT / "src/shared/proto_data"
    with tempfile.TemporaryDirectory(prefix="pets_") as tmp:
        desc = Path(tmp) / "d.pb"
        subprocess.run([str(find_protoc(ROOT)), f"-I{schema_dir}",
                        f"--descriptor_set_out={desc}", "--include_imports", proto],
                       cwd=schema_dir, check=True)
        file_set = descriptor_pb2.FileDescriptorSet.FromString(desc.read_bytes())
    pool = descriptor_pool.DescriptorPool()
    for entry in file_set.file:
        pool.Add(entry)
    message_type = message_factory.GetMessageClass(pool.FindMessageTypeByName(message_name))
    _TYPE_CACHE[name] = message_type
    return message_type


def editor_path(name):
    return ROOT / f"data/editor/data/{name}.data"


def client_path(name):
    return ROOT / f"data/client/ClientDB/{name}.data"


PROJECT_TXT = ROOT / "data/editor/data/project.txt"
PETS_PROJECT_LINE = b'pets = (file = "pets.data")'
# ProjectSaver writes the managers in this order; pets follow emotes.
PETS_PROJECT_ANCHOR = b'emotes = (file = "emotes.data")'


def load_editor(name):
    message_type = load_type(name)
    path = editor_path(name)
    if not path.is_file():
        return message_type()
    return message_type.FromString(path.read_bytes())


def project_txt_with_pets(text):
    """project.txt with the pets manager registered (unchanged if it already is)."""
    if PETS_PROJECT_LINE in text:
        return text
    _require(PETS_PROJECT_ANCHOR in text, "project.txt has no emotes entry to anchor pets on")
    newline = b"\r\n" if b"\r\n" in text else b"\n"
    return text.replace(PETS_PROJECT_ANCHOR, PETS_PROJECT_ANCHOR + newline + PETS_PROJECT_LINE, 1)


# --- Upserts ---------------------------------------------------------------------------------

def upsert_by_id(dataset, drafts, kind, keep=()):
    """Replaces rows by id. Fields listed in keep survive a re-run (owned by other scripts)."""
    by_id = {row.id: row for row in dataset.entry}
    for draft in drafts:
        target = by_id.get(draft["id"])
        kept = {}
        if target is None:
            target = dataset.entry.add()
        else:
            _require(target.name == draft["name"],
                     f"{kind} {draft['id']} is {target.name!r}, refusing to overwrite it "
                     f"with {draft['name']!r}")
            kept = {field: getattr(target, field) for field in keep if target.HasField(field)}
        target.Clear()
        json_format.ParseDict(draft, target)
        for field, value in kept.items():
            setattr(target, field, value)


def set_tamable(units):
    wolf = next((unit for unit in units.entry if unit.id == TAMABLE_UNIT), None)
    _require(wolf is not None, f"unit {TAMABLE_UNIT} is missing")
    _require(wolf.name == TAMABLE_UNIT_NAME,
             f"unit {TAMABLE_UNIT} is {wolf.name!r}, expected {TAMABLE_UNIT_NAME!r}")
    wolf.tame_pet_entry = WOLF_PET


def upsert_trainer_rows(trainers):
    trainer = next((entry for entry in trainers.entry if entry.id == ACOLYTE_TRAINER), None)
    _require(trainer is not None, f"trainer {ACOLYTE_TRAINER} is missing")
    _require(trainer.classid == ACOLYTE_CLASS_ID and "Acolyte" in trainer.name,
             f"trainer {ACOLYTE_TRAINER} is {trainer.name!r} (class {trainer.classid}), "
             "expected the acolyte trainer")
    ours = {row[0] for row in TRAINER_ROWS}
    kept = [row for row in trainer.spells if row.spell not in ours]
    del trainer.spells[:]
    for row in kept:
        trainer.spells.add().CopyFrom(row)
    for spell_id, cost, level in TRAINER_ROWS:
        row = trainer.spells.add()
        row.spell = spell_id
        row.spellcost = cost
        row.reqskill = 0
        row.reqskillval = 0
        row.reqlevel = level


def upsert_spawn(maps):
    world = next((entry for entry in maps.entry if entry.id == MAP_ID), None)
    _require(world is not None, f"map {MAP_ID} is missing")
    draft = spawn_definition()
    existing = [spawn for spawn in world.unitspawns if spawn.name == SPAWN_NAME]
    _require(len(existing) <= 1, f"spawn {SPAWN_NAME!r} exists more than once")
    target = existing[0] if existing else world.unitspawns.add()
    target.Clear()
    json_format.ParseDict(draft, target)


# --- Validation ------------------------------------------------------------------------------

PET_NAME = re.compile(r"^[A-Za-z]{1,12}$")


def _locales(entries):
    return sorted(entry.locale for entry in entries)


def validate(data):
    spells, units, pets, trainers, maps = (data[name] for name in EDITOR_TYPES)
    spells_by_id = {spell.id: spell for spell in spells.entry}
    units_by_id = {unit.id: unit for unit in units.entry}
    pets_by_id = {pet.id: pet for pet in pets.entry}

    for name, dataset in data.items():
        ids = [row.id for row in dataset.entry]
        _require(len(ids) == len(set(ids)), f"duplicate ids in {name}")
        _require(dataset.IsInitialized(), f"{name} dataset is missing required fields")

    for spell_id in SPELL_IDS:
        spell = spells_by_id.get(spell_id)
        _require(spell is not None, f"spell {spell_id} missing after upsert")
        _require((ROOT / "data/client" / spell.icon).is_file(), f"missing icon {spell.icon}")
        _require(not spell.reagents, f"spell {spell_id} must not need reagents")
        for field in ("name_loc", "description_loc"):
            _require(_locales(getattr(spell, field)) == [DE, EN, FR, RU],
                     f"spell {spell_id}.{field} lacks a locale")
        if spell.auratext:
            _require(_locales(spell.auratext_loc) == [DE, EN, FR, RU],
                     f"spell {spell_id}.auratext_loc lacks a locale")
        for label, text in spell_text_data.spell_texts(spell):
            problems = spell_text_data.placeholder_problems(text, spell, spells_by_id)
            _require(not problems, f"spell {spell_id} {label}: {problems}")
        for effect in spell.effects:
            if effect.triggerspell:
                _require(effect.triggerspell in spells_by_id,
                         f"spell {spell_id} triggers unknown spell {effect.triggerspell}")
            if effect.summonunit:
                _require(effect.summonunit in units_by_id,
                         f"spell {spell_id} summons unknown unit {effect.summonunit}")
            if effect.type in (SUMMON_PERSISTENT_PET, GRANT_PERSISTENT_PET) and effect.miscvaluea:
                _require(effect.miscvaluea in pets_by_id,
                         f"spell {spell_id} names unknown pet {effect.miscvaluea}")

    # Pet abilities are cast by a creature: no class restriction may stand in the way.
    for spell_id in (GLOOM_DART, SAPPING_MURK, UMBRAL_LASH, MURKBIND, RAISE_SKELETONS):
        _require(spells_by_id[spell_id].classmask == 0, f"creature spell {spell_id} has a classmask")
    for spell_id in (TAME_BEAST, CALL_PET, REVIVE_PET):
        _require(spells_by_id[spell_id].classmask == 0, f"test spell {spell_id} has a classmask")
        for trainer in trainers.entry:
            _require(all(row.spell != spell_id for row in trainer.spells),
                     f"test spell {spell_id} is sold by trainer {trainer.id}")

    for unit_id in UNIT_IDS:
        unit = units_by_id.get(unit_id)
        _require(unit is not None, f"unit {unit_id} missing after upsert")
        _require(_locales(unit.name_loc) == [DE, EN, FR, RU], f"unit {unit_id} lacks a locale")
        for creature_spell in unit.creaturespells:
            _require(creature_spell.spellid in spells_by_id,
                     f"unit {unit_id} casts unknown spell {creature_spell.spellid}")
    _require(any(cs.spellid == RAISE_SKELETONS for cs in units_by_id[GRAVECALLER].creaturespells),
             "the Gravecaller does not cast Raise Skeletons")
    _require(units_by_id[TAMABLE_UNIT].tame_pet_entry == WOLF_PET, "the wolf is not tamable")

    for pet_id in PET_IDS:
        pet = pets_by_id.get(pet_id)
        _require(pet is not None, f"pet {pet_id} missing after upsert")
        _require(pet.unit_entry in units_by_id, f"pet {pet_id} names unknown unit {pet.unit_entry}")
        _require(_locales(pet.name_loc) == [DE, EN, FR, RU], f"pet {pet_id} lacks a locale")
        for name in pet.random_names:
            _require(PET_NAME.match(name), f"pet {pet_id} random name {name!r} is not a valid name")
        spell_ids = list(pet.default_spells) + [row.spell for row in pet.level_spells]
        spell_ids += [row.spellid for row in pet.ai_spells]
        for spell_id in spell_ids:
            _require(spell_id in spells_by_id, f"pet {pet_id} knows unknown spell {spell_id}")
            _require(spells_by_id[spell_id].classmask == 0,
                     f"pet {pet_id} spell {spell_id} has a classmask")

    world = next(entry for entry in maps.entry if entry.id == MAP_ID)
    spawns = [spawn for spawn in world.unitspawns if spawn.name == SPAWN_NAME]
    _require(len(spawns) == 1 and spawns[0].unitentry == GRAVECALLER, "Gravecaller spawn missing")

    trainer = next(entry for entry in trainers.entry if entry.id == ACOLYTE_TRAINER)
    sold = [row.spell for row in trainer.spells]
    for spell_id, _, _ in TRAINER_ROWS:
        _require(sold.count(spell_id) == 1, f"trainer sells spell {spell_id} {sold.count(spell_id)} times")


def build():
    data = {name: load_editor(name) for name in EDITOR_TYPES}
    upsert_by_id(data["spells"], spell_definitions(), "spell")
    upsert_by_id(data["units"], unit_definitions(), "unit")
    upsert_by_id(data["pets"], pet_definitions(), "pet")
    set_tamable(data["units"])
    upsert_trainer_rows(data["trainers"])
    upsert_spawn(data["maps"])
    validate(data)
    return data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    data = build()
    project_txt = project_txt_with_pets(PROJECT_TXT.read_bytes())

    if not args.apply:
        print(f"validated spells {SPELL_IDS[0]}-{SPELL_IDS[-1]}, units {UNIT_IDS}, pets {PET_IDS}, "
              f"tamable unit {TAMABLE_UNIT}, trainer {ACOLYTE_TRAINER} rows and spawn {SPAWN_NAME!r}")
        return

    OUT.mkdir(parents=True, exist_ok=True)
    backup = OUT / ("backup_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    backup.mkdir()
    for name in EDITOR_TYPES:
        if editor_path(name).is_file():
            shutil.copy2(editor_path(name), backup / f"editor_{name}.data")
    for name in CLIENT_SHARED:
        shutil.copy2(client_path(name), backup / f"client_{name}.data")
    shutil.copy2(PROJECT_TXT, backup / "project.txt")

    for name, dataset in data.items():
        blob = dataset.SerializeToString()
        if not editor_path(name).is_file() or editor_path(name).read_bytes() != blob:
            editor_path(name).write_bytes(blob)
        if name in CLIENT_SHARED and client_path(name).read_bytes() != blob:
            client_path(name).write_bytes(blob)
    if PROJECT_TXT.read_bytes() != project_txt:
        PROJECT_TXT.write_bytes(project_txt)

    print(f"wrote pet content to editor ({', '.join(EDITOR_TYPES)}, project.txt) and ClientDB "
          f"({', '.join(CLIENT_SHARED)}). Backup: {backup}")


if __name__ == "__main__":
    main()
