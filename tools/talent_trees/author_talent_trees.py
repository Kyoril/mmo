# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the constellation talent trees of every class (talents, talent tabs and their spells).

    py tools/talent_trees/author_talent_trees.py              # validate only
    py tools/talent_trees/author_talent_trees.py --summary    # print the per-class overview
    py tools/talent_trees/author_talent_trees.py --apply      # write editor + ClientDB

Idempotent: re-running rewrites the same talent and spell ids. ClientDB receives a byte copy
of the editor blobs, exactly what mmo_edit's ExportToClient does, so the two cannot drift.

Layout: each class tab has a hub (the class emblem) and three paths 120 degrees apart. Every
path has nine nodes on four rings (see SLOTS): three roots on the inner ring connected to the
hub, two flank nodes plus the spine node on the second ring, two flank nodes on the third and
the capstone on the outer ring. A capstone costs 6 of the 9 talent points a class earns, so
a player completes one path and dips into a second.

Design rules that are easy to undo by accident:

* One point = one noticeable effect. At most two ranks, and rarely.
* Real talents use only mechanics the server runs: spell modifiers (matched by family flags),
  stat auras, ProcTriggerSpell and active spells. Anything else is a placeholder: shown in the
  tree with its planned effect, refused by the server, dimmed by the client.
* Single-rank talent spells use rank 0 and baseid 0 (no "Rank 1" line in the tooltip).
* Descriptions use literal numbers for modifiers: the tooltip formatter prints $s raw, so a
  negative cast time modifier would read "-400".
* Old rank spells that a talent no longer uses are neutered (effects removed). Characters who
  learned them keep the spell, so it must not keep working; the server resets talents whose
  saved rank no longer exists.
"""

import argparse
import math
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

OUT = ROOT / "generated/talent_trees"
ICONS = "Interface/Icons/Spells/"

# --- Enum values (src/shared/game/spell.h, aura.h, spell_target_map.h) -------------------------
APPLY_AURA, SCHOOL_DAMAGE, HEAL, APPLY_AREA_AURA = 6, 2, 10, 49
MOD_STAT_PCT, ADD_FLAT, ADD_PCT, PROC_TRIGGER = 31, 18, 19, 12
PERIODIC_DAMAGE, MOD_ROOT, MOD_STUN, MOD_CRIT_TAKEN = 13, 25, 27, 40
DMG_DONE_PCT, DMG_TAKEN_PCT, MOD_DODGE, MOD_POWER_REGEN_PCT, MOD_RESIST_PCT = 23, 24, 36, 39, 30
OP_DAMAGE, OP_DURATION, OP_RANGE, OP_CRIT, OP_ALL_EFFECTS = 0, 1, 4, 6, 7
OP_CAST, OP_COOLDOWN, OP_COST, OP_CRIT_DAMAGE, OP_PERIODIC = 9, 10, 11, 12, 18
STA, STR, AGI, INT, SPI = 0, 1, 2, 3, 4
POWER_MANA, POWER_RAGE, POWER_ENERGY = 0, 1, 2
KILL, DONE_MELEE_SPELL, DONE_MAGIC_NEG, DONE_PERIODIC = 0x2, 0x10, 0x10000, 0x40000
EX_CRIT = 0x2
T_CASTER, T_ENEMY, T_SOURCE_AREA_ENEMY, T_CASTER_AREA_PARTY, T_INSTIGATOR = 0, 5, 9, 13, 18
SCHOOL_PHYSICAL, SCHOOL_HOLY, SCHOOL_FIRE, SCHOOL_FROST, SCHOOL_SHADOW, SCHOOL_ARCANE = 0, 1, 2, 4, 5, 6
MECHANIC_STUN, MECHANIC_ROOT = 10, 11

# Attribute words, copied from working spells of the same kind.
PASSIVE_ATTRS = [0x80000050, 134]      # talent passives (Burning Soul)
DEBUFF_ATTRS = [0, 128]                # proc debuffs on the victim (Chilled)
BUFF_ATTRS = [0x18, 128]               # proc buffs on the caster (Void Resurgence buff)
SELF_ACTIVE_ATTRS = [16, 132]          # self-cast cooldowns (Last Stand)
AOE_ACTIVE_ATTRS = [67108880, 4]       # instant AoE around the caster (Arcane Pulse)
HEAL_ACTIVE_ATTRS = [16, 0]            # cast-time heals (Healing Light)

DE, EN, FR, RU = 1, 2, 3, 4

# --- Layout ------------------------------------------------------------------------------------
CANVAS_W, CANVAS_H = 2300, 1420
HUB = (1150, 900)
# The talent window is about twice as wide as high, so the rings are ellipses: horizontal
# distances are stretched by this factor. It also gives the two lower paths, which run mostly
# sideways, the room they need.
STRETCH = 1.4
# slot -> (ring radius, angle offset within the path in degrees, required points, node scale,
#          parent slot or None for a hub root, tier)
SLOTS = {
    "A1": (245, -32, 0, 0.80, None, 0),
    "A2": (245, 0, 0, 0.95, None, 0),
    "A3": (245, 32, 0, 0.80, None, 0),
    "B1": (410, -19, 2, 0.80, "A1", 1),
    "M": (410, 0, 2, 0.95, "A2", 1),
    "B2": (410, 19, 2, 0.80, "A3", 1),
    "C1": (570, -16, 4, 0.80, "B1", 2),
    "C2": (570, 16, 4, 0.80, "B2", 2),
    "CAP": (720, 0, 5, 1.20, "M", 3),
}
PATH_ANGLES = (-90, 150, 30)    # top, lower left, lower right (screen space, y down)
# Path labels sit just outside their capstone: above it for the top path, below it otherwise.
LABEL_GAP = 125


def polar(radius, degrees):
    rad = math.radians(degrees)
    return round(HUB[0] + radius * math.cos(rad) * STRETCH), round(HUB[1] + radius * math.sin(rad))


def label_position(angle):
    x, y = polar(SLOTS["CAP"][0], angle)
    return x, y - LABEL_GAP if math.sin(math.radians(angle)) < 0 else y + LABEL_GAP


# --- Spell builders ----------------------------------------------------------------------------

def loc(texts):
    return [{"locale": locale, "value": value} for locale, value in zip((DE, EN, FR, RU), texts)]


def text_fields(name, description, auratext=None):
    fields = {"name": name[1], "name_loc": loc(name), "description": description[1],
              "description_loc": loc(description)}
    if auratext:
        fields.update({"auratext": auratext[1], "auratext_loc": loc(auratext)})
    return fields


def mod(op, mask, points, pct=False):
    return {"type": APPLY_AURA, "aura": ADD_PCT if pct else ADD_FLAT, "miscvaluea": op,
            "affectmask": str(mask), "basepoints": points, "targeta": T_CASTER}


def aura(aura_type, points=0, misc=0, target=T_CASTER):
    effect = {"type": APPLY_AURA, "aura": aura_type, "basepoints": points, "targeta": target}
    if misc:
        effect["miscvaluea"] = misc
    return effect


def proc(trigger_spell, target=T_INSTIGATOR):
    return {"type": APPLY_AURA, "aura": PROC_TRIGGER, "triggerspell": trigger_spell,
            "targeta": T_CASTER, "targetb": target}


class Spell:
    """A spell draft. ``existing`` drafts patch a live spell, new drafts replace the entry."""

    def __init__(self, spell_id, fields, existing=False, keep_effects=False):
        self.id = spell_id
        self.fields = fields
        self.existing = existing
        self.keep_effects = keep_effects


def passive(spell_id, name, description, icon, school, effects, **extra):
    fields = {"attributes": PASSIVE_ATTRS, "spellSchool": school, "icon": ICONS + icon,
              "effects": effects, **text_fields(name, description), **extra}
    return Spell(spell_id, fields)


def placeholder(spell_id, name, description, icon, school):
    return passive(spell_id, name, description, icon, school, [])


def patch(spell_id, name=None, description=None, effects=None, icon=None):
    """Patches an existing spell: optional new texts, effects and icon."""
    fields = {}
    if name and description:
        fields.update(text_fields(name, description))
    elif name:
        fields.update({"name": name[1], "name_loc": loc(name)})
    if effects is not None:
        fields["effects"] = effects
    if icon:
        fields["icon"] = ICONS + icon
    return Spell(spell_id, fields, existing=True, keep_effects=effects is None)


# --- Content -----------------------------------------------------------------------------------
# Each path: (label key, label texts, accent color, {slot: talent}).
# A talent: dict(id=talent id, ranks=[Spell or int spell id], placeholder=bool, extra=[Spell])
# ``extra`` holds spells the talent needs but does not rank in (proc results).

def talent(talent_id, *ranks, extra=(), is_placeholder=False):
    return {"id": talent_id, "ranks": list(ranks), "extra": list(extra), "placeholder": is_placeholder}


def ph(talent_id, spell):
    return talent(talent_id, spell, is_placeholder=True)


MAGE = {
    "class_id": 0, "tab_id": 3, "classmask": 0x80000000, "icon": "S_Class_Mage.htex",
    "paths": [
        ("TALENT_PATH_ARCANE", ("Äther", "Aether", "Éther", "Эфир"), 0xFFC08CFF, {
            "A1": talent(100, passive(1000,
                ("Klarer Geist", "Lucid Mind", "Esprit lucide", "Ясный разум"),
                ("Erhöht deine Intelligenz um $s0%.", "Increases your Intellect by $s0%.",
                 "Augmente votre Intelligence de $s0%.", "Увеличивает ваш интеллект на $s0%."),
                "T_Icon_Arcane_96.htex", SCHOOL_ARCANE, [aura(MOD_STAT_PCT, 8, INT)])),
            "A2": talent(27, patch(193,
                ("Bemessenes Wirken", "Measured Casting", "Incantation mesurée", "Размеренное колдовство"),
                ("Verringert die Manakosten deiner Arkanzauber um 20%.",
                 "Reduces the mana cost of your Arcane spells by 20%.",
                 "Réduit de 20% le coût en mana de vos sorts d'Arcanes.",
                 "Снижает затраты маны на ваши заклинания тайной магии на 20%."),
                [mod(OP_COST, 0x3C0, -20, pct=True)], icon="T_Icon_Arcane_15.htex")),
            "A3": talent(28, patch(196,
                ("Traumweber", "Dreamweaver", "Tisseur de rêves", "Ткач снов"),
                ("Verringert die Zauberzeit von Schlaf um 0,5 Sek. und erhöht seine Dauer um 30%.",
                 "Reduces the cast time of Sleep by 0.5 sec and increases its duration by 30%.",
                 "Réduit le temps d'incantation de Sommeil de 0,5 s et augmente sa durée de 30%.",
                 "Сокращает время произнесения «Сна» на 0,5 сек. и увеличивает его длительность на 30%."),
                [mod(OP_CAST, 0x40, -500), mod(OP_DURATION, 0x40, 30, pct=True)])),
            "B1": talent(101, passive(1001,
                ("Glyphe des Schweigens", "Silencing Glyph", "Glyphe de mutisme", "Глиф безмолвия"),
                ("Verringert die Abklingzeit von Arkane Störung um 10 Sek. und verlängert die Stille um 2 Sek.",
                 "Reduces the cooldown of Arcane Disruption by 10 sec and silences 2 sec longer.",
                 "Réduit le temps de recharge de Perturbation arcanique de 10 s et prolonge le silence de 2 s.",
                 "Сокращает время восстановления «Чародейского сбоя» на 10 сек. и продлевает немоту на 2 сек."),
                "T_Icon_Arcane_49.htex", SCHOOL_ARCANE,
                [mod(OP_COOLDOWN, 0x80, -10000), mod(OP_DURATION, 0x80, 2000)])),
            "M": talent(102, passive(1002,
                ("Quellfluss", "Wellspring", "Source vive", "Живой родник"),
                ("Deine Schadenszauber haben eine Chance von 10%, eine Quelle des Äthers anzuzapfen: Dein nächster Zauber innerhalb von 15 Sek. kostet kein Mana.",
                 "Your damaging spells have a 10% chance to tap a wellspring of aether: your next spell within 15 sec costs no mana.",
                 "Vos sorts de dégâts ont 10% de chances de puiser à une source d'éther : votre prochain sort dans les 15 s ne coûte pas de mana.",
                 "Ваши атакующие заклинания с вероятностью 10% открывают родник эфира: следующее заклинание в течение 15 сек. не расходует ману."),
                "T_Icon_Arcane_74.htex", SCHOOL_ARCANE, [proc(1003, T_CASTER)],
                procflags=DONE_MAGIC_NEG, procchance=10),
                extra=[Spell(1003, {
                    "attributes": BUFF_ATTRS, "spellSchool": SCHOOL_ARCANE, "positive": 1,
                    "icon": ICONS + "T_Icon_Arcane_74.htex", "duration": 15000,
                    "effects": [mod(OP_COST, 0x7FF, -100, pct=True)],
                    "procflags": DONE_MAGIC_NEG, "procchance": 100, "proccharges": 1,
                    **text_fields(
                        ("Quellfluss", "Wellspring", "Source vive", "Живой родник"),
                        ("", "", "", ""),
                        ("Dein nächster Zauber kostet kein Mana.", "Your next spell costs no mana.",
                         "Votre prochain sort ne coûte pas de mana.", "Следующее заклинание не расходует ману."))})]),
            "B2": ph(103, placeholder(1004,
                ("Schlummerwache", "Slumber Ward", "Garde du sommeil", "Страж сна"),
                ("Wenn dein Schlaf endet, wird das Ziel 4 Sek. lang um 50% verlangsamt.",
                 "When your Sleep breaks, the target is slowed by 50% for 4 sec.",
                 "Quand votre Sommeil se dissipe, la cible est ralentie de 50% pendant 4 s.",
                 "Когда ваш «Сон» прерывается, цель замедляется на 50% на 4 сек."),
                "T_Icon_Arcane_108.htex", SCHOOL_ARCANE)),
            "C1": ph(104, placeholder(1005,
                ("Ätherschild", "Aether Ward", "Garde éthérée", "Эфирный заслон"),
                ("Absorbiert Schaden auf Kosten deines Manas: Jeder Schadenspunkt verbraucht stattdessen 2 Mana.",
                 "Absorbs damage at the cost of your mana: each point of damage drains 2 mana instead.",
                 "Absorbe les dégâts au prix de votre mana : chaque point de dégâts consomme 2 points de mana à la place.",
                 "Поглощает урон за счёт маны: каждая единица урона вместо этого сжигает 2 ед. маны."),
                "T_Icon_Arcane_75.htex", SCHOOL_ARCANE)),
            "C2": ph(105, placeholder(1006,
                ("Sternenschritt", "Starstep", "Pas stellaire", "Звёздный шаг"),
                ("Teleportiert dich 15 Meter nach vorn und befreit dich von Bewegungsunfähigkeit und Betäubung.",
                 "Teleports you 15 yards forward and frees you from roots and stuns.",
                 "Vous téléporte 15 mètres en avant et vous libère des immobilisations et étourdissements.",
                 "Переносит вас на 15 м вперёд и освобождает от обездвиживания и оглушения."),
                "T_Icon_Arcane_106.htex", SCHOOL_ARCANE)),
            "CAP": talent(29, patch(198)),
        }),
        ("TALENT_PATH_FROST", ("Raureif", "Rime", "Givre", "Иней"), 0xFF66CCFF, {
            "A1": talent(106,
                passive(1010,
                    ("Splitternde Kälte", "Splintering Cold", "Froid éclatant", "Раскалывающий холод"),
                    ("Erhöht den kritischen Schadensbonus deiner Frostzauber um $s0%.",
                     "Increases the critical strike damage bonus of your Frost spells by $s0%.",
                     "Augmente de $s0% le bonus aux dégâts critiques de vos sorts de Givre.",
                     "Увеличивает бонус к критическому урону ваших заклинаний льда на $s0%."),
                    "T_Icon_Frost_57.htex", SCHOOL_FROST, [mod(OP_CRIT_DAMAGE, 0x401, 50, pct=True)],
                    rank=1, baseid=1010),
                passive(1011,
                    ("Splitternde Kälte", "Splintering Cold", "Froid éclatant", "Раскалывающий холод"),
                    ("Erhöht den kritischen Schadensbonus deiner Frostzauber um $s0%.",
                     "Increases the critical strike damage bonus of your Frost spells by $s0%.",
                     "Augmente de $s0% le bonus aux dégâts critiques de vos sorts de Givre.",
                     "Увеличивает бонус к критическому урону ваших заклинаний льда на $s0%."),
                    "T_Icon_Frost_57.htex", SCHOOL_FROST, [mod(OP_CRIT_DAMAGE, 0x401, 100, pct=True)],
                    rank=2, baseid=1010)),
            "A2": talent(6, patch(85,
                ("Eilender Raureif", "Quickening Rime", "Givre empressé", "Скорый иней"),
                ("Verringert die Zauberzeit deines Frostblitzes um 0,4 Sek.",
                 "Reduces the cast time of your Frostbolt by 0.4 sec.",
                 "Réduit le temps d'incantation de votre Éclair de givre de 0,4 s.",
                 "Сокращает время произнесения «Ледяной стрелы» на 0,4 сек."),
                [mod(OP_CAST, 0x2, -400)], icon="T_Icon_Frost_101.htex")),
            "A3": talent(21,
                patch(182, ("Raureifklinge", "Hoarfrost Edge", "Fil de givre", "Кромка изморози"),
                      ("Erhöht die kritische Trefferchance deiner Frostzauber um $s0%.",
                       "Increases the critical strike chance of your Frost spells by $s0%.",
                       "Augmente de $s0% les chances de coup critique de vos sorts de Givre.",
                       "Увеличивает шанс критического удара ваших заклинаний льда на $s0%."),
                      [mod(OP_CRIT, 0x401, 4)]),
                patch(183, ("Raureifklinge", "Hoarfrost Edge", "Fil de givre", "Кромка изморози"),
                      ("Erhöht die kritische Trefferchance deiner Frostzauber um $s0%.",
                       "Increases the critical strike chance of your Frost spells by $s0%.",
                       "Augmente de $s0% les chances de coup critique de vos sorts de Givre.",
                       "Увеличивает шанс критического удара ваших заклинаний льда на $s0%."),
                      [mod(OP_CRIT, 0x401, 8)])),
            "B1": talent(107, passive(1012,
                ("Gletschergriff", "Glacial Grip", "Étreinte glaciaire", "Ледниковая хватка"),
                ("Dein Frostblitz hat eine Chance von 15%, das Ziel 3 Sek. lang festzufrieren.",
                 "Your Frostbolt has a 15% chance to freeze the target in place for 3 sec.",
                 "Votre Éclair de givre a 15% de chances de geler la cible sur place pendant 3 s.",
                 "Ваша «Ледяная стрела» с вероятностью 15% сковывает цель на 3 сек."),
                "T_Icon_Frost_49.htex", SCHOOL_FROST, [proc(1013)],
                procflags=DONE_MAGIC_NEG, procchance=15, procfamily=0x2),
                extra=[Spell(1013, {
                    "attributes": DEBUFF_ATTRS, "spellSchool": SCHOOL_FROST, "duration": 3000,
                    "mechanic": MECHANIC_ROOT, "icon": ICONS + "T_Icon_Frost_49.htex",
                    "visualization_id": 47, "effects": [aura(MOD_ROOT, target=T_ENEMY)],
                    **text_fields(("Gletschergriff", "Glacial Grip", "Étreinte glaciaire", "Ледниковая хватка"),
                                  ("", "", "", ""),
                                  ("Festgefroren.", "Frozen in place.", "Gelé sur place.", "Скован льдом."))})]),
            "M": talent(22, patch(185,
                ("Tiefer Winter", "Deep Winter", "Hiver profond", "Глубокая зима"),
                ("Erhöht die Dauer deiner Frosteffekte um 30%.",
                 "Increases the duration of your Frost effects by 30%.",
                 "Augmente de 30% la durée de vos effets de Givre.",
                 "Увеличивает длительность ваших эффектов льда на 30%."),
                [mod(OP_DURATION, 0x7, 30, pct=True)], icon="T_Icon_Frost_41.htex")),
            "B2": talent(108, passive(1014,
                ("Frostspröde", "Frostbrittle", "Fragilité du givre", "Ледяная хрупкость"),
                ("Von deiner Frostnova eingefrorene Gegner werden 5 Sek. lang mit 50% höherer Chance kritisch getroffen.",
                 "Enemies frozen by your Frost Nova are 50% more likely to be critically hit for 5 sec.",
                 "Les ennemis gelés par votre Nova de givre ont 50% de chances supplémentaires de subir un coup critique pendant 5 s.",
                 "Враги, скованные вашей «Ледяной новой», в течение 5 сек. получают критические удары с вероятностью на 50% выше."),
                "T_Icon_Frost_63.htex", SCHOOL_FROST, [proc(1015)],
                procflags=DONE_MAGIC_NEG, procchance=100, procfamily=0x4),
                extra=[Spell(1015, {
                    "attributes": DEBUFF_ATTRS, "spellSchool": SCHOOL_FROST, "duration": 5000,
                    "icon": ICONS + "T_Icon_Frost_63.htex",
                    "effects": [aura(MOD_CRIT_TAKEN, 50, target=T_ENEMY)],
                    **text_fields(("Spröde", "Brittle", "Fragilisé", "Хрупкость"),
                                  ("", "", "", ""),
                                  ("Chance, kritisch getroffen zu werden, um $s0% erhöht.",
                                   "Chance to be critically hit increased by $s0%.",
                                   "Chances de subir un coup critique augmentées de $s0%.",
                                   "Вероятность получить критический удар повышена на $s0%."))})]),
            "C1": ph(109, placeholder(1016,
                ("Gletschermantel", "Glacier Mantle", "Manteau de glacier", "Ледниковая мантия"),
                ("Hüllt dich in einen Eisschild, der 60 Sek. lang Schaden absorbiert.",
                 "Encases you in a shield of ice that absorbs damage for 60 sec.",
                 "Vous enveloppe d'un bouclier de glace qui absorbe les dégâts pendant 60 s.",
                 "Окружает вас ледяным щитом, поглощающим урон в течение 60 сек."),
                "T_Icon_Frost_82.htex", SCHOOL_FROST)),
            "C2": ph(110, placeholder(1017,
                ("Ruf des Winters", "Winter's Recall", "Rappel de l'hiver", "Зов зимы"),
                ("Beendet sofort die Abklingzeit all deiner Frostzauber.",
                 "Finishes the cooldown of all your Frost spells.",
                 "Termine le temps de recharge de tous vos sorts de Givre.",
                 "Мгновенно завершает восстановление всех ваших заклинаний льда."),
                "T_Icon_Frost_69.htex", SCHOOL_FROST)),
            "CAP": talent(23, patch(187)),
        }),
        ("TALENT_PATH_FIRE", ("Glut", "Cinder", "Braise", "Угли"), 0xFFFF8A3D, {
            "A1": talent(24,
                patch(188, ("Brennende Seele", "Burning Soul", "Âme ardente", "Пылающая душа"),
                      ("Erhöht den Schaden deiner Feuerzauber um $s0%.",
                       "Increases the damage of your Fire spells by $s0%.",
                       "Augmente de $s0% les dégâts de vos sorts de Feu.",
                       "Увеличивает урон ваших заклинаний огня на $s0%."),
                      [mod(OP_DAMAGE, 0x38, 5, pct=True)], icon="T_Icon_Fire_100.htex"),
                patch(189, ("Brennende Seele", "Burning Soul", "Âme ardente", "Пылающая душа"),
                      ("Erhöht den Schaden deiner Feuerzauber um $s0%.",
                       "Increases the damage of your Fire spells by $s0%.",
                       "Augmente de $s0% les dégâts de vos sorts de Feu.",
                       "Увеличивает урон ваших заклинаний огня на $s0%."),
                      [mod(OP_DAMAGE, 0x38, 10, pct=True)], icon="T_Icon_Fire_100.htex")),
            "A2": talent(111, patch(4)),
            "A3": talent(25, patch(191,
                ("Stichflamme", "Flash Fire", "Embrasement éclair", "Вспышка пламени"),
                ("Verringert die Abklingzeit deiner Feuerexplosion um 2 Sek.",
                 "Reduces the cooldown of your Fire Blast by 2 sec.",
                 "Réduit de 2 s le temps de recharge de votre Trait de feu.",
                 "Сокращает время восстановления «Огненного взрыва» на 2 сек."),
                [mod(OP_COOLDOWN, 0x10, -2000)])),
            "B1": talent(112, passive(1020,
                ("Entfachte Wut", "Kindled Fury", "Fureur attisée", "Разожжённая ярость"),
                ("Erhöht die kritische Trefferchance deiner Feuerzauber um $s0%.",
                 "Increases the critical strike chance of your Fire spells by $s0%.",
                 "Augmente de $s0% les chances de coup critique de vos sorts de Feu.",
                 "Увеличивает шанс критического удара ваших заклинаний огня на $s0%."),
                "T_Icon_Fire_140.htex", SCHOOL_FIRE, [mod(OP_CRIT, 0x38, 8)])),
            "M": talent(113, passive(1021,
                ("Schwelende Wunden", "Smoldering Wounds", "Plaies fumantes", "Тлеющие раны"),
                ("Kritische Treffer deiner Feuerzauber setzen das Ziel in Brand und verursachen 4 Sek. lang zusätzlichen Feuerschaden.",
                 "Critical strikes of your Fire spells set the target ablaze, dealing additional fire damage over 4 sec.",
                 "Les coups critiques de vos sorts de Feu embrasent la cible, infligeant des dégâts de Feu supplémentaires en 4 s.",
                 "Критические удары ваших заклинаний огня поджигают цель, нанося дополнительный урон от огня в течение 4 сек."),
                "T_Icon_Fire_18.htex", SCHOOL_FIRE, [proc(1022)],
                procflags=DONE_MAGIC_NEG, procchance=100, procfamily=0x38, procexflags=EX_CRIT),
                extra=[Spell(1022, {
                    "attributes": DEBUFF_ATTRS, "spellSchool": SCHOOL_FIRE, "duration": 4000,
                    "familyflags": str(0x800), "icon": ICONS + "T_Icon_Fire_18.htex",
                    "maxlevel": 10, "baselevel": 1, "spelllevel": 1,
                    "effects": [{**aura(PERIODIC_DAMAGE, 4, target=T_ENEMY), "pointsperlevel": 0.8,
                                 "amplitude": 1000}],
                    **text_fields(("Schwelende Wunden", "Smoldering Wounds", "Plaies fumantes", "Тлеющие раны"),
                                  ("", "", "", ""),
                                  ("Erleidet $s0 Feuerschaden alle $I0. Hält $D an.",
                                   "Suffers $s0 fire damage every $I0. Lasts $D.",
                                   "Subit $s0 points de dégâts de Feu toutes les $I0. Dure $D.",
                                   "Получает $s0 ед. урона от огня каждые $I0. Длится $D."))})]),
            "B2": talent(114, passive(1023,
                ("Glut schüren", "Stoke the Flames", "Attiser les flammes", "Раздуть пламя"),
                ("Deine Feuerexplosion heizt dich auf: Dein Feuerhagel verursacht 8 Sek. lang 40% mehr Schaden.",
                 "Your Fire Blast heats you up: your Fire Barrage deals 40% more damage for 8 sec.",
                 "Votre Trait de feu vous réchauffe : votre Barrage de feu inflige 40% de dégâts supplémentaires pendant 8 s.",
                 "Ваш «Огненный взрыв» разогревает вас: «Огненный шквал» в течение 8 сек. наносит на 40% больше урона."),
                "T_Icon_Fire_83.htex", SCHOOL_FIRE, [proc(1024, T_CASTER)],
                procflags=DONE_MAGIC_NEG, procchance=100, procfamily=0x10),
                extra=[Spell(1024, {
                    "attributes": BUFF_ATTRS, "spellSchool": SCHOOL_FIRE, "positive": 1,
                    "duration": 8000, "icon": ICONS + "T_Icon_Fire_83.htex",
                    "effects": [mod(OP_DAMAGE, 0x20, 40, pct=True)],
                    **text_fields(("Glut schüren", "Stoke the Flames", "Attiser les flammes", "Раздуть пламя"),
                                  ("", "", "", ""),
                                  ("Feuerhagel verursacht $s0% mehr Schaden.",
                                   "Fire Barrage deals $s0% more damage.",
                                   "Barrage de feu inflige $s0% de dégâts supplémentaires.",
                                   "«Огненный шквал» наносит на $s0% больше урона."))})]),
            "C1": ph(115, placeholder(1025,
                ("Herz des Lauffeuers", "Wildfire Heart", "Cœur de feu sauvage", "Сердце пожара"),
                ("Deine nächsten 3 Feuerzauber innerhalb von 10 Sek. treffen garantiert kritisch.",
                 "Your next 3 Fire spells within 10 sec are guaranteed critical strikes.",
                 "Vos 3 prochains sorts de Feu dans les 10 s sont des coups critiques assurés.",
                 "Ваши следующие 3 заклинания огня в течение 10 сек. гарантированно наносят критический урон."),
                "T_Icon_Fire_108.htex", SCHOOL_FIRE)),
            "C2": ph(116, placeholder(1026,
                ("Essenatem", "Furnace Breath", "Souffle de fournaise", "Дыхание горна"),
                ("Speit einen Feuerkegel, der Gegnern vor dir Schaden zufügt und sie 3 Sek. lang desorientiert.",
                 "Breathes a cone of fire that damages enemies in front of you and disorients them for 3 sec.",
                 "Crache un cône de feu qui blesse les ennemis devant vous et les désoriente pendant 3 s.",
                 "Выдыхает конус пламени, нанося урон врагам перед вами и дезориентируя их на 3 сек."),
                "T_Icon_Fire_105.htex", SCHOOL_FIRE)),
            "CAP": talent(26, patch(150)),
        }),
    ],
}

WARRIOR = {
    "class_id": 1, "tab_id": 4, "classmask": 1, "icon": "S_Class_Warrior.htex",
    "paths": [
        ("TALENT_PATH_ARMS", ("Klinge", "Blade", "Lame", "Клинок"), 0xFFE65A45, {
            "A1": talent(9,
                patch(94, ("Gezackte Klinge", "Jagged Edge", "Lame dentelée", "Зазубренный клинок"),
                      ("Erhöht den Schaden deines Verwundens um $s0%.", "Increases the damage of your Rend by $s0%.",
                       "Augmente de $s0% les dégâts de votre Pourfendre.", "Увеличивает урон «Кровопускания» на $s0%."),
                      [mod(OP_DAMAGE, 0x2, 20, pct=True)]),
                patch(95, ("Gezackte Klinge", "Jagged Edge", "Lame dentelée", "Зазубренный клинок"),
                      ("Erhöht den Schaden deines Verwundens um $s0%.", "Increases the damage of your Rend by $s0%.",
                       "Augmente de $s0% les dégâts de votre Pourfendre.", "Увеличивает урон «Кровопускания» на $s0%."),
                      [mod(OP_DAMAGE, 0x2, 40, pct=True)])),
            "A2": talent(120, passive(1100,
                ("Offene Adern", "Open Veins", "Veines ouvertes", "Вскрытые вены"),
                ("Kritische Treffer deiner Waffentechniken lassen das Ziel 6 Sek. lang bluten.",
                 "Critical strikes of your weapon techniques make the target bleed for 6 sec.",
                 "Les coups critiques de vos techniques d'arme font saigner la cible pendant 6 s.",
                 "Критические удары ваших приёмов вызывают у цели кровотечение на 6 сек."),
                "T_Icon_BloodCombat_106.htex", SCHOOL_PHYSICAL, [proc(1101)],
                procflags=DONE_MELEE_SPELL, procchance=100, procexflags=EX_CRIT),
                extra=[Spell(1101, {
                    "attributes": DEBUFF_ATTRS, "spellSchool": SCHOOL_PHYSICAL, "duration": 6000,
                    "icon": ICONS + "T_Icon_BloodCombat_106.htex", "visualization_id": 6,
                    "maxlevel": 10, "baselevel": 1, "spelllevel": 1,
                    "effects": [{**aura(PERIODIC_DAMAGE, 3, target=T_ENEMY), "pointsperlevel": 0.6,
                                 "amplitude": 2000}],
                    **text_fields(("Offene Ader", "Open Vein", "Veine ouverte", "Вскрытая вена"),
                                  ("", "", "", ""),
                                  ("Erleidet $s0 Schaden alle $I0.", "Suffers $s0 damage every $I0.",
                                   "Subit $s0 points de dégâts toutes les $I0.", "Получает $s0 ед. урона каждые $I0."))})]),
            "A3": talent(8, patch(93,
                ("Sparsame Bewegung", "Economy of Motion", "Économie de mouvement", "Экономия движений"),
                ("Verringert die Wutkosten deines Schlags um 5.", "Reduces the Rage cost of your Strike by 5.",
                 "Réduit de 5 le coût en rage de votre Frappe.", "Снижает затраты ярости на «Удар» на 5."),
                [mod(OP_COST, 0x1, -5)], icon="T_Icon_BloodCombat_32.htex")),
            "B1": talent(121, passive(1102,
                ("Anhaltende Wunden", "Lingering Wounds", "Plaies persistantes", "Незаживающие раны"),
                ("Dein Verwunden hält 6 Sek. länger an.", "Your Rend lasts 6 sec longer.",
                 "Votre Pourfendre dure 6 s de plus.", "«Кровопускание» длится на 6 сек. дольше."),
                "T_Icon_BloodCombat_87.htex", SCHOOL_PHYSICAL, [mod(OP_DURATION, 0x2, 6000)])),
            "M": talent(122, passive(1103,
                ("Henker", "Headsman", "Bourreau", "Палач"),
                ("Verringert die Abklingzeit von Hinrichten um 5 Sek. und erhöht seine kritische Trefferchance um 20%.",
                 "Reduces the cooldown of Execute by 5 sec and increases its critical strike chance by 20%.",
                 "Réduit de 5 s le temps de recharge d'Exécution et augmente ses chances de coup critique de 20%.",
                 "Сокращает время восстановления «Казни» на 5 сек. и повышает её шанс критического удара на 20%."),
                "T_Icon_BloodCombat_71.htex", SCHOOL_PHYSICAL,
                [mod(OP_COOLDOWN, 0x20, -5000), mod(OP_CRIT, 0x20, 20)])),
            "B2": talent(33,
                patch(206, ("Geschliffener Stahl", "Honed Steel", "Acier affûté", "Отточенная сталь"),
                      ("Erhöht den Schaden deiner Waffentechniken um $s0%.", "Increases the damage of your weapon techniques by $s0%.",
                       "Augmente de $s0% les dégâts de vos techniques d'arme.", "Увеличивает урон ваших приёмов на $s0%."),
                      [mod(OP_DAMAGE, 0x1061, 5, pct=True)]),
                patch(207, ("Geschliffener Stahl", "Honed Steel", "Acier affûté", "Отточенная сталь"),
                      ("Erhöht den Schaden deiner Waffentechniken um $s0%.", "Increases the damage of your weapon techniques by $s0%.",
                       "Augmente de $s0% les dégâts de vos techniques d'arme.", "Увеличивает урон ваших приёмов на $s0%."),
                      [mod(OP_DAMAGE, 0x1061, 10, pct=True)])),
            "C1": ph(123, placeholder(1104,
                ("Stahlzyklon", "Steel Cyclone", "Cyclone d'acier", "Стальной циклон"),
                ("Du wirbelst 6 Sek. lang als Sturm aus Stahl umher und triffst alle Gegner in der Nähe. Du kannst nicht aufgehalten werden.",
                 "Become a whirlwind of steel for 6 sec, striking all nearby enemies. You can not be stopped.",
                 "Devenez un tourbillon d'acier pendant 6 s et frappez tous les ennemis proches. Rien ne peut vous arrêter.",
                 "Вы превращаетесь в стальной вихрь на 6 сек., поражая всех врагов рядом. Вас невозможно остановить."),
                "T_Icon_BloodCombat_46.htex", SCHOOL_PHYSICAL)),
            "C2": ph(124, placeholder(1105,
                ("Lücke bestrafen", "Punish the Opening", "Châtier l'ouverture", "Кара за брешь"),
                ("Nachdem dein Ziel ausgewichen ist, schlägst du sofort mit schwerem Schaden zu. Kann nicht ausgewichen, geblockt oder pariert werden.",
                 "After your target dodges, strike instantly for heavy damage. Can not be dodged, blocked or parried.",
                 "Après une esquive de votre cible, frappez instantanément pour de lourds dégâts. Ne peut être esquivé, bloqué ou paré.",
                 "После уклонения цели мгновенно наносит сильный удар, от которого нельзя уклониться, который нельзя блокировать или парировать."),
                "T_Icon_BloodCombat_47.htex", SCHOOL_PHYSICAL)),
            "CAP": talent(34, patch(209)),
        }),
        ("TALENT_PATH_PROTECTION", ("Bollwerk", "Bulwark", "Rempart", "Оплот"), 0xFF78AEE0, {
            "A1": talent(15,
                patch(139, ("Eisenhaut", "Ironhide", "Peau de fer", "Железная шкура"),
                      ("Erhöht deine Rüstung um $s0%.", "Increases your armor by $s0%.",
                       "Augmente votre armure de $s0%.", "Увеличивает вашу броню на $s0%."),
                      [aura(MOD_RESIST_PCT, 5)]),
                patch(140, ("Eisenhaut", "Ironhide", "Peau de fer", "Железная шкура"),
                      ("Erhöht deine Rüstung um $s0%.", "Increases your armor by $s0%.",
                       "Augmente votre armure de $s0%.", "Увеличивает вашу броню на $s0%."),
                      [aura(MOD_RESIST_PCT, 10)])),
            "A2": talent(30, patch(202,
                ("Unbeugsamer Blick", "Unyielding Glare", "Regard inflexible", "Непреклонный взгляд"),
                ("Erhöht die von deinen Kriegerfähigkeiten erzeugte Bedrohung um 30%.",
                 "Increases the threat generated by your warrior abilities by 30%.",
                 "Augmente de 30% la menace générée par vos techniques de guerrier.",
                 "Увеличивает угрозу от ваших способностей воина на 30%."))),
            "A3": talent(31, patch(204)),
            "B1": talent(35, patch(211, icon="T_Icon_Gold_38.htex")),
            "M": talent(125, passive(1110,
                ("Die Klinge lesen", "Read the Blade", "Lire la lame", "Читать клинок"),
                ("Erhöht deine Chance, Nahkampfangriffen auszuweichen, um $s0%.",
                 "Increases your chance to dodge melee attacks by $s0%.",
                 "Augmente de $s0% vos chances d'esquiver les attaques en mêlée.",
                 "Повышает вероятность уклониться от атак ближнего боя на $s0%."),
                "T_Icon_Gold_55.htex", SCHOOL_PHYSICAL, [aura(MOD_DODGE, 4)])),
            "B2": talent(126, Spell(1111, {
                "attributes": SELF_ACTIVE_ATTRS, "spellSchool": SCHOOL_PHYSICAL, "positive": 1,
                "icon": ICONS + "T_Icon_Gold_84.htex", "duration": 10000, "cooldown": "180000",
                "powertype": POWER_RAGE, "cost": 0, "visualization_id": 36,
                "effects": [aura(DMG_TAKEN_PCT, -50)],
                **text_fields(("Die Linie halten", "Hold the Line", "Tenir la ligne", "Держать строй"),
                              ("Verringert $D lang den erlittenen Schaden um 50%.",
                               "Reduces all damage taken by 50% for $D.",
                               "Réduit tous les dégâts subis de 50% pendant $D.",
                               "Снижает весь получаемый урон на 50% на $D."),
                              ("Erlittener Schaden um 50% verringert.", "Damage taken reduced by 50%.",
                               "Dégâts subis réduits de 50%.", "Получаемый урон снижен на 50%."))})),
            "C1": ph(127, placeholder(1112,
                ("Eiserne Antwort", "Iron Answer", "Réponse de fer", "Железный ответ"),
                ("Nachdem du ausgewichen bist oder geblockt hast, schlägst du mit Waffenschaden zurück und erzeugst hohe Bedrohung.",
                 "After you dodge or block, counterattack for weapon damage and generate high threat.",
                 "Après une esquive ou un blocage, contre-attaquez avec votre arme et générez une forte menace.",
                 "После уклонения или блока вы контратакуете оружием и создаёте высокую угрозу."),
                "T_Icon_BloodCombat_91.htex", SCHOOL_PHYSICAL)),
            "C2": ph(128, placeholder(1113,
                ("Spiegelschild", "Mirrored Shield", "Bouclier miroir", "Зеркальный щит"),
                ("Hebt deinen Schild und wirft den nächsten feindlichen Zauber auf seinen Wirker zurück.",
                 "Raise your shield to reflect the next hostile spell back at its caster.",
                 "Levez votre bouclier pour renvoyer le prochain sort hostile à son lanceur.",
                 "Поднимает щит, отражая следующее враждебное заклинание обратно в заклинателя."),
                "T_Icon_Energy_54.htex", SCHOOL_PHYSICAL)),
            "CAP": talent(32, patch(205)),
        }),
        ("TALENT_PATH_BATTLE", ("Kriegsschrei", "Warcry", "Cri de guerre", "Боевой клич"), 0xFFF0B44C, {
            "A1": talent(36, patch(213, ("Donnernder Ansturm", "Thundering Charge", "Charge tonitruante", "Громовой натиск"))),
            "A2": talent(37, patch(215, icon="T_Icon_Gold_83.htex")),
            "A3": talent(129, passive(1120,
                ("Kochendes Blut", "Boiling Blood", "Sang bouillonnant", "Кипящая кровь"),
                ("Verringert die Abklingzeit von Blutrausch um 20 Sek.",
                 "Reduces the cooldown of Bloodrush by 20 sec.",
                 "Réduit de 20 s le temps de recharge de Ruée sanglante.",
                 "Сокращает время восстановления «Прилива крови» на 20 сек."),
                "T_Icon_BloodCombat_75.htex", SCHOOL_PHYSICAL, [mod(OP_COOLDOWN, 0x8, -20000)])),
            "B1": ph(130, placeholder(1121,
                ("Unaufhaltsam", "Unstoppable", "Inarrêtable", "Неудержимый"),
                ("Sturmangriff kann im Kampf eingesetzt werden und seine Abklingzeit sinkt um 5 Sek.",
                 "Charge can be used in combat and its cooldown is reduced by 5 sec.",
                 "Charge peut être utilisée en combat et son temps de recharge est réduit de 5 s.",
                 "«Рывок» можно применять в бою, а его восстановление сокращается на 5 сек."),
                "T_Icon_Gold_04.htex", SCHOOL_PHYSICAL)),
            "M": talent(131, passive(1122,
                ("Stimme des Heerzugs", "Voice of the Warhost", "Voix de l'ost", "Голос воинства"),
                ("Erhöht die Dauer von Schlachtruf und Demoralisierendem Ruf um 50%.",
                 "Increases the duration of Battlecry and Demoralizing Shout by 50%.",
                 "Augmente de 50% la durée de Cri de guerre et de Cri démoralisant.",
                 "Увеличивает длительность «Боевого клича» и «Деморализующего крика» на 50%."),
                "T_Icon_BloodCombat_17.htex", SCHOOL_PHYSICAL, [mod(OP_DURATION, 0xA00, 50, pct=True)])),
            "B2": talent(132, passive(1123,
                ("Kampftrance", "Battle Trance", "Transe guerrière", "Боевой транс"),
                ("Nachdem du einen Gegner getötet hast, verursachst du 12 Sek. lang 10% mehr Schaden.",
                 "After killing an enemy, you deal 10% more damage for 12 sec.",
                 "Après avoir tué un ennemi, vous infligez 10% de dégâts supplémentaires pendant 12 s.",
                 "После убийства врага вы наносите на 10% больше урона в течение 12 сек."),
                "T_Icon_BloodCombat_94.htex", SCHOOL_PHYSICAL, [proc(1124, T_CASTER)],
                procflags=KILL, procchance=100),
                extra=[Spell(1124, {
                    "attributes": BUFF_ATTRS, "spellSchool": SCHOOL_PHYSICAL, "positive": 1,
                    "duration": 12000, "icon": ICONS + "T_Icon_BloodCombat_94.htex",
                    "visualization_id": 12, "effects": [aura(DMG_DONE_PCT, 10)],
                    **text_fields(("Kampftrance", "Battle Trance", "Transe guerrière", "Боевой транс"), ("", "", "", ""),
                                  ("Verursachter Schaden um $s0% erhöht.", "Damage dealt increased by $s0%.",
                                   "Dégâts infligés augmentés de $s0%.", "Наносимый урон увеличен на $s0%."))})]),
            "C1": ph(133, placeholder(1125,
                ("Schnitterschwung", "Reaping Sweep", "Fauche", "Жатва"),
                ("Trifft alle Gegner im Umkreis von 8 Metern mit Waffenschaden.",
                 "Strikes all enemies within 8 yards for weapon damage.",
                 "Frappe tous les ennemis à moins de 8 mètres avec votre arme.",
                 "Наносит урон оружием всем врагам в радиусе 8 м."),
                "T_Icon_Gold_101.htex", SCHOOL_PHYSICAL)),
            "C2": ph(134, placeholder(1126,
                ("Verletzter Stolz", "Wounded Pride", "Fierté blessée", "Уязвлённая гордость"),
                ("Ein kritischer Treffer gegen dich versetzt dich in Rage: 8 Sek. lang 15% mehr verursachter Schaden.",
                 "Taking a critical hit sends you into a rage, increasing damage dealt by 15% for 8 sec.",
                 "Subir un coup critique vous enrage et augmente vos dégâts de 15% pendant 8 s.",
                 "Получив критический удар, вы впадаете в ярость: наносимый урон повышается на 15% на 8 сек."),
                "T_Icon_BloodCombat_19.htex", SCHOOL_PHYSICAL)),
            "CAP": talent(135, Spell(1127, {
                "attributes": SELF_ACTIVE_ATTRS, "spellSchool": SCHOOL_PHYSICAL, "positive": 1,
                "icon": ICONS + "T_Icon_BloodCombat_84.htex", "duration": 30000, "cooldown": "120000",
                "powertype": POWER_RAGE, "cost": 0, "visualization_id": 31,
                "effects": [aura(DMG_DONE_PCT, 20), aura(DMG_TAKEN_PCT, 10)],
                **text_fields(("Schwur des Berserkers", "Berserker's Oath", "Serment du berserker", "Клятва берсерка"),
                              ("Du verfällst $D lang in Raserei: Du verursachst $s0% mehr Schaden, erleidest aber auch 10% mehr Schaden.",
                               "You fall into a frenzy for $D: you deal $s0% more damage, but also take 10% more damage.",
                               "Vous entrez en frénésie pendant $D : vous infligez $s0% de dégâts en plus, mais en subissez aussi 10% de plus.",
                               "Вы впадаете в неистовство на $D: наносите на $s0% больше урона, но и получаете на 10% больше."),
                              ("Verursachter Schaden um $s0% erhöht, erlittener Schaden um 10% erhöht.",
                               "Damage dealt increased by $s0%, damage taken increased by 10%.",
                               "Dégâts infligés augmentés de $s0%, dégâts subis augmentés de 10%.",
                               "Наносимый урон увеличен на $s0%, получаемый — на 10%."))})),
        }),
    ],
}

CLERIC = {
    "class_id": 2, "tab_id": 5, "classmask": 2, "icon": "S_Class_Cleric.htex",
    "paths": [
        ("TALENT_PATH_HOLY", ("Gnade", "Grace", "Grâce", "Благодать"), 0xFFFFD66E, {
            "A1": talent(38, patch(220)),
            "A2": talent(20,
                patch(176, ("Gesegnete Erneuerung", "Blessed Renewal", "Renouveau béni", "Благословенное обновление"),
                      ("Erhöht die Heilung deines Erneuernden Lichts um $s0%.",
                       "Increases the healing of your Renewing Light by $s0%.",
                       "Augmente de $s0% les soins de votre Lumière rénovatrice.",
                       "Увеличивает исцеление «Обновляющего света» на $s0%."),
                      [mod(OP_PERIODIC, 0x8, 15, pct=True)]),
                patch(177, ("Gesegnete Erneuerung", "Blessed Renewal", "Renouveau béni", "Благословенное обновление"),
                      ("Erhöht die Heilung deines Erneuernden Lichts um $s0%.",
                       "Increases the healing of your Renewing Light by $s0%.",
                       "Augmente de $s0% les soins de votre Lumière rénovatrice.",
                       "Увеличивает исцеление «Обновляющего света» на $s0%."),
                      [mod(OP_PERIODIC, 0x8, 30, pct=True)])),
            "A3": talent(42, patch(228, icon="T_Icon_Gold_46.htex")),
            "B1": talent(11, patch(104)),
            "M": talent(12,
                patch(105, ("Himmlisches Zielen", "Celestial Aim", "Visée céleste", "Небесная точность"),
                      ("Erhöht die kritische Trefferchance von Heilendem Licht, Göttlicher Strafe und Heiligem Feuer um $s0%.",
                       "Increases the critical strike chance of Healing Light, Smite and Holy Fire by $s0%.",
                       "Augmente de $s0% les chances de coup critique de Lumière guérisseuse, Châtiment et Flammes sacrées.",
                       "Повышает шанс критического эффекта «Исцеляющего света», «Кары» и «Священного огня» на $s0%."),
                      [mod(OP_CRIT, 0x5, 4)], icon="T_Icon_Gold_37.htex"),
                patch(106, ("Himmlisches Zielen", "Celestial Aim", "Visée céleste", "Небесная точность"),
                      ("Erhöht die kritische Trefferchance von Heilendem Licht, Göttlicher Strafe und Heiligem Feuer um $s0%.",
                       "Increases the critical strike chance of Healing Light, Smite and Holy Fire by $s0%.",
                       "Augmente de $s0% les chances de coup critique de Lumière guérisseuse, Châtiment et Flammes sacrées.",
                       "Повышает шанс критического эффекта «Исцеляющего света», «Кары» и «Священного огня» на $s0%."),
                      [mod(OP_CRIT, 0x5, 8)], icon="T_Icon_Gold_37.htex")),
            "B2": ph(140, placeholder(1200,
                ("Nachglühendes Licht", "Lingering Light", "Lumière persistante", "Неугасающий свет"),
                ("Bei deinem Tod wirst du 15 Sek. lang zu einem Geist des Lichts und kannst weiter heilen.",
                 "Upon death, become a spirit of light for 15 sec and keep healing your allies.",
                 "À votre mort, devenez un esprit de lumière pendant 15 s et continuez à soigner vos alliés.",
                 "После смерти вы на 15 сек. становитесь духом света и продолжаете исцелять союзников."),
                "T_Icon_Gold_02.htex", SCHOOL_HOLY)),
            "C1": ph(141, placeholder(1201,
                ("Schwingen des Hüters", "Wings of the Warden", "Ailes du gardien", "Крылья хранителя"),
                ("Ruft einen Schutzgeist, der einen Verbündeten 10 Sek. lang schützt: 40% mehr erhaltene Heilung und ein verhinderter tödlicher Schlag.",
                 "Calls a guardian spirit to protect an ally for 10 sec, increasing healing received by 40% and preventing one killing blow.",
                 "Invoque un esprit gardien qui protège un allié pendant 10 s : soins reçus augmentés de 40% et un coup fatal évité.",
                 "Призывает дух-хранитель, оберегающий союзника 10 сек.: получаемое исцеление +40%, один смертельный удар предотвращается."),
                "T_Icon_Gold_39.htex", SCHOOL_HOLY)),
            "C2": ph(142, placeholder(1202,
                ("Ring der Heilung", "Ring of Mending", "Anneau de guérison", "Кольцо исцеления"),
                ("Heilt bis zu 5 Gruppenmitglieder im Umkreis von 15 Metern um das Ziel.",
                 "Heals up to 5 party members within 15 yards of the target.",
                 "Soigne jusqu'à 5 membres du groupe à moins de 15 mètres de la cible.",
                 "Исцеляет до 5 участников группы в радиусе 15 м от цели."),
                "T_Icon_Gold_72.htex", SCHOOL_HOLY)),
            "CAP": talent(143, Spell(1203, {
                "attributes": HEAL_ACTIVE_ATTRS, "spellSchool": SCHOOL_HOLY, "positive": 1,
                "icon": ICONS + "T_Icon_Gold_88.htex", "casttime": 2500, "cost": 80,
                "cooldown": "1500", "visualization_id": 70, "rangetype": 5,
                "maxlevel": 10, "baselevel": 1, "spelllevel": 1,
                "effects": [{"type": HEAL, "basepoints": 30, "diesides": 8, "pointsperlevel": 1.5,
                             "targeta": T_CASTER_AREA_PARTY, "radius": 30.0, "powerbonusfactor": 0.3}],
                **text_fields(("Hymne der Morgenröte", "Hymn of Dawn", "Hymne de l'aube", "Гимн рассвета"),
                              ("Heilt dich und deine Gruppenmitglieder im Umkreis von 30 Metern um $s0.",
                               "Heals you and your party members within 30 yards for $s0.",
                               "Rend $s0 points de vie à vous et aux membres de votre groupe à moins de 30 mètres.",
                               "Восполняет $s0 ед. здоровья вам и участникам группы в радиусе 30 м."))})),
        }),
        ("TALENT_PATH_FAITH", ("Ägide", "Aegis", "Égide", "Эгида"), 0xFF9AD8FF, {
            "A1": talent(10, patch(98, ("Überfließende Vitalität", "Abundant Vitality", "Vitalité débordante", "Избыточная жизненная сила"))),
            "A2": talent(19, patch(175)),
            "A3": talent(39, patch(222)),
            "B1": talent(40, patch(224, icon="T_Icon_Gold_81.htex")),
            "M": talent(144, passive(1210,
                ("Standhafter Glaube", "Steadfast Faith", "Foi inébranlable", "Непоколебимая вера"),
                ("Verringert jeglichen erlittenen Schaden um 6%.", "Reduces all damage taken by 6%.",
                 "Réduit tous les dégâts subis de 6%.", "Снижает весь получаемый урон на 6%."),
                "T_Icon_Gold_05.htex", SCHOOL_HOLY, [aura(DMG_TAKEN_PCT, -6)])),
            "B2": ph(145, placeholder(1211,
                ("Widerhallende Auren", "Resounding Auras", "Auras retentissantes", "Звучные ауры"),
                ("Verdoppelt 8 Sek. lang die Wirkung deiner Auren.", "Doubles the strength of your auras for 8 sec.",
                 "Double la puissance de vos auras pendant 8 s.", "Удваивает силу ваших аур на 8 сек."),
                "T_Icon_Gold_71.htex", SCHOOL_HOLY)),
            "C1": ph(146, placeholder(1212,
                ("Geist entfachen", "Kindle the Spirit", "Raviver l'esprit", "Разжечь дух"),
                ("Erfüllt einen Verbündeten mit Macht und erhöht sein Tempo 15 Sek. lang um 20%.",
                 "Infuses an ally with power, increasing their haste by 20% for 15 sec.",
                 "Insuffle de la puissance à un allié et augmente sa hâte de 20% pendant 15 s.",
                 "Наполняет союзника силой, повышая его скорость на 20% на 15 сек."),
                "T_Icon_Gold_83.htex", SCHOOL_HOLY)),
            "C2": ph(147, placeholder(1213,
                ("Schützende Hände", "Sheltering Hands", "Mains protectrices", "Оберегающие руки"),
                ("Verringert den Schaden, den ein Verbündeter erleidet, 8 Sek. lang um 40%.",
                 "Reduces the damage an ally takes by 40% for 8 sec.",
                 "Réduit de 40% les dégâts subis par un allié pendant 8 s.",
                 "Снижает получаемый союзником урон на 40% на 8 сек."),
                "T_Icon_Gold_96.htex", SCHOOL_HOLY)),
            "CAP": talent(148, Spell(1214, {
                "attributes": SELF_ACTIVE_ATTRS, "spellSchool": SCHOOL_HOLY, "positive": 1,
                "icon": ICONS + "T_Icon_Gold_116.htex", "duration": 8000, "cooldown": "120000",
                "cost": 60, "rangetype": 5, "visualization_id": 76,
                "effects": [{"type": APPLY_AREA_AURA, "aura": DMG_TAKEN_PCT, "basepoints": -20, "radius": 30.0}],
                **text_fields(("Kuppel der Zuflucht", "Sanctuary Dome", "Dôme sanctuaire", "Купол убежища"),
                              ("Du und deine Gruppenmitglieder im Umkreis von 30 Metern erleidet $D lang 20% weniger Schaden.",
                               "You and your party members within 30 yards take 20% less damage for $D.",
                               "Vous et les membres de votre groupe à moins de 30 mètres subissez 20% de dégâts en moins pendant $D.",
                               "Вы и участники группы в радиусе 30 м получаете на 20% меньше урона в течение $D."),
                              ("Erlittener Schaden um 20% verringert.", "Damage taken reduced by 20%.",
                               "Dégâts subis réduits de 20%.", "Получаемый урон снижен на 20%."))})),
        }),
        ("TALENT_PATH_WRATH", ("Strahlen", "Radiance", "Rayonnement", "Сияние"), 0xFFFF7E45, {
            "A1": talent(18,
                patch(171, ("Sengendes Licht", "Searing Light", "Lumière incendiaire", "Опаляющий свет"),
                      ("Erhöht den Schaden von Göttlicher Strafe und Heiligem Feuer um $s0%.",
                       "Increases the damage of Smite and Holy Fire by $s0%.",
                       "Augmente de $s0% les dégâts de Châtiment et de Flammes sacrées.",
                       "Увеличивает урон «Кары» и «Священного огня» на $s0%."),
                      [mod(OP_DAMAGE, 0x4, 10, pct=True)]),
                patch(172, ("Sengendes Licht", "Searing Light", "Lumière incendiaire", "Опаляющий свет"),
                      ("Erhöht den Schaden von Göttlicher Strafe und Heiligem Feuer um $s0%.",
                       "Increases the damage of Smite and Holy Fire by $s0%.",
                       "Augmente de $s0% les dégâts de Châtiment et de Flammes sacrées.",
                       "Увеличивает урон «Кары» и «Священного огня» на $s0%."),
                      [mod(OP_DAMAGE, 0x4, 20, pct=True)])),
            "A2": talent(149, passive(1220,
                ("Rasches Urteil", "Swift Verdict", "Verdict prompt", "Скорый приговор"),
                ("Verringert die Zauberzeit von Göttlicher Strafe und Heiligem Feuer um 0,4 Sek.",
                 "Reduces the cast time of Smite and Holy Fire by 0.4 sec.",
                 "Réduit de 0,4 s le temps d'incantation de Châtiment et de Flammes sacrées.",
                 "Сокращает время произнесения «Кары» и «Священного огня» на 0,4 сек."),
                "T_Icon_Gold_59.htex", SCHOOL_HOLY, [mod(OP_CAST, 0x4, -400)])),
            "A3": talent(41, patch(226,
                ("Gebündeltes Strahlen", "Focused Radiance", "Rayonnement focalisé", "Сосредоточенное сияние"),
                ("Verringert die Manakosten von Göttlicher Strafe und Heiligem Feuer um 15%.",
                 "Reduces the mana cost of Smite and Holy Fire by 15%.",
                 "Réduit de 15% le coût en mana de Châtiment et de Flammes sacrées.",
                 "Снижает затраты маны на «Кару» и «Священный огонь» на 15%."),
                [mod(OP_COST, 0x4, -15, pct=True)], icon="T_Icon_Gold_66.htex")),
            "B1": talent(150, passive(1221,
                ("Brennender Glaube", "Burning Faith", "Foi ardente", "Пылающая вера"),
                ("Erhöht den regelmäßigen Schaden deines Heiligen Feuers um $s0%.",
                 "Increases the periodic damage of your Holy Fire by $s0%.",
                 "Augmente de $s0% les dégâts périodiques de vos Flammes sacrées.",
                 "Увеличивает периодический урон «Священного огня» на $s0%."),
                "T_Icon_Gold_70.htex", SCHOOL_HOLY, [mod(OP_PERIODIC, 0x4, 50, pct=True)])),
            "M": talent(151, passive(1222,
                ("Blendender Tadel", "Dazzling Rebuke", "Réprimande éblouissante", "Ослепительный упрёк"),
                ("Göttliche Strafe und Heiliges Feuer haben eine Chance von 15%, das Ziel 2 Sek. lang zu betäuben.",
                 "Smite and Holy Fire have a 15% chance to stun the target for 2 sec.",
                 "Châtiment et Flammes sacrées ont 15% de chances d'étourdir la cible pendant 2 s.",
                 "«Кара» и «Священный огонь» с вероятностью 15% оглушают цель на 2 сек."),
                "T_Icon_Gold_106.htex", SCHOOL_HOLY, [proc(1223)],
                procflags=DONE_MAGIC_NEG, procchance=15, procfamily=0x4),
                extra=[Spell(1223, {
                    "attributes": DEBUFF_ATTRS, "spellSchool": SCHOOL_HOLY, "duration": 2000,
                    "mechanic": MECHANIC_STUN, "icon": ICONS + "T_Icon_Gold_106.htex",
                    "effects": [aura(MOD_STUN, target=T_ENEMY)],
                    **text_fields(("Geblendet", "Dazzled", "Ébloui", "Ослеплён"), ("", "", "", ""),
                                  ("Betäubt.", "Stunned.", "Étourdi.", "Оглушён."))})]),
            "B2": talent(152, passive(1224,
                ("Sonnenfeuermal", "Sunfire Brand", "Marque solaire", "Клеймо солнца"),
                ("Erhöht den kritischen Schadensbonus von Göttlicher Strafe und Heiligem Feuer um $s0%.",
                 "Increases the critical strike damage bonus of Smite and Holy Fire by $s0%.",
                 "Augmente de $s0% le bonus aux dégâts critiques de Châtiment et de Flammes sacrées.",
                 "Увеличивает бонус к критическому урону «Кары» и «Священного огня» на $s0%."),
                "T_Icon_Gold_53.htex", SCHOOL_HOLY, [mod(OP_CRIT_DAMAGE, 0x4, 100, pct=True)])),
            "C1": ph(153, placeholder(1225,
                ("Geweihter Boden", "Hallowed Ground", "Sol béni", "Освящённая земля"),
                ("Weiht den Boden unter dir und fügt Gegnern darauf 8 Sek. lang Heiligschaden zu.",
                 "Consecrates the ground beneath you, dealing holy damage to enemies standing in it over 8 sec.",
                 "Consacre le sol sous vos pieds et inflige des dégâts du Sacré aux ennemis qui s'y trouvent pendant 8 s.",
                 "Освящает землю под вами, нанося урон от светлой магии врагам на ней в течение 8 сек."),
                "T_Icon_Gold_120.htex", SCHOOL_HOLY)),
            "C2": ph(154, placeholder(1226,
                ("Dreifaches Licht", "Threefold Light", "Lumière triple", "Тройной свет"),
                ("Kanalisiert drei Lichtblitze, die einem Gegner Schaden zufügen oder einen Verbündeten heilen.",
                 "Channels three bolts of holy light that damage an enemy or heal an ally.",
                 "Canalise trois traits de lumière sacrée qui blessent un ennemi ou soignent un allié.",
                 "Направляет три луча света, которые ранят врага или исцеляют союзника."),
                "T_Icon_Gold_97.htex", SCHOOL_HOLY)),
            "CAP": talent(155, Spell(1227, {
                "attributes": AOE_ACTIVE_ATTRS, "spellSchool": SCHOOL_HOLY, "icon": ICONS + "T_Icon_Gold_86.htex",
                "cooldown": "15000", "cost": 60, "rangetype": 4, "familyflags": str(0x80),
                "maxlevel": 10, "baselevel": 0, "spelllevel": 0, "visualization_id": 9,
                "effects": [{"type": SCHOOL_DAMAGE, "basepoints": 26, "diesides": 8, "pointsperlevel": 1.2,
                             "targeta": T_SOURCE_AREA_ENEMY, "radius": 8.0, "powerbonusfactor": 0.3}],
                **text_fields(("Morgenbruch", "Dawnbreak", "Point du jour", "Рассветный удар"),
                              ("Entfesselt heiligen Zorn und fügt Gegnern in der Nähe $s0 Heiligschaden zu.",
                               "Unleashes holy wrath, dealing $s0 holy damage to nearby enemies.",
                               "Libère une colère divine qui inflige $s0 points de dégâts du Sacré aux ennemis proches.",
                               "Высвобождает священный гнев, нанося $s0 ед. урона от светлой магии врагам поблизости."))})),
        }),
    ],
}

ACOLYTE = {
    "class_id": 3, "tab_id": 2, "classmask": 4, "icon": "S_Class_Acolyte.htex",
    "paths": [
        ("TALENT_PATH_SHADOW", ("Umbra", "Umbra", "Pénombre", "Полутьма"), 0xFFB57AFF, {
            "A1": talent(160,
                passive(1300, ("Vertiefte Dunkelheit", "Deepening Dark", "Ténèbres profondes", "Сгущающаяся тьма"),
                        ("Erhöht den Schaden deiner Schattenzauber um $s0%.", "Increases the damage of your Shadow spells by $s0%.",
                         "Augmente de $s0% les dégâts de vos sorts d'Ombre.", "Увеличивает урон ваших заклинаний тьмы на $s0%."),
                        "T_Icon_Shadow_40.htex", SCHOOL_SHADOW, [mod(OP_DAMAGE, 0x1, 5, pct=True)], rank=1, baseid=1300),
                passive(1301, ("Vertiefte Dunkelheit", "Deepening Dark", "Ténèbres profondes", "Сгущающаяся тьма"),
                        ("Erhöht den Schaden deiner Schattenzauber um $s0%.", "Increases the damage of your Shadow spells by $s0%.",
                         "Augmente de $s0% les dégâts de vos sorts d'Ombre.", "Увеличивает урон ваших заклинаний тьмы на $s0%."),
                        "T_Icon_Shadow_40.htex", SCHOOL_SHADOW, [mod(OP_DAMAGE, 0x1, 10, pct=True)], rank=2, baseid=1300)),
            "A2": talent(161, passive(1302,
                ("Geflüsterter Blitz", "Whispered Bolt", "Trait murmuré", "Шёпот стрелы"),
                ("Verringert die Zauberzeit deines Schattenblitzes um 0,5 Sek.",
                 "Reduces the cast time of your Shadowbolt by 0.5 sec.",
                 "Réduit de 0,5 s le temps d'incantation de votre Trait de l'ombre.",
                 "Сокращает время произнесения «Стрелы тьмы» на 0,5 сек."),
                "T_Icon_Shadow_78.htex", SCHOOL_SHADOW, [mod(OP_CAST, 0x10, -500)])),
            "A3": talent(162, passive(1303,
                ("Dunkle Einsicht", "Dark Insight", "Sombre clairvoyance", "Тёмное прозрение"),
                ("Erhöht die kritische Trefferchance deiner Schattenzauber um $s0%.",
                 "Increases the critical strike chance of your Shadow spells by $s0%.",
                 "Augmente de $s0% les chances de coup critique de vos sorts d'Ombre.",
                 "Увеличивает шанс критического удара ваших заклинаний тьмы на $s0%."),
                "T_Icon_Shadow_33.htex", SCHOOL_SHADOW, [mod(OP_CRIT, 0x1, 6)])),
            "B1": ph(163, placeholder(1304,
                ("Auflösung", "Unraveling", "Délitement", "Распад сути"),
                ("Deine Schattenblitze machen das Ziel verwundbar: +3% erlittener Schattenschaden pro Stapel, bis zu 5-mal.",
                 "Your Shadowbolts make the target vulnerable: +3% shadow damage taken per stack, up to 5 stacks.",
                 "Vos Traits de l'ombre rendent la cible vulnérable : +3% de dégâts d'Ombre subis par cumul, jusqu'à 5 fois.",
                 "Ваши «Стрелы тьмы» делают цель уязвимой: +3% получаемого урона от тьмы за заряд, до 5 зарядов."),
                "T_Icon_Shadow_64.htex", SCHOOL_SHADOW)),
            "M": talent(164, passive(1305,
                ("Zwielicht", "Gloaming", "Brune", "Сумрак"),
                ("Die Schadenseffekte deiner Verderbenden Berührung haben eine Chance von 10%, deinen nächsten Schattenblitz innerhalb von 10 Sek. sofort wirkbar zu machen.",
                 "The damage ticks of your Corrupting Touch have a 10% chance to make your next Shadowbolt within 10 sec instant.",
                 "Les dégâts périodiques de votre Toucher corrupteur ont 10% de chances de rendre instantané votre prochain Trait de l'ombre dans les 10 s.",
                 "Периодический урон «Порчи» с вероятностью 10% делает следующую «Стрелу тьмы» в течение 10 сек. мгновенной."),
                "T_Icon_Shadow_85.htex", SCHOOL_SHADOW, [proc(1306, T_CASTER)],
                procflags=DONE_PERIODIC, procchance=10, procfamily=0x2),
                extra=[Spell(1306, {
                    "attributes": BUFF_ATTRS, "spellSchool": SCHOOL_SHADOW, "positive": 1,
                    "duration": 10000, "icon": ICONS + "T_Icon_Shadow_85.htex",
                    "effects": [mod(OP_CAST, 0x10, -100, pct=True)],
                    "procflags": DONE_MAGIC_NEG, "procchance": 100, "proccharges": 1, "procfamily": 0x10,
                    **text_fields(("Zwielichttrance", "Gloaming Trance", "Transe de la brune", "Сумеречный транс"),
                                  ("", "", "", ""),
                                  ("Dein nächster Schattenblitz ist sofort wirkbar.", "Your next Shadowbolt is instant.",
                                   "Votre prochain Trait de l'ombre est instantané.", "Следующая «Стрела тьмы» мгновенна."))})]),
            "B2": talent(2, patch(82,
                ("Abgründige Reichweite", "Abyssal Reach", "Portée abyssale", "Бездонный охват"),
                ("Erhöht die Reichweite deiner Schattenzauber und deiner Furcht um 20%.",
                 "Increases the range of your Shadow spells and Fear by 20%.",
                 "Augmente de 20% la portée de vos sorts d'Ombre et de Peur.",
                 "Увеличивает дальность ваших заклинаний тьмы и «Страха» на 20%."),
                [mod(OP_RANGE, 0x5, 20, pct=True)])),
            "C1": ph(165, placeholder(1307,
                ("Reißendes Flüstern", "Rending Whispers", "Murmures déchirants", "Раздирающий шёпот"),
                ("Greift 3 Sek. lang den Geist des Ziels an, verursacht Schattenschaden und verlangsamt es um 50%.",
                 "Assaults the target's mind for 3 sec, dealing shadow damage and slowing it by 50%.",
                 "Assaille l'esprit de la cible pendant 3 s, lui infligeant des dégâts d'Ombre et la ralentissant de 50%.",
                 "Терзает разум цели 3 сек., нанося урон от тьмы и замедляя её на 50%."),
                "T_Icon_Shadow_50.htex", SCHOOL_SHADOW)),
            "C2": ph(166, placeholder(1308,
                ("Hungriger Schatten", "Hungering Shade", "Ombre affamée", "Голодная тень"),
                ("Beschwört 15 Sek. lang einen Schattengeist, der dein Ziel angreift. Jeder Treffer stellt Mana wieder her.",
                 "Summons a shadowfiend that attacks your target for 15 sec. Each hit restores your mana.",
                 "Invoque un ombrefiel qui attaque votre cible pendant 15 s. Chaque coup vous rend du mana.",
                 "Призывает исчадие тьмы, атакующее вашу цель 15 сек. Каждый удар восполняет вам ману."),
                "T_Icon_Shadow_80.htex", SCHOOL_SHADOW)),
            "CAP": talent(167, Spell(1309, {
                "attributes": AOE_ACTIVE_ATTRS, "spellSchool": SCHOOL_SHADOW, "icon": ICONS + "T_Icon_Shadow_58.htex",
                "cooldown": "12000", "cost": 55, "rangetype": 4, "familyflags": str(0x20),
                "maxlevel": 10, "baselevel": 0, "spelllevel": 0, "visualization_id": 55,
                "effects": [{"type": SCHOOL_DAMAGE, "basepoints": 28, "diesides": 8, "pointsperlevel": 1.2,
                             "targeta": T_SOURCE_AREA_ENEMY, "radius": 8.0, "powerbonusfactor": 0.3}],
                **text_fields(("Schattenblüte", "Umbral Bloom", "Floraison ombrale", "Теневое цветение"),
                              ("Entfesselt eine Welle der Leere und fügt Gegnern in der Nähe $s0 Schattenschaden zu.",
                               "Unleashes a wave of void energy, dealing $s0 shadow damage to nearby enemies.",
                               "Libère une vague d'énergie du Vide qui inflige $s0 points de dégâts d'Ombre aux ennemis proches.",
                               "Высвобождает волну энергии Бездны, нанося $s0 ед. урона от тьмы врагам поблизости."))})),
        }),
        ("TALENT_PATH_DECAY", ("Fäule", "Blight", "Flétrissure", "Порча"), 0xFF8CD35E, {
            "A1": talent(3, patch(84,
                ("Rascher Verfall", "Rapid Decay", "Putréfaction rapide", "Стремительный распад"),
                ("Deine Verderbende Berührung wird sofort gewirkt.", "Your Corrupting Touch becomes instant.",
                 "Votre Toucher corrupteur devient instantané.", "«Порча» применяется мгновенно."),
                [mod(OP_CAST, 0x2, -1500)])),
            "A2": talent(4, patch(25,
                ("Endloser Verfall", "Endless Decay", "Putréfaction sans fin", "Бесконечный тлен"),
                ("Erhöht die Dauer deiner Verderbenden Berührung um $s0%.",
                 "Increases the duration of your Corrupting Touch by $s0%.",
                 "Augmente de $s0% la durée de votre Toucher corrupteur.",
                 "Увеличивает длительность «Порчи» на $s0%."),
                [mod(OP_DURATION, 0x2, 33, pct=True)])),
            "A3": talent(168,
                passive(1310, ("Verdorren", "Withering", "Flétrissure", "Иссушение"),
                        ("Erhöht den regelmäßigen Schaden deiner Verderbenden Berührung um $s0%.",
                         "Increases the periodic damage of your Corrupting Touch by $s0%.",
                         "Augmente de $s0% les dégâts périodiques de votre Toucher corrupteur.",
                         "Увеличивает периодический урон «Порчи» на $s0%."),
                        "T_Icon_Unholy_116.htex", SCHOOL_SHADOW, [mod(OP_PERIODIC, 0x2, 10, pct=True)], rank=1, baseid=1310),
                passive(1311, ("Verdorren", "Withering", "Flétrissure", "Иссушение"),
                        ("Erhöht den regelmäßigen Schaden deiner Verderbenden Berührung um $s0%.",
                         "Increases the periodic damage of your Corrupting Touch by $s0%.",
                         "Augmente de $s0% les dégâts périodiques de votre Toucher corrupteur.",
                         "Увеличивает периодический урон «Порчи» на $s0%."),
                        "T_Icon_Unholy_116.htex", SCHOOL_SHADOW, [mod(OP_PERIODIC, 0x2, 20, pct=True)], rank=2, baseid=1310)),
            "B1": ph(169, placeholder(1312,
                ("Kriechende Fäulnis", "Creeping Rot", "Pourriture rampante", "Ползучая гниль"),
                ("Deine Verderbende Berührung springt bei jedem Schaden auf einen Gegner in der Nähe über, bis zu 2-mal.",
                 "Corrupting Touch jumps to a nearby enemy every time it deals damage, up to 2 times.",
                 "Votre Toucher corrupteur passe à un ennemi proche à chaque fois qu'il inflige des dégâts, jusqu'à 2 fois.",
                 "«Порча» при каждом нанесении урона перескакивает на ближайшего врага, до 2 раз."),
                "T_Icon_Unholy_42.htex", SCHOOL_SHADOW)),
            "M": talent(170, passive(1313,
                ("Saugende Fäulnis", "Leeching Rot", "Pourriture sangsue", "Пиявочная гниль"),
                ("Jeder Schadenseffekt deiner Verderbenden Berührung heilt dich um einen kleinen Betrag.",
                 "Every damage tick of your Corrupting Touch heals you for a small amount.",
                 "Chaque dégât périodique de votre Toucher corrupteur vous soigne d'un petit montant.",
                 "Каждый тик урона «Порчи» немного исцеляет вас."),
                "T_Icon_Unholy_27.htex", SCHOOL_SHADOW, [proc(1314, T_CASTER)],
                procflags=DONE_PERIODIC, procchance=100, procfamily=0x2),
                extra=[Spell(1314, {
                    "attributes": BUFF_ATTRS, "spellSchool": SCHOOL_SHADOW, "positive": 1,
                    "icon": ICONS + "T_Icon_Unholy_27.htex", "maxlevel": 10, "baselevel": 1, "spelllevel": 1,
                    "effects": [{"type": HEAL, "basepoints": 2, "pointsperlevel": 0.5, "targeta": T_CASTER}],
                    **text_fields(("Saugende Fäulnis", "Leeching Rot", "Pourriture sangsue", "Пиявочная гниль"),
                                  ("Heilt dich um $s0.", "Heals you for $s0.", "Vous rend $s0 points de vie.",
                                   "Восполняет вам $s0 ед. здоровья."))})]),
            "B2": talent(14, patch(116)),
            "C1": ph(171, placeholder(1315,
                ("Miasma", "Miasma", "Miasme", "Миазмы"),
                ("Setzt am Zielort eine Wolke des Verfalls frei, die alle Gegner darin mit Verderbender Berührung belegt.",
                 "Releases a cloud of decay at the target location that afflicts all enemies inside with Corrupting Touch.",
                 "Libère un nuage de putréfaction à l'endroit ciblé qui afflige tous les ennemis à l'intérieur de Toucher corrupteur.",
                 "Выпускает облако тлена в указанной точке, накладывая «Порчу» на всех врагов внутри."),
                "T_Icon_Unholy_174.htex", SCHOOL_SHADOW)),
            "C2": ph(172, placeholder(1316,
                ("Giftiger Rückschlag", "Venomous Backlash", "Contrecoup venimeux", "Ядовитая отдача"),
                ("Wird deine Verderbende Berührung gebannt, wird der Bannende zum Schweigen gebracht und erleidet schweren Schattenschaden.",
                 "When your Corrupting Touch is dispelled, the dispeller is silenced and takes heavy shadow damage.",
                 "Quand votre Toucher corrupteur est dissipé, le dissipateur est réduit au silence et subit de lourds dégâts d'Ombre.",
                 "Если «Порчу» рассеивают, рассеявший теряет дар речи и получает сильный урон от тьмы."),
                "T_Icon_Unholy_99.htex", SCHOOL_SHADOW)),
            "CAP": talent(5, patch(63)),
        }),
        ("TALENT_PATH_DREAD", ("Grauen", "Dread", "Effroi", "Ужас"), 0xFFE0566E, {
            "A1": talent(173, passive(1320,
                ("Grauenhafte Präsenz", "Dread Presence", "Présence effroyable", "Ужасающее присутствие"),
                ("Verringert die Zauberzeit von Furcht um 1 Sek.", "Reduces the cast time of Fear by 1 sec.",
                 "Réduit de 1 s le temps d'incantation de Peur.", "Сокращает время произнесения «Страха» на 1 сек."),
                "T_Icon_Shadow_103.htex", SCHOOL_SHADOW, [mod(OP_CAST, 0x4, -1000)])),
            "A2": talent(174, passive(1321,
                ("Grabesausdauer", "Grave Endurance", "Endurance sépulcrale", "Могильная стойкость"),
                ("Erhöht deine Ausdauer um $s0%.", "Increases your Stamina by $s0%.",
                 "Augmente votre Endurance de $s0%.", "Увеличивает вашу выносливость на $s0%."),
                "T_Icon_Shadow_12.htex", SCHOOL_SHADOW, [aura(MOD_STAT_PCT, 8, STA)])),
            "A3": talent(175, passive(1322,
                ("Mitternachtszehnt", "Midnight Tithe", "Dîme de minuit", "Полуночная дань"),
                ("Erhöht deine Manaregeneration um $s0%.", "Increases your mana regeneration by $s0%.",
                 "Augmente votre régénération de mana de $s0%.", "Увеличивает восстановление маны на $s0%."),
                "T_Icon_Shadow_55.htex", SCHOOL_SHADOW, [aura(MOD_POWER_REGEN_PCT, 25, POWER_MANA)])),
            "B1": ph(176, placeholder(1323,
                ("Klage der Verlorenen", "Wail of the Lost", "Plainte des égarés", "Плач потерянных"),
                ("Stößt ein Geheul aus, das bis zu 5 Gegner in der Nähe 6 Sek. lang in Angst und Schrecken fliehen lässt.",
                 "Lets out a howl that makes up to 5 nearby enemies flee in terror for 6 sec.",
                 "Pousse un hurlement qui fait fuir de terreur jusqu'à 5 ennemis proches pendant 6 s.",
                 "Испускает вой, обращающий в бегство до 5 ближайших врагов на 6 сек."),
                "T_Icon_Shadow_29.htex", SCHOOL_SHADOW)),
            "M": talent(177, passive(1324,
                ("Seelenrüstung", "Soul Armor", "Armure d'âme", "Доспех души"),
                ("Erhöht deine Rüstung um $s0%.", "Increases your armor by $s0%.",
                 "Augmente votre armure de $s0%.", "Увеличивает вашу броню на $s0%."),
                "T_Icon_Shadow_45.htex", SCHOOL_SHADOW, [aura(MOD_RESIST_PCT, 15)])),
            "B2": ph(178, placeholder(1325,
                ("Greifendes Grauen", "Grasping Dread", "Effroi agrippant", "Цепкий ужас"),
                ("Schleudert eine Schattenspirale, die den Gegner 3 Sek. lang entsetzt und dich um den verursachten Schaden heilt.",
                 "Hurls a coil of shadow that horrifies the enemy for 3 sec and heals you for the damage dealt.",
                 "Projette une spirale d'ombre qui horrifie l'ennemi pendant 3 s et vous soigne des dégâts infligés.",
                 "Бросает виток тьмы, приводящий врага в ужас на 3 сек. и исцеляющий вас на величину урона."),
                "T_Icon_Shadow_89.htex", SCHOOL_SHADOW)),
            "C1": ph(179, placeholder(1326,
                ("Verknotete Zunge", "Tangled Tongue", "Langue nouée", "Заплетающийся язык"),
                ("Verflucht das Ziel und erhöht 30 Sek. lang seine Zauberzeit um 50%.",
                 "Curses the target, increasing its casting time by 50% for 30 sec.",
                 "Maudit la cible et augmente son temps d'incantation de 50% pendant 30 s.",
                 "Проклинает цель, увеличивая время произнесения её заклинаний на 50% на 30 сек."),
                "T_Icon_Shadow_100.htex", SCHOOL_SHADOW)),
            "C2": ph(180, placeholder(1327,
                ("Gebundener Diener", "Bound Servant", "Serviteur lié", "Связанный слуга"),
                ("Teilt 20% des Schadens, den du erleidest, mit deinem beschworenen Diener.",
                 "Shares 20% of the damage you take with your summoned servant.",
                 "Partage 20% des dégâts que vous subissez avec votre serviteur invoqué.",
                 "Перенаправляет 20% получаемого урона на вашего призванного слугу."),
                "T_Icon_Shadow_99.htex", SCHOOL_SHADOW)),
            "CAP": talent(181, Spell(1328, {
                "attributes": SELF_ACTIVE_ATTRS, "spellSchool": SCHOOL_SHADOW, "positive": 1,
                "icon": ICONS + "T_Icon_Shadow_118.htex", "duration": 20000, "cooldown": "120000",
                "cost": 40, "visualization_id": 2,
                "effects": [mod(OP_DAMAGE, 0x1, 20, pct=True), aura(DMG_TAKEN_PCT, -10)],
                **text_fields(("Umarmung des Grauens", "Embrace of Dread", "Étreinte de l'effroi", "Объятия ужаса"),
                              ("Du wirst $D lang zum Avatar des Grauens: 20% mehr Schattenschaden und 10% weniger erlittener Schaden.",
                               "You become an avatar of dread for $D: 20% more shadow damage and 10% less damage taken.",
                               "Vous devenez un avatar de l'effroi pendant $D : 20% de dégâts d'Ombre en plus et 10% de dégâts subis en moins.",
                               "Вы становитесь воплощением ужаса на $D: урон от тьмы +20%, получаемый урон −10%."),
                              ("Schattenschaden um 20% erhöht, erlittener Schaden um 10% verringert.",
                               "Shadow damage increased by 20%, damage taken reduced by 10%.",
                               "Dégâts d'Ombre augmentés de 20%, dégâts subis réduits de 10%.",
                               "Урон от тьмы увеличен на 20%, получаемый урон снижен на 10%."))})),
        }),
    ],
}

SCOUT = {
    "class_id": 4, "tab_id": 1, "classmask": 8, "icon": "S_Class_Scout.htex",
    "paths": [
        ("TALENT_PATH_COMBAT", ("Duellant", "Duelist", "Duelliste", "Дуэлянт"), 0xFFF0B44C, {
            "A1": talent(190, passive(1400,
                ("Zwillingszähne", "Twin Fangs", "Crocs jumeaux", "Парные клыки"),
                ("Erhöht den Schaden von Doppelschnitt um $s0%.", "Increases the damage of Twin Slash by $s0%.",
                 "Augmente de $s0% les dégâts de Double entaille.", "Увеличивает урон «Двойного рассечения» на $s0%."),
                "T_Icon_Energy_38.htex", SCHOOL_PHYSICAL, [mod(OP_DAMAGE, 0x10, 15, pct=True)])),
            "A2": talent(191, passive(1401,
                ("Flinke Hände", "Quick Hands", "Mains agiles", "Ловкие руки"),
                ("Verringert die Energiekosten von Schneller Schnitt um 8.", "Reduces the Energy cost of Quick Cut by 8.",
                 "Réduit de 8 le coût en énergie d'Entaille rapide.", "Снижает затраты энергии на «Быстрый надрез» на 8."),
                "T_Icon_Energy_83.htex", SCHOOL_PHYSICAL, [mod(OP_COST, 0x40, -8)])),
            "A3": talent(192, passive(1402,
                ("Messerjongleur", "Knife Juggler", "Jongleur de lames", "Жонглёр ножами"),
                ("Verringert die Abklingzeit von Wurfmesser um 3 Sek. und erhöht seinen Schaden um 20%.",
                 "Reduces the cooldown of Throwing Knife by 3 sec and increases its damage by 20%.",
                 "Réduit de 3 s le temps de recharge de Couteau de lancer et augmente ses dégâts de 20%.",
                 "Сокращает время восстановления «Метательного ножа» на 3 сек. и увеличивает его урон на 20%."),
                "T_Icon_BloodCombat_28.htex", SCHOOL_PHYSICAL,
                [mod(OP_COOLDOWN, 0x20, -3000), mod(OP_DAMAGE, 0x20, 20, pct=True)])),
            "B1": ph(193, placeholder(1403,
                ("Gegenhieb", "Counterstroke", "Contre-coup", "Встречный удар"),
                ("Nachdem du ausgewichen bist, schlägst du mit 150% Waffenschaden zurück und entwaffnest den Angreifer 4 Sek. lang.",
                 "After you dodge, riposte for 150% weapon damage and disarm the attacker for 4 sec.",
                 "Après une esquive, ripostez pour 150% des dégâts de l'arme et désarmez l'attaquant pendant 4 s.",
                 "После уклонения наносит ответный удар (150% урона оружием) и обезоруживает атакующего на 4 сек."),
                "T_Icon_Energy_43.htex", SCHOOL_PHYSICAL)),
            "M": talent(194, passive(1404,
                ("Zweiter Atem", "Second Wind", "Second souffle", "Второе дыхание"),
                ("Erhöht deine Energieregeneration um $s0%.", "Increases your Energy regeneration by $s0%.",
                 "Augmente votre régénération d'énergie de $s0%.", "Увеличивает восстановление энергии на $s0%."),
                "T_Icon_Energy_14.htex", SCHOOL_PHYSICAL, [aura(MOD_POWER_REGEN_PCT, 15, POWER_ENERGY)])),
            "B2": talent(195,
                passive(1405, ("Vitalpunkte", "Vital Points", "Points vitaux", "Уязвимые точки"),
                        ("Erhöht den kritischen Schadensbonus deiner Nahkampftechniken um $s0%.",
                         "Increases the critical strike damage bonus of your melee techniques by $s0%.",
                         "Augmente de $s0% le bonus aux dégâts critiques de vos techniques de mêlée.",
                         "Увеличивает бонус к критическому урону ваших приёмов ближнего боя на $s0%."),
                        "T_Icon_BloodCombat_01.htex", SCHOOL_PHYSICAL, [mod(OP_CRIT_DAMAGE, 0x53, 15, pct=True)],
                        rank=1, baseid=1405),
                passive(1406, ("Vitalpunkte", "Vital Points", "Points vitaux", "Уязвимые точки"),
                        ("Erhöht den kritischen Schadensbonus deiner Nahkampftechniken um $s0%.",
                         "Increases the critical strike damage bonus of your melee techniques by $s0%.",
                         "Augmente de $s0% le bonus aux dégâts critiques de vos techniques de mêlée.",
                         "Увеличивает бонус к критическому урону ваших приёмов ближнего боя на $s0%."),
                        "T_Icon_BloodCombat_01.htex", SCHOOL_PHYSICAL, [mod(OP_CRIT_DAMAGE, 0x53, 30, pct=True)],
                        rank=2, baseid=1405)),
            "C1": ph(196, placeholder(1407,
                ("Tanzende Klingen", "Dancing Blades", "Lames dansantes", "Танцующие клинки"),
                ("15 Sek. lang treffen deine Angriffe zusätzlich einen Gegner in der Nähe.",
                 "For 15 sec your strikes also hit a nearby enemy.",
                 "Pendant 15 s, vos coups touchent aussi un ennemi proche.",
                 "В течение 15 сек. ваши удары поражают также ближайшего врага."),
                "T_Icon_Energy_05.htex", SCHOOL_PHYSICAL)),
            "C2": ph(197, placeholder(1408,
                ("Flackernder Stahl", "Flickering Steel", "Acier vacillant", "Мерцающая сталь"),
                ("Teleportiert dich zu bis zu 5 Gegnern im Umkreis von 10 Metern und trifft sie in schneller Folge.",
                 "Teleports to and strikes up to 5 enemies within 10 yards in rapid succession.",
                 "Vous téléporte vers jusqu'à 5 ennemis à moins de 10 mètres et les frappe en succession rapide.",
                 "Стремительно перемещается к 5 врагам в радиусе 10 м и поражает их одного за другим."),
                "T_Icon_Energy_80.htex", SCHOOL_PHYSICAL)),
            "CAP": talent(198, Spell(1409, {
                "attributes": SELF_ACTIVE_ATTRS, "spellSchool": SCHOOL_PHYSICAL, "positive": 1,
                "icon": ICONS + "T_Icon_Energy_70.htex", "duration": 15000, "cooldown": "180000",
                "powertype": POWER_ENERGY, "cost": 0, "visualization_id": 4,
                "effects": [aura(MOD_POWER_REGEN_PCT, 100, POWER_ENERGY)],
                **text_fields(("Fieberrausch", "Fever Pitch", "Fièvre du combat", "Боевая лихорадка"),
                              ("Verdoppelt $D lang deine Energieregeneration.", "Doubles your Energy regeneration for $D.",
                               "Double votre régénération d'énergie pendant $D.", "Удваивает восстановление энергии на $D."),
                              ("Energieregeneration um $s0% erhöht.", "Energy regeneration increased by $s0%.",
                               "Régénération d'énergie augmentée de $s0%.", "Восстановление энергии увеличено на $s0%."))})),
        }),
        ("TALENT_PATH_ASSASSINATION", ("Hinterhalt", "Ambush", "Embuscade", "Засада"), 0xFF86D65E, {
            "A1": talent(1, patch(81,
                ("Grausame Effizienz", "Cruel Efficiency", "Efficacité cruelle", "Жестокая сноровка"),
                ("Verringert die Energiekosten deines Verwundenden Stoßes um 10.",
                 "Reduces the Energy cost of your Wounding Strike by 10.",
                 "Réduit de 10 le coût en énergie de votre Frappe vulnérante.",
                 "Снижает затраты энергии на «Ранящий удар» на 10."),
                [mod(OP_COST, 0x2, -10)])),
            "A2": talent(199, passive(1410,
                ("Mörderische Absicht", "Murderous Intent", "Intention meurtrière", "Жажда убийства"),
                ("Erhöht den Schaden von Erbarmungsloser Stoß um $s0%.", "Increases the damage of Ruthless Strike by $s0%.",
                 "Augmente de $s0% les dégâts de Frappe impitoyable.", "Увеличивает урон «Безжалостного удара» на $s0%."),
                "T_Icon_BloodCombat_15.htex", SCHOOL_PHYSICAL, [mod(OP_DAMAGE, 0x1, 15, pct=True)])),
            "A3": talent(200, passive(1411,
                ("Schwachstelle", "Weak Spot", "Point faible", "Слабое место"),
                ("Erhöht die kritische Trefferchance von Erbarmungsloser Stoß und Verwundender Stoß um $s0%.",
                 "Increases the critical strike chance of Ruthless Strike and Wounding Strike by $s0%.",
                 "Augmente de $s0% les chances de coup critique de Frappe impitoyable et de Frappe vulnérante.",
                 "Повышает шанс критического удара «Безжалостного удара» и «Ранящего удара» на $s0%."),
                "T_Icon_BloodCombat_02.htex", SCHOOL_PHYSICAL, [mod(OP_CRIT, 0x3, 8)])),
            "B1": ph(201, placeholder(1412,
                ("Lohn des Schnitters", "Reaper's Due", "Dû du faucheur", "Доля жнеца"),
                ("Kritische Treffer mit Erbarmungsloser Stoß erstatten 15 Energie zurück.",
                 "Critical strikes with Ruthless Strike refund 15 Energy.",
                 "Les coups critiques de Frappe impitoyable rendent 15 points d'énergie.",
                 "Критические удары «Безжалостным ударом» возвращают 15 ед. энергии."),
                "T_Icon_BloodCombat_11.htex", SCHOOL_PHYSICAL)),
            "M": talent(17, patch(169)),
            "B2": talent(202, passive(1414,
                ("Bloßgelegte Schwäche", "Exposed Weakness", "Faiblesse exposée", "Обнажённая слабость"),
                ("Verringert die Abklingzeit von Schwäche markieren um 10 Sek.",
                 "Reduces the cooldown of Mark Weakness by 10 sec.",
                 "Réduit de 10 s le temps de recharge de Marquer la faiblesse.",
                 "Сокращает время восстановления «Метки слабости» на 10 сек."),
                "T_Icon_BloodCombat_09.htex", SCHOOL_PHYSICAL, [mod(OP_COOLDOWN, 0x100, -10000)])),
            "C1": ph(203, placeholder(1415,
                ("Giftige Ernte", "Toxic Harvest", "Récolte toxique", "Ядовитая жатва"),
                ("Verbraucht die Blutung deines Verwundenden Stoßes und verursacht sofort Naturschaden.",
                 "Consumes your Wounding Strike bleed to deal instant nature damage.",
                 "Consume le saignement de votre Frappe vulnérante pour infliger des dégâts de Nature instantanés.",
                 "Поглощает кровотечение от «Ранящего удара», мгновенно нанося урон от сил природы."),
                "S_Poison_01.htex", SCHOOL_PHYSICAL)),
            "C2": ph(204, placeholder(1416,
                ("Blutschwur", "Blood Oath", "Serment de sang", "Кровная клятва"),
                ("Markiert einen Gegner zum Tode: Du verursachst 20 Sek. lang 20% mehr Schaden an ihm.",
                 "Marks an enemy for death: you deal 20% more damage to it for 20 sec.",
                 "Marque un ennemi pour la mort : vous lui infligez 20% de dégâts supplémentaires pendant 20 s.",
                 "Обрекает врага на смерть: вы наносите ему на 20% больше урона в течение 20 сек."),
                "T_Icon_BloodCombat_22.htex", SCHOOL_PHYSICAL)),
            "CAP": talent(205, Spell(1413, {
                "attributes": SELF_ACTIVE_ATTRS, "spellSchool": SCHOOL_PHYSICAL, "positive": 1,
                "icon": ICONS + "T_Icon_BloodCombat_05.htex", "duration": 20000, "cooldown": "90000",
                "powertype": POWER_ENERGY, "cost": 0,
                "effects": [mod(OP_CRIT, 0x53, 100)],
                "procflags": DONE_MELEE_SPELL, "procchance": 100, "proccharges": 1,
                **text_fields(("Ruhige Hand", "Steady Hand", "Main sûre", "Твёрдая рука"),
                              ("Deine nächste Nahkampftechnik innerhalb von $D trifft garantiert kritisch.",
                               "Your next melee technique within $D is a guaranteed critical strike.",
                               "Votre prochaine technique de mêlée dans les $D est un coup critique assuré.",
                               "Ваш следующий приём ближнего боя в течение $D гарантированно наносит критический урон."),
                              ("Deine nächste Nahkampftechnik trifft kritisch.", "Your next melee technique is a critical strike.",
                               "Votre prochaine technique de mêlée est un coup critique.",
                               "Следующий приём ближнего боя будет критическим."))})),
        }),
        ("TALENT_PATH_SUBTLETY", ("Gaukler", "Trickster", "Filou", "Ловкач"), 0xFFA389FF, {
            "A1": talent(13, patch(111,
                ("Leichtfüßig", "Fleet-Footed", "Pied léger", "Быстроногий"),
                ("Verringert die Abklingzeit deines Sprints um 20 Sek.", "Reduces the cooldown of your Sprint by 20 sec.",
                 "Réduit de 20 s le temps de recharge de votre Sprint.", "Сокращает время восстановления «Спринта» на 20 сек."),
                [mod(OP_COOLDOWN, 0x8, -20000)])),
            "A2": talent(16, patch(162, ("Licht aus", "Lights Out", "Extinction des feux", "Отбой"))),
            "A3": talent(206, passive(1420,
                ("Aalglatt", "Slippery", "Glissant", "Скользкий"),
                ("Verringert die Abklingzeit von Ausweichschritt um 8 Sek.", "Reduces the cooldown of Evasive Step by 8 sec.",
                 "Réduit de 8 s le temps de recharge de Pas évasif.", "Сокращает время восстановления «Шага уклонения» на 8 сек."),
                "T_Icon_Energy_51.htex", SCHOOL_PHYSICAL, [mod(OP_COOLDOWN, 0x80, -8000)])),
            "B1": ph(207, placeholder(1421,
                ("Chamäleon", "Chameleon", "Caméléon", "Хамелеон"),
                ("Du verschmilzt mit deiner Umgebung: Verstohlenheit verlangsamt dich außerhalb des Kampfes nicht mehr.",
                 "Blend into your surroundings: stealth no longer slows you while out of combat.",
                 "Fondez-vous dans le décor : le camouflage ne vous ralentit plus hors combat.",
                 "Вы сливаетесь с окружением: незаметность больше не замедляет вас вне боя."),
                "T_Icon_Unholy_131.htex", SCHOOL_PHYSICAL)),
            "M": talent(208, passive(1422,
                ("Gewandtheit", "Nimble", "Agilité", "Проворство"),
                ("Erhöht deine Chance, Nahkampfangriffen auszuweichen, um $s0%.",
                 "Increases your chance to dodge melee attacks by $s0%.",
                 "Augmente de $s0% vos chances d'esquiver les attaques en mêlée.",
                 "Повышает вероятность уклониться от атак ближнего боя на $s0%."),
                "T_Icon_Unholy_71.htex", SCHOOL_PHYSICAL, [aura(MOD_DODGE, 5)])),
            "B2": ph(209, placeholder(1423,
                ("Hinter dir", "Behind You", "Derrière toi", "За спиной"),
                ("Tritt durch die Schatten und erscheint hinter deinem Ziel.", "Step through the shadows to appear behind your target.",
                 "Passez à travers les ombres pour apparaître derrière votre cible.", "Проходит сквозь тени и появляется за спиной цели."),
                "T_Icon_Shadow_44.htex", SCHOOL_PHYSICAL)),
            "C1": ph(210, placeholder(1424,
                ("Nicht heute", "Not Today", "Pas aujourd'hui", "Не сегодня"),
                ("Ein tödlicher Schlag lässt dich stattdessen mit 10% Gesundheit zurück und verringert 3 Sek. lang den erlittenen Schaden um 80%. Einmal alle 90 Sek.",
                 "A killing blow leaves you at 10% health instead and reduces damage taken by 80% for 3 sec. Once every 90 sec.",
                 "Un coup fatal vous laisse à 10% de vie à la place et réduit les dégâts subis de 80% pendant 3 s. Une fois toutes les 90 s.",
                 "Смертельный удар вместо этого оставляет вам 10% здоровья и снижает получаемый урон на 80% на 3 сек. Раз в 90 сек."),
                "T_Icon_Shadow_87.htex", SCHOOL_PHYSICAL)),
            "C2": ph(211, placeholder(1425,
                ("Trickkiste", "Bag of Tricks", "Sac à malices", "Мешок фокусов"),
                ("Beendet sofort die Abklingzeit von Sprinten, Ausweichschritt und Niederschlagen.",
                 "Finishes the cooldown of Sprint, Evasive Step and Knockout.",
                 "Termine le temps de recharge de Sprint, Pas évasif et Assommer.",
                 "Мгновенно завершает восстановление «Спринта», «Шага уклонения» и «Нокаута»."),
                "T_Icon_Gold_100.htex", SCHOOL_PHYSICAL)),
            "CAP": talent(212, Spell(1426, {
                "attributes": SELF_ACTIVE_ATTRS, "spellSchool": SCHOOL_PHYSICAL, "positive": 1,
                "icon": ICONS + "T_Icon_Energy_30.htex", "duration": 10000, "cooldown": "180000",
                "powertype": POWER_ENERGY, "cost": 0,
                "effects": [aura(MOD_DODGE, 50)],
                **text_fields(("Rauch und Spiegel", "Smoke and Mirrors", "Écran de fumée", "Дым и зеркала"),
                              ("Erhöht $D lang deine Ausweichchance um $s0%.",
                               "Increases your dodge chance by $s0% for $D.",
                               "Augmente vos chances d'esquive de $s0% pendant $D.",
                               "Повышает вероятность уклонения на $s0% на $D."),
                              ("Ausweichchance um $s0% erhöht.", "Dodge chance increased by $s0%.",
                               "Chances d'esquive augmentées de $s0%.", "Вероятность уклонения повышена на $s0%."))})),
        }),
    ],
}

CLASSES = [MAGE, WARRIOR, CLERIC, ACOLYTE, SCOUT]

# Family flag changes so every modifier above hits exactly the spells it names.
FAMILY_FLAGS = {
    23: 0x11, 74: 0x11,      # Shadowbolt: own bit 0x10 (Corrupting Touch shares 0x1)
    73: 0x4, 38: 0x8,        # Fear, Veil of Shadows
    168: 0x10, 167: 0x20,    # Twin Slash, Throwing Knife (shared bits with Knockout/Sprint)
    163: 0x40, 166: 0x80,    # Quick Cut, Evasive Step
    169: 0x100,              # Mark Weakness
}

# Rank spells of the old trees that no talent uses any more (the first --apply listed them).
# Pinned here because a re-run can no longer see them in the talent data.
RETIRED_RANK_SPELLS = {
    27, 64, 80, 83, 86, 87, 88, 89, 90, 91, 92, 96, 97, 102, 103, 107, 108, 109, 110, 114, 115,
    141, 173, 174, 178, 184, 186, 190, 192, 194, 195, 197, 200, 201, 203, 208, 210, 212, 214,
    218, 219, 221, 223, 225, 227,
}

# Path label keys -> texts (de, en, fr, ru), filled from CLASSES, plus the frame strings.
UI_STRINGS = {
    "TALENT_COMING_SOON": ("Demnächst verfügbar", "Coming soon", "Bientôt disponible", "Скоро"),
}

# --- Data handling -----------------------------------------------------------------------------

def _require(condition, message):
    """Like assert, but survives ``python -O`` -- these guards gate writing game data."""
    if not condition:
        raise SystemExit(message)


def load_type(schema_dir, proto, message_name):
    with tempfile.TemporaryDirectory(prefix="talents_") as tmp:
        desc = Path(tmp) / "d.pb"
        subprocess.run([str(find_protoc(ROOT)), f"-I{schema_dir}", f"--descriptor_set_out={desc}",
                        "--include_imports", proto], cwd=schema_dir, check=True)
        file_set = descriptor_pb2.FileDescriptorSet.FromString(desc.read_bytes())
    pool = descriptor_pool.DescriptorPool()
    for entry in file_set.file:
        pool.Add(entry)
    return message_factory.GetMessageClass(pool.FindMessageTypeByName(message_name))


EDITOR_TYPES = {
    "spells": ("spells.proto", "mmo.proto.Spells"),
    "talents": ("talents.proto", "mmo.proto.Talents"),
    "talent_tabs": ("talent_tabs.proto", "mmo.proto.TalentTabs"),
}


def editor_path(name):
    return ROOT / f"data/editor/data/{name}.data"


def client_path(name):
    return ROOT / f"data/client/ClientDB/{name}.data"


def load_editor(name):
    proto, message = EDITOR_TYPES[name]
    message_type = load_type(ROOT / "src/shared/proto_data", proto, message)
    return message_type.FromString(editor_path(name).read_bytes())


def all_spell_drafts(cls):
    for _, _, _, slots in cls["paths"]:
        for node in slots.values():
            for rank in node["ranks"]:
                yield rank, node
            for extra in node["extra"]:
                yield extra, node


def apply_spell(dataset_by_id, dataset, draft, classmask):
    target = dataset_by_id.get(draft.id)
    if draft.existing:
        _require(target is not None, f"spell {draft.id} does not exist")
        fields = dict(draft.fields)
        if "effects" in fields:
            # Talent passives are hidden auras; an old aura text would read removed effects.
            target.auratext = ""
            target.ClearField("auratext_loc")
            del target.effects[:]
            for index, effect in enumerate(fields.pop("effects")):
                json_format.ParseDict({"index": index, **effect}, target.effects.add())
        for key in ("name_loc", "description_loc"):
            if key in fields:
                target.ClearField(key)
        json_format.ParseDict(fields, target)
        return target

    if target is None:
        target = dataset.entry.add()
        dataset_by_id[draft.id] = target
    target.Clear()
    fields = {"id": draft.id, "classmask": classmask, "rank": 0, "baseid": 0, **draft.fields}
    effects = fields.pop("effects", [])
    json_format.ParseDict(fields, target)
    for index, effect in enumerate(effects):
        json_format.ParseDict({"index": index, **effect}, target.effects.add())
    return target


def build(spells, talents, tabs):
    spells_by_id = {spell.id: spell for spell in spells.entry}
    talents_by_id = {entry.id: entry for entry in talents.entry}
    tabs_by_id = {entry.id: entry for entry in tabs.entry}
    old_ranks = {entry.id: list(entry.ranks) for entry in talents.entry}

    # Talents of the five class tabs that the new trees no longer contain are removed.
    class_tabs = {cls["tab_id"] for cls in CLASSES}
    authored_talents = {node["id"] for cls in CLASSES for _, _, _, s in cls["paths"] for node in s.values()}
    removed = [entry.id for entry in talents.entry if entry.tab in class_tabs and entry.id not in authored_talents]

    used_rank_spells = set()
    labels = dict(UI_STRINGS)
    for cls in CLASSES:
        tab = tabs_by_id.get(cls["tab_id"])
        _require(tab is not None, f"talent tab {cls['tab_id']} missing")
        _require(tab.class_id == cls["class_id"], f"tab {tab.id} belongs to class {tab.class_id}")
        tab.icon = ICONS + cls["icon"]
        tab.canvas_width, tab.canvas_height = CANVAS_W, CANVAS_H
        tab.initial_zoom = 0.95
        tab.hub_x, tab.hub_y = HUB
        del tab.labels[:]

        for path_index, (label_key, label_texts, color, slots) in enumerate(cls["paths"]):
            _require(set(slots) == set(SLOTS), f"{label_key}: slots {sorted(slots)}")
            angle = PATH_ANGLES[path_index]
            label = tab.labels.add()
            label.text = label_key
            label.x, label.y = label_position(angle)
            label.color = color
            labels[label_key] = label_texts

            for slot, node in slots.items():
                radius, offset, required, scale, parent, tier = SLOTS[slot]
                entry = talents_by_id.get(node["id"])
                if entry is None:
                    entry = talents.entry.add()
                    talents_by_id[node["id"]] = entry
                entry.Clear()
                entry.id = node["id"]
                entry.tab = cls["tab_id"]
                entry.row = tier
                entry.column = path_index
                entry.position_x, entry.position_y = polar(radius, angle + offset)
                entry.required_points = required
                entry.node_scale = scale
                entry.placeholder = node["placeholder"]
                entry.accent_color = color
                if parent:
                    prerequisite = entry.prerequisites.add()
                    prerequisite.talent_id = slots[parent]["id"]
                    prerequisite.rank = len(slots[parent]["ranks"])

                ranks = node["ranks"]
                for rank_index, rank in enumerate(ranks):
                    spell = apply_spell(spells_by_id, spells, rank, cls["classmask"])
                    if len(ranks) == 1:
                        spell.rank, spell.baseid = 0, 0
                    else:
                        spell.rank, spell.baseid = rank_index + 1, ranks[0].id
                    spell.prevspell = ranks[rank_index - 1].id if rank_index > 0 else 0
                    spell.nextspell = ranks[rank_index + 1].id if rank_index + 1 < len(ranks) else 0
                    entry.ranks.append(rank.id)
                    used_rank_spells.add(rank.id)
                for extra in node["extra"]:
                    apply_spell(spells_by_id, spells, extra, 0)

    for spell_id, flags in FAMILY_FLAGS.items():
        _require(spell_id in spells_by_id, f"family flag target {spell_id} missing")
        spells_by_id[spell_id].familyflags = flags

    # Neuter rank spells no talent uses any more (see module docstring).
    neutered = []
    candidates = set(RETIRED_RANK_SPELLS)
    for talent_id, ranks in old_ranks.items():
        if talents_by_id[talent_id].tab in class_tabs or talent_id in removed:
            candidates.update(ranks)
    _require(not (RETIRED_RANK_SPELLS & used_rank_spells), "a retired rank spell is used again")
    for spell_id in sorted(candidates):
        if spell_id in used_rank_spells or spell_id not in spells_by_id:
            continue
        spell = spells_by_id[spell_id]
        if spell.effects or spell.description:
            del spell.effects[:]
            spell.procflags = 0
            spell.procchance = 0
            # Texts would read the removed effects ($s0); the spell is never shown anyway.
            spell.description = ""
            spell.auratext = ""
            spell.ClearField("description_loc")
            spell.ClearField("auratext_loc")
            neutered.append(spell_id)

    for talent_id in removed:
        index = next(i for i, entry in enumerate(talents.entry) if entry.id == talent_id)
        del talents.entry[index]

    return {"removed": removed, "neutered": sorted(neutered), "labels": labels}


def validate(spells, talents, tabs):
    import spell_text_data as std  # tools/spell_text

    by_id = {spell.id: spell for spell in spells.entry}
    _require(len(by_id) == len(spells.entry), "duplicate spell ids")
    talent_ids = [entry.id for entry in talents.entry]
    _require(len(talent_ids) == len(set(talent_ids)), "duplicate talent ids")
    talents_by_id = {entry.id: entry for entry in talents.entry}

    problems = []
    for cls in CLASSES:
        names = set()
        icons = {}
        for _, _, _, slots in cls["paths"]:
            for node in slots.values():
                icon_path = by_id[node["ranks"][0].id].icon
                icons.setdefault(icon_path, []).append(by_id[node["ranks"][0].id].name)
        for icon_path, owners in icons.items():
            if len(owners) > 1:
                problems.append(f"class {cls['class_id']}: {icon_path} used by {owners}")
        for draft, node in all_spell_drafts(cls):
            spell = by_id[draft.id]
            icon = ROOT / "data/client" / spell.icon
            if not icon.is_file():
                problems.append(f"spell {spell.id} {spell.name}: missing icon {spell.icon}")
            for label, text in std.spell_texts(spell):
                for problem in std.placeholder_problems(text, spell, by_id):
                    problems.append(f"spell {spell.id} {spell.name} {label}: {problem}")
            if draft in node["ranks"] and node["ranks"].index(draft) == 0:
                if spell.name in names:
                    problems.append(f"{spell.name} appears twice in class {cls['class_id']}")
                names.add(spell.name)
            if not node["placeholder"] and draft in node["ranks"] and not spell.effects:
                problems.append(f"real talent spell {spell.id} {spell.name} has no effects")
            for effect in spell.effects:
                if effect.aura == PROC_TRIGGER and effect.triggerspell not in by_id:
                    problems.append(f"spell {spell.id} triggers missing spell {effect.triggerspell}")

    for entry in talents.entry:
        for prerequisite in entry.prerequisites:
            parent = talents_by_id.get(prerequisite.talent_id)
            if parent is None or parent.tab != entry.tab:
                problems.append(f"talent {entry.id}: bad prerequisite {prerequisite.talent_id}")
            elif parent.placeholder and not entry.placeholder:
                problems.append(f"talent {entry.id}: real talent behind placeholder {parent.id}")
        for spell_id in entry.ranks:
            if spell_id not in by_id:
                problems.append(f"talent {entry.id}: rank spell {spell_id} missing")

    _require(spells.IsInitialized() and talents.IsInitialized() and tabs.IsInitialized(),
             "a dataset is missing required fields")
    _require(not problems, "\n".join(problems))


def summary(spells):
    by_id = {spell.id: spell for spell in spells.entry}
    lines = []
    for cls in CLASSES:
        real = sum(1 for _, _, _, s in cls["paths"] for n in s.values() if not n["placeholder"])
        lines.append(f"\n## {cls['icon'][8:-5]} ({real} real / {27 - real} placeholder)")
        for label_key, texts, _, slots in cls["paths"]:
            lines.append(f"\n**{texts[1]}**")
            for slot in SLOTS:
                node = slots[slot]
                spell = by_id[node["ranks"][0].id]
                kind = "placeholder" if node["placeholder"] else f"{len(node['ranks'])} rank(s)"
                text = spell.description.replace("\n", " ")
                lines.append(f"- `{slot}` **{spell.name}** ({kind}): {text}")
    return "\n".join(lines)


def localization_lines(labels):
    """Writes the label and frame strings into every locale's Localization.txt."""
    locales = {"Locale_deDE": 0, "Locale_enUS": 1, "Locale_frFR": 2, "Locale_ruRU": 3}
    for folder, index in locales.items():
        path = ROOT / "data/client/Locales" / folder / "Localization.txt"
        raw = path.read_bytes()
        crlf = b"\r\n" in raw
        text = raw.decode("utf-8").replace("\r\n", "\n")
        lines = text.split("\n")
        anchor = next(i for i, line in enumerate(lines) if '"TALENT_POINTS_LABEL"' in line)
        lines = [line for line in lines if not any(f'"{key}"' in line for key in labels)]
        anchor = next(i for i, line in enumerate(lines) if '"TALENT_POINTS_LABEL"' in line)
        new = [f'\t(key = "{key}", string = "{texts[index]}")' for key, texts in labels.items()]
        lines[anchor + 1:anchor + 1] = new
        text = "\n".join(lines)
        if crlf:
            text = text.replace("\n", "\r\n")
        path.write_bytes(text.encode("utf-8"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--summary", action="store_true")
    args = parser.parse_args()

    sys.path.insert(0, str(ROOT / "tools/spell_text"))
    spells = load_editor("spells")
    talents = load_editor("talents")
    tabs = load_editor("talent_tabs")
    report = build(spells, talents, tabs)
    validate(spells, talents, tabs)

    if args.summary:
        print(summary(spells))
    print(f"removed talents {report['removed']}, neutered rank spells {report['neutered']}")

    if not args.apply:
        print("validated talent trees (dry run)")
        return

    OUT.mkdir(parents=True, exist_ok=True)
    backup = OUT / ("backup_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    backup.mkdir()
    for name in EDITOR_TYPES:
        shutil.copy2(editor_path(name), backup / f"editor_{name}.data")
        shutil.copy2(client_path(name), backup / f"client_{name}.data")

    for name, dataset in (("spells", spells), ("talents", talents), ("talent_tabs", tabs)):
        blob = dataset.SerializeToString()
        editor_path(name).write_bytes(blob)
        client_path(name).write_bytes(blob)
    localization_lines(report["labels"])

    print(f"wrote talent trees to editor + ClientDB + Localization.txt. Backup: {backup}")


if __name__ == "__main__":
    main()
