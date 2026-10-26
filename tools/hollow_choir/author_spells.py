# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the Hollow Choir boss spells (251-265) in the editor dataset and the ClientDB copy.

    py -3 tools/hollow_choir/author_spells.py            # validate only
    py -3 tools/hollow_choir/author_spells.py --apply    # write both spells.data files

Idempotent: spells are written by id, replacing what is there, except for visualization_id, which
tools/hollow_choir/author_visuals.py owns. Design: docs/hollow_choir_bosses.md.

Each spell can be tested on its own with `learnspell <id>` and a cast at a hostile NPC. The zone
spells (Guttering Candle, Silent Place, Dissonance) then mark the ground under that NPC.

Rules that are easy to break by accident:

* Zone ticks and detonations (254, 259, 261) are cast by the boss on players it may not see:
  the zone lies where the player *was*, often behind a pillar. They ignore line of sight.
* The zone spells themselves (253, 258, 260) also ignore line of sight, so the encounter can
  aim them at any player in the room.
* Kicks stop every cast unless the spell opts out. Grave Strike and the summons are answered by
  positioning, not by an interrupt, so they carry CannotBeInterrupted; Lament and Dirge of the
  Grave are the interruptible ones.
* Dirge of the Grave is channeled: its channel length is `casttime`, its caster aura lasts
  `duration`, and the periodic trigger on that aura deals the damage. The two must match.
* Every player-visible string needs its TRANSLATIONS entry, like every other spell in the
  dataset: a spell without them shows English in a German, French or Russian client.
"""

import argparse
import shutil
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-npc-designer/scripts"))
from proto_runtime import load_modules  # noqa: E402

EDITOR_SPELLS = ROOT / "data/editor/data/spells.data"
CLIENT_SPELLS = ROOT / "data/client/ClientDB/spells.data"
ICONS = ROOT / "data/client"

# LocaleIndex values (src/shared/base/localization.h). enUS is the base field itself and is
# never stored as an override; enGB repeats the English text, as it does for every other spell.
LOCALE_INDEX = {"deDE": 1, "enGB": 2, "frFR": 3, "ruRU": 4}
TRANSLATED_LOCALES = ("deDE", "frFR", "ruRU")

# spell_effects (src/shared/game/spell.h)
SCHOOL_DAMAGE, APPLY_AURA, PERSISTENT_AREA_AURA, SUMMON = 2, 6, 24, 25
# spell_effect_targets (src/shared/game/spell_target_map.h)
CASTER, TARGET_ENEMY, SOURCE_AREA_ENEMY, TARGET_ALLY, CONE_ENEMY = 0, 5, 9, 14, 16
# aura_type (src/shared/game/aura.h)
PERIODIC_TRIGGER_SPELL, MOD_DECREASE_SPEED, MOD_DAMAGE_DONE_PCT, MOD_DAMAGE_TAKEN_PCT = 9, 15, 23, 24
# spell schools
PHYSICAL, FIRE, SHADOW = 0, 2, 5
# attributes
CHANNELED = 0x00000001
CAN_TARGET_DEAD = 0x00000400
CASTABLE_WHILE_DEAD = 0x00800000
NEGATIVE = 0x04000000
IGNORE_LOS = 1 << 7
CANNOT_BE_INTERRUPTED = 1 << 11
# spell_interrupt_flags
INTERRUPT = 0x04
# ranges.data
RANGE_SELF, RANGE_INFINITE, RANGE_35, RANGE_MELEE = 0, 1, 2, 6

# creatures
RISEN_NOVICE, MOURNING_CHORISTER, HOLLOW_CHORISTER = 89, 90, 91


def effect(type_, target=CASTER, **fields):
    return dict(type=type_, targeta=target, **fields)


def spell(id_, name, level, school, description, icon, effects,
          cast_ms=0, duration=0, attributes=(0, 0), interrupt=0, range_type=RANGE_SELF,
          aura_text=None, stack_amount=None, facing=0):
    return dict(id=id_, name=name, level=level, school=school, description=description,
                icon=icon, effects=effects, cast_ms=cast_ms,
                duration=duration, attributes=attributes, interrupt=interrupt,
                range_type=range_type, aura_text=aura_text, stack_amount=stack_amount, facing=facing)


SPELLS = [
    # --- Brother Oswin -------------------------------------------------------------------------
    spell(251, "Grave Strike", 12, PHYSICAL,
          "Brings the broken censer down in a crushing arc, dealing $s0 physical damage to "
          "everyone in front of the caster.",
          "Interface/Icons/Spells/T_Icon_BloodCombat_12.htex",
          [effect(SCHOOL_DAMAGE, CONE_ENEMY, basepoints=104, diesides=22, radius=8.0, miscvalueb=100)],
          cast_ms=1500, attributes=(NEGATIVE, CANNOT_BE_INTERRUPTED), range_type=RANGE_MELEE, facing=1),
    spell(252, "Last Vigil", 12, SHADOW,
          "Calls two novices up from their biers to keep the vigil with the caster.",
          "Interface/Icons/Spells/T_Icon_Unholy_40.htex",
          [effect(SUMMON, CASTER, basepoints=2, summonunit=RISEN_NOVICE, radius=6.0)],
          cast_ms=2000, attributes=(0, CANNOT_BE_INTERRUPTED)),
    spell(253, "Guttering Candle", 12, FIRE,
          "Sets a guttering candle flame on the ground beneath the target. After 2 sec it flares, "
          "dealing $254s0 fire damage to everyone still standing in it.",
          "Interface/Icons/Spells/T_Icon_Fire_40.htex",
          [effect(PERSISTENT_AREA_AURA, TARGET_ENEMY, radius=3.5, amplitude=2000, triggerspell=254)],
          duration=2000, attributes=(NEGATIVE, IGNORE_LOS), range_type=RANGE_35),
    spell(254, "Guttering Candle", 12, FIRE,
          "The candle flares, dealing $s0 fire damage.",
          "Interface/Icons/Spells/T_Icon_Fire_40.htex",
          [effect(SCHOOL_DAMAGE, TARGET_ENEMY, basepoints=78, diesides=18)],
          attributes=(NEGATIVE, IGNORE_LOS), range_type=RANGE_INFINITE),

    # --- Sister Mereth -------------------------------------------------------------------------
    spell(255, "Lament", 13, SHADOW,
          "A lament for the unburied, dealing $s0 shadow damage to every enemy within 40 yards.",
          "Interface/Icons/Spells/T_Icon_Shadow_30.htex",
          [effect(SCHOOL_DAMAGE, SOURCE_AREA_ENEMY, basepoints=48, diesides=12, radius=40.0)],
          cast_ms=3000, attributes=(NEGATIVE, IGNORE_LOS), interrupt=INTERRUPT),
    spell(256, "Mourning Voices", 13, SHADOW,
          "Calls two Mourning Choristers to opposite sides of the hall.",
          "Interface/Icons/Spells/T_Icon_Shadow_45.htex",
          # Small enough that the choristers land inside the cloister whichever way Mereth faces.
          [effect(SUMMON, CASTER, basepoints=2, summonunit=MOURNING_CHORISTER, radius=5.5)],
          cast_ms=2000, attributes=(0, CANNOT_BE_INTERRUPTED)),
    spell(257, "Mourning Chorus", 13, SHADOW,
          "The choristers' grief shrouds the caster, reducing damage taken by $s0%.",
          "Interface/Icons/Spells/T_Icon_Shadow_45.htex",
          [effect(APPLY_AURA, CASTER, aura=MOD_DAMAGE_TAKEN_PCT, basepoints=-40)],
          aura_text="Damage taken reduced by 40%."),
    spell(258, "Silent Place", 13, SHADOW,
          "Hushes the ground beneath the target for 12 sec. Anyone standing in the silence takes "
          "$259s0 shadow damage every second and is slowed by 50%.",
          "Interface/Icons/Spells/T_Icon_Shadow_60.htex",
          [effect(PERSISTENT_AREA_AURA, TARGET_ENEMY, radius=4.5, amplitude=1000, triggerspell=259)],
          duration=12000, attributes=(NEGATIVE, IGNORE_LOS), range_type=RANGE_35),
    spell(259, "Silent Place", 13, SHADOW,
          "The silence deals $s0 shadow damage and slows movement by 50%.",
          "Interface/Icons/Spells/T_Icon_Shadow_60.htex",
          [effect(SCHOOL_DAMAGE, TARGET_ENEMY, basepoints=14, diesides=6),
           effect(APPLY_AURA, TARGET_ENEMY, aura=MOD_DECREASE_SPEED, basepoints=-50)],
          duration=1500, attributes=(NEGATIVE, IGNORE_LOS), range_type=RANGE_INFINITE,
          aura_text="Movement slowed by 50%."),

    # --- Cantor Veyr ---------------------------------------------------------------------------
    spell(260, "Dissonance", 14, SHADOW,
          "Strikes a discordant note beneath the target. After 2 sec a wave of sound bursts from "
          "it, dealing $261s0 shadow damage to everyone still standing in it.",
          "Interface/Icons/Spells/T_Icon_Arcane_20.htex",
          [effect(PERSISTENT_AREA_AURA, TARGET_ENEMY, radius=4.0, amplitude=2000, triggerspell=261)],
          duration=2000, attributes=(NEGATIVE, IGNORE_LOS), range_type=RANGE_35),
    spell(261, "Dissonance", 14, SHADOW,
          "A wave of discordant sound deals $s0 shadow damage.",
          "Interface/Icons/Spells/T_Icon_Arcane_20.htex",
          [effect(SCHOOL_DAMAGE, TARGET_ENEMY, basepoints=88, diesides=20)],
          attributes=(NEGATIVE, IGNORE_LOS), range_type=RANGE_INFINITE),
    spell(262, "Dirge of the Grave", 14, SHADOW,
          "Conducts the hollow choir for 5 sec, dealing $263s0 shadow damage to every enemy "
          "within 40 yards each second.",
          "Interface/Icons/Spells/T_Icon_Unholy_80.htex",
          [effect(APPLY_AURA, CASTER, aura=PERIODIC_TRIGGER_SPELL, amplitude=1000, triggerspell=263, targetb=CASTER)],
          cast_ms=5000, duration=5000, attributes=(CHANNELED | NEGATIVE, 0), interrupt=INTERRUPT),
    spell(263, "Dirge of the Grave", 14, SHADOW,
          "The dirge deals $s0 shadow damage.",
          "Interface/Icons/Spells/T_Icon_Unholy_80.htex",
          [effect(SCHOOL_DAMAGE, SOURCE_AREA_ENEMY, basepoints=16, diesides=6, radius=40.0)],
          attributes=(NEGATIVE, IGNORE_LOS)),
    spell(264, "The Choir Rises", 14, SHADOW,
          "Raises two Hollow Choristers from the dark of the apse.",
          "Interface/Icons/Spells/T_Icon_Unholy_100.htex",
          # Small enough that the choristers land inside the round apse whichever way Veyr faces.
          [effect(SUMMON, CASTER, basepoints=2, summonunit=HOLLOW_CHORISTER, radius=7.0)],
          cast_ms=2000, attributes=(0, CANNOT_BE_INTERRUPTED)),
    # One instance per chorister: each living chorister keeps re-applying its own copy on Veyr
    # (auras of one spell from different casters coexist, the same caster refreshes its own).
    # When a chorister dies its copy runs out within 6 s, so the buff tracks the living choir
    # without any trigger having to remove a stack.
    spell(265, "Choral Resonance", 14, SHADOW,
          "A living chorister swells the target's voice, increasing damage done by $s0% for $D. "
          "Each chorister sustains its own resonance.",
          "Interface/Icons/Spells/T_Icon_Unholy_100.htex",
          [effect(APPLY_AURA, TARGET_ALLY, aura=MOD_DAMAGE_DONE_PCT, basepoints=12)],
          duration=6000, aura_text="Damage done increased by 12%.", range_type=RANGE_INFINITE),

    # --- Trash ---------------------------------------------------------------------------------
    # Each trash ability rehearses a boss mechanic at lower stakes.
    spell(266, "Warden's Cleave", 11, PHYSICAL,
          "Heaves a grave-spade in a wide arc, dealing $s0 physical damage to everyone in front "
          "of the caster.",
          "Interface/Icons/Spells/T_Icon_BloodCombat_12.htex",
          [effect(SCHOOL_DAMAGE, CONE_ENEMY, basepoints=55, diesides=13, radius=8.0, miscvalueb=100)],
          cast_ms=2000, attributes=(NEGATIVE, CANNOT_BE_INTERRUPTED), range_type=RANGE_MELEE, facing=1),
    spell(267, "Mournful Dirge", 11, SHADOW,
          "A dirge for the unburied, dealing $s0 shadow damage to every enemy within 30 yards.",
          "Interface/Icons/Spells/T_Icon_Shadow_30.htex",
          [effect(SCHOOL_DAMAGE, SOURCE_AREA_ENEMY, basepoints=26, diesides=8, radius=30.0)],
          cast_ms=2500, attributes=(NEGATIVE, IGNORE_LOS), interrupt=INTERRUPT),
    # Cast by a Candlebearer's OnKilled trigger, so by a dead caster on its own corpse.
    spell(268, "Spilled Wax", 11, FIRE,
          "The fallen candle spills burning wax, dealing $269s0 fire damage every second to "
          "anyone standing in it for 6 sec.",
          "Interface/Icons/Spells/T_Icon_Fire_40.htex",
          [effect(PERSISTENT_AREA_AURA, CASTER, radius=2.2, amplitude=1000, triggerspell=269)],
          duration=6000, attributes=(CASTABLE_WHILE_DEAD | CAN_TARGET_DEAD, IGNORE_LOS)),
    spell(269, "Spilled Wax", 11, FIRE,
          "Burning wax deals $s0 fire damage.",
          "Interface/Icons/Spells/T_Icon_Fire_40.htex",
          [effect(SCHOOL_DAMAGE, TARGET_ENEMY, basepoints=9, diesides=4)],
          attributes=(NEGATIVE, IGNORE_LOS), range_type=RANGE_INFINITE),
]

# Player-visible text per locale, keyed by spell id: (name, description) and a third entry for
# the spells that carry an aura text. Damage and duration placeholders ($s0, $254s0, $D, ...)
# are resolved by the client and have to survive translation unchanged.
TRANSLATIONS = {
    251: {
        "deDE": ("Grabesschlag",
                 "Lässt das zerbrochene Weihrauchfass in einem zermalmenden Bogen niederfahren und "
                 "verursacht $s0 physischen Schaden bei allen vor dem Zauberer."),
        "frFR": ("Frappe sépulcrale",
                 "Abat l'encensoir brisé en un arc écrasant, infligeant $s0 points de dégâts "
                 "physiques à tous ceux qui se trouvent devant l'incantateur."),
        "ruRU": ("Могильный удар",
                 "Обрушивает сломанное кадило сокрушительной дугой, нанося $s0 ед. физического "
                 "урона всем перед заклинателем."),
    },
    252: {
        "deDE": ("Letzte Totenwache",
                 "Ruft zwei Novizen von ihren Bahren, um mit dem Zauberer die Totenwache zu halten."),
        "frFR": ("Dernière veillée",
                 "Appelle deux novices hors de leurs civières pour veiller aux côtés de l'incantateur."),
        "ruRU": ("Последнее бдение",
                 "Поднимает с катафалков двух послушников, чтобы они бдели вместе с заклинателем."),
    },
    253: {
        "deDE": ("Flackernde Kerze",
                 "Entzündet eine flackernde Kerzenflamme am Boden unter dem Ziel. Nach 2 Sek. lodert "
                 "sie auf und verursacht $254s0 Feuerschaden bei allen, die noch darin stehen."),
        "frFR": ("Bougie vacillante",
                 "Dépose une flamme de bougie vacillante sur le sol sous la cible. Après 2 s, elle "
                 "s'embrase et inflige $254s0 points de dégâts de feu à tous ceux qui s'y trouvent encore."),
        "ruRU": ("Чадящая свеча",
                 "Зажигает чадящее пламя свечи на земле под целью. Через 2 сек. оно вспыхивает, нанося "
                 "$254s0 ед. урона от огня всем, кто ещё стоит в нём."),
    },
    254: {
        "deDE": ("Flackernde Kerze",
                 "Die Kerze lodert auf und verursacht $s0 Feuerschaden."),
        "frFR": ("Bougie vacillante",
                 "La bougie s'embrase et inflige $s0 points de dégâts de feu."),
        "ruRU": ("Чадящая свеча",
                 "Свеча вспыхивает, нанося $s0 ед. урона от огня."),
    },
    255: {
        "deDE": ("Klagelied",
                 "Ein Klagelied für die Unbestatteten, das allen Gegnern im Umkreis von 40 Metern "
                 "$s0 Schattenschaden verursacht."),
        "frFR": ("Complainte",
                 "Une complainte pour les sans-sépulture, infligeant $s0 points de dégâts d'ombre à "
                 "tous les ennemis dans un rayon de 40 mètres."),
        "ruRU": ("Плач",
                 "Плач по непогребённым, наносящий $s0 ед. урона от тьмы всем врагам в радиусе "
                 "40 метров."),
    },
    256: {
        "deDE": ("Trauernde Stimmen",
                 "Ruft zwei Trauernde Choristen an gegenüberliegende Seiten der Halle."),
        "frFR": ("Voix endeuillées",
                 "Appelle deux choristes endeuillés aux côtés opposés de la salle."),
        "ruRU": ("Скорбные голоса",
                 "Призывает двух скорбящих хористов к противоположным сторонам зала."),
    },
    257: {
        "deDE": ("Trauerchor",
                 "Die Trauer der Choristen hüllt den Zauberer ein und verringert den erlittenen "
                 "Schaden um $s0 %.",
                 "Erlittener Schaden um 40 % verringert."),
        "frFR": ("Chœur de deuil",
                 "Le chagrin des choristes enveloppe l'incantateur, réduisant les dégâts subis de $s0%.",
                 "Dégâts subis réduits de 40%."),
        "ruRU": ("Скорбный хор",
                 "Горе хористов окутывает заклинателя, снижая получаемый урон на $s0%.",
                 "Получаемый урон снижен на 40%."),
    },
    258: {
        "deDE": ("Stiller Ort",
                 "Bringt den Boden unter dem Ziel für 12 Sek. zum Schweigen. Wer in der Stille steht, "
                 "erleidet jede Sekunde $259s0 Schattenschaden und wird um 50 % verlangsamt."),
        "frFR": ("Lieu silencieux",
                 "Fait taire le sol sous la cible pendant 12 s. Quiconque se tient dans le silence "
                 "subit $259s0 points de dégâts d'ombre par seconde et est ralenti de 50%."),
        "ruRU": ("Безмолвное место",
                 "Заставляет землю под целью умолкнуть на 12 сек. Все, кто стоит в тишине, получают "
                 "$259s0 ед. урона от тьмы каждую секунду и замедляются на 50%."),
    },
    259: {
        "deDE": ("Stiller Ort",
                 "Die Stille verursacht $s0 Schattenschaden und verlangsamt die Bewegung um 50 %.",
                 "Bewegung um 50 % verlangsamt."),
        "frFR": ("Lieu silencieux",
                 "Le silence inflige $s0 points de dégâts d'ombre et réduit la vitesse de déplacement "
                 "de 50%.",
                 "Déplacement ralenti de 50%."),
        "ruRU": ("Безмолвное место",
                 "Тишина наносит $s0 ед. урона от тьмы и замедляет передвижение на 50%.",
                 "Передвижение замедлено на 50%."),
    },
    260: {
        "deDE": ("Dissonanz",
                 "Schlägt einen misstönenden Ton unter dem Ziel an. Nach 2 Sek. bricht eine Schallwelle "
                 "daraus hervor und verursacht $261s0 Schattenschaden bei allen, die noch darin stehen."),
        "frFR": ("Dissonance",
                 "Frappe une note discordante sous la cible. Après 2 s, une onde sonore en jaillit, "
                 "infligeant $261s0 points de dégâts d'ombre à tous ceux qui s'y trouvent encore."),
        "ruRU": ("Диссонанс",
                 "Берёт нестройную ноту под целью. Через 2 сек. из неё вырывается звуковая волна, "
                 "нанося $261s0 ед. урона от тьмы всем, кто ещё стоит в ней."),
    },
    261: {
        "deDE": ("Dissonanz",
                 "Eine Welle misstönenden Klangs verursacht $s0 Schattenschaden."),
        "frFR": ("Dissonance",
                 "Une onde sonore discordante inflige $s0 points de dégâts d'ombre."),
        "ruRU": ("Диссонанс",
                 "Волна нестройного звука наносит $s0 ед. урона от тьмы."),
    },
    262: {
        "deDE": ("Grabesklage",
                 "Dirigiert 5 Sek. lang den Hohlen Chor und verursacht jede Sekunde $263s0 "
                 "Schattenschaden bei allen Gegnern im Umkreis von 40 Metern."),
        "frFR": ("Complainte de la tombe",
                 "Dirige le chœur creux pendant 5 s, infligeant chaque seconde $263s0 points de dégâts "
                 "d'ombre à tous les ennemis dans un rayon de 40 mètres."),
        "ruRU": ("Плач могилы",
                 "Дирижирует Пустым хором 5 сек., нанося каждую секунду $263s0 ед. урона от тьмы всем "
                 "врагам в радиусе 40 метров."),
    },
    263: {
        "deDE": ("Grabesklage",
                 "Die Klage verursacht $s0 Schattenschaden."),
        "frFR": ("Complainte de la tombe",
                 "La complainte inflige $s0 points de dégâts d'ombre."),
        "ruRU": ("Плач могилы",
                 "Плач наносит $s0 ед. урона от тьмы."),
    },
    264: {
        "deDE": ("Der Chor erhebt sich",
                 "Erhebt zwei Hohle Choristen aus dem Dunkel der Apsis."),
        "frFR": ("Le chœur se lève",
                 "Relève deux choristes creux des ténèbres de l'abside."),
        "ruRU": ("Хор восстаёт",
                 "Поднимает двух пустых хористов из тьмы апсиды."),
    },
    265: {
        "deDE": ("Chorresonanz",
                 "Ein lebender Chorist verstärkt die Stimme des Ziels und erhöht den verursachten "
                 "Schaden für $D um $s0 %. Jeder Chorist erhält seine eigene Resonanz aufrecht.",
                 "Verursachter Schaden um 12 % erhöht."),
        "frFR": ("Résonance chorale",
                 "Un choriste vivant enfle la voix de la cible, augmentant les dégâts infligés de $s0% "
                 "pendant $D. Chaque choriste entretient sa propre résonance.",
                 "Dégâts infligés augmentés de 12%."),
        "ruRU": ("Хоровой резонанс",
                 "Живой хорист усиливает голос цели, увеличивая наносимый урон на $s0% на $D. Каждый "
                 "хорист поддерживает собственный резонанс.",
                 "Наносимый урон увеличен на 12%."),
    },
    266: {
        "deDE": ("Spaltschlag des Wärters",
                 "Schwingt einen Grabspaten in weitem Bogen und verursacht $s0 physischen Schaden bei "
                 "allen vor dem Zauberer."),
        "frFR": ("Taillade du gardien",
                 "Fait tournoyer une bêche de fossoyeur en un large arc, infligeant $s0 points de "
                 "dégâts physiques à tous ceux qui se trouvent devant l'incantateur."),
        "ruRU": ("Рассекающий удар стража",
                 "Размашисто бьёт могильной лопатой, нанося $s0 ед. физического урона всем перед "
                 "заклинателем."),
    },
    267: {
        "deDE": ("Trauriges Klagelied",
                 "Ein Klagelied für die Unbestatteten, das allen Gegnern im Umkreis von 30 Metern "
                 "$s0 Schattenschaden verursacht."),
        "frFR": ("Complainte funèbre",
                 "Une complainte pour les sans-sépulture, infligeant $s0 points de dégâts d'ombre à "
                 "tous les ennemis dans un rayon de 30 mètres."),
        "ruRU": ("Скорбный плач",
                 "Плач по непогребённым, наносящий $s0 ед. урона от тьмы всем врагам в радиусе "
                 "30 метров."),
    },
    268: {
        "deDE": ("Verschüttetes Wachs",
                 "Die gefallene Kerze verschüttet brennendes Wachs und verursacht 6 Sek. lang jede "
                 "Sekunde $269s0 Feuerschaden bei allen, die darin stehen."),
        "frFR": ("Cire renversée",
                 "La bougie tombée répand de la cire brûlante, infligeant $269s0 points de dégâts de "
                 "feu par seconde pendant 6 s à quiconque s'y tient."),
        "ruRU": ("Разлитый воск",
                 "Упавшая свеча разливает горящий воск, нанося $269s0 ед. урона от огня каждую секунду "
                 "в течение 6 сек. всем, кто стоит в нём."),
    },
    269: {
        "deDE": ("Verschüttetes Wachs",
                 "Brennendes Wachs verursacht $s0 Feuerschaden."),
        "frFR": ("Cire renversée",
                 "La cire brûlante inflige $s0 points de dégâts de feu."),
        "ruRU": ("Разлитый воск",
                 "Горящий воск наносит $s0 ед. урона от огня."),
    },
}


def add_loc(loc_array, locale, value):
    override = loc_array.add()
    override.locale = LOCALE_INDEX[locale]
    override.value = value


def localized(spec, locale):
    """Returns (name, description, aura text) for a locale; enGB repeats the English base."""
    if locale == "enGB":
        return spec["name"], spec["description"], spec["aura_text"]

    texts = TRANSLATIONS[spec["id"]][locale]
    return texts[0], texts[1], texts[2] if len(texts) > 2 else None


def build(spells_pb, spec):
    entry = spells_pb.SpellEntry()
    entry.id = spec["id"]
    entry.name = spec["name"]
    entry.attributes.extend(spec["attributes"])
    for index, fields in enumerate(spec["effects"]):
        eff = entry.effects.add()
        eff.index = index
        for key, value in fields.items():
            setattr(eff, key, value)
    entry.cooldown = 0
    entry.casttime = spec["cast_ms"]
    entry.cost = 0
    # spelllevel gates who may cast the spell (ValidateCasterRequirements), so it stays at 1:
    # these are boss spells, but a GM tests them on a level 10 character with learnspell. No
    # effect scales per level, so the level fields do not change any number.
    entry.maxlevel = spec["level"]
    entry.baselevel = entry.spelllevel = 1
    entry.spellSchool = spec["school"]
    # Only Grave Strike is aimed: the ground spells mark wherever their target stands, and the
    # rest centre on the caster. Facing 1 would demand the target in front for all of them.
    entry.facing = spec["facing"]
    entry.duration = spec["duration"]
    entry.interruptflags = spec["interrupt"]
    entry.rangetype = spec["range_type"]
    entry.classmask = 0
    entry.rank = 0
    entry.description = spec["description"]
    entry.icon = spec["icon"]
    if spec["aura_text"]:
        entry.auratext = spec["aura_text"]
    if spec["stack_amount"]:
        entry.stackamount = spec["stack_amount"]
    # Same as every other creature spell in this project: no global cooldown, no school lockout.
    entry.cooldownflags = 3
    # Overrides in ascending LocaleIndex order, the layout the rest of spells.data uses.
    for locale in sorted(LOCALE_INDEX, key=LOCALE_INDEX.get):
        name, description, aura_text = localized(spec, locale)
        add_loc(entry.name_loc, locale, name)
        add_loc(entry.description_loc, locale, description)
        if spec["aura_text"]:
            add_loc(entry.auratext_loc, locale, aura_text)
    return entry


def validate(spells, units):
    by_id = {s.id: s for s in spells.entry}
    for spec in SPELLS:
        icon = ICONS / spec["icon"]
        if not icon.is_file():
            sys.exit(f"spell {spec['id']}: icon {spec['icon']} does not exist")
        for fields in spec["effects"]:
            trigger = fields.get("triggerspell")
            if trigger and trigger not in by_id:
                sys.exit(f"spell {spec['id']}: trigger spell {trigger} does not exist")
            summon = fields.get("summonunit")
            if summon and summon not in units:
                sys.exit(f"spell {spec['id']}: summoned unit {summon} does not exist")
            if fields["type"] == PERSISTENT_AREA_AURA:
                if spec["duration"] <= 0 or fields.get("radius", 0) <= 0:
                    sys.exit(f"spell {spec['id']}: a zone needs a duration and a radius")
        if spec["attributes"][0] & CHANNELED and spec["cast_ms"] != spec["duration"]:
            sys.exit(f"spell {spec['id']}: channel length and aura duration differ")
        # A missing translation is invisible in the editor and shows English to the player.
        for locale in TRANSLATED_LOCALES:
            texts = TRANSLATIONS.get(spec["id"], {}).get(locale)
            if not texts or len(texts) < 2 or not all(texts[:2]):
                sys.exit(f"spell {spec['id']}: no {locale} name and description")
            if bool(spec["aura_text"]) != (len(texts) > 2 and bool(texts[2])):
                sys.exit(f"spell {spec['id']}: {locale} aura text does not match the English one")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    mods = load_modules(ROOT)
    spells = mods["spells"].Spells()
    spells.ParseFromString(EDITOR_SPELLS.read_bytes())
    units = mods["units"].Units()
    units.ParseFromString((ROOT / "data/editor/data/units.data").read_bytes())

    if EDITOR_SPELLS.read_bytes() != CLIENT_SPELLS.read_bytes():
        sys.exit("editor and ClientDB spells.data differ; reconcile them before authoring")

    for spec in SPELLS:
        entry = build(mods["spells"], spec)
        for i, existing in enumerate(spells.entry):
            if existing.id == entry.id:
                # The visualization link belongs to author_visuals.py; keep whatever it set.
                if existing.HasField("visualization_id"):
                    entry.visualization_id = existing.visualization_id
                spells.entry[i].CopyFrom(entry)
                break
        else:
            spells.entry.append(entry)
        print(f"spell {spec['id']} {spec['name']}")

    validate(spells, {u.id for u in units.entry})

    if not args.apply:
        print("validated; pass --apply to write")
        return

    backup = ROOT / "generated/hollow_choir/backup" / datetime.now().strftime("%Y%m%d%H%M%S")
    backup.mkdir(parents=True, exist_ok=True)
    data = spells.SerializeToString()
    for path in (EDITOR_SPELLS, CLIENT_SPELLS):
        shutil.copy2(path, backup / f"{path.parent.name}_{path.name}")
        path.write_bytes(data)
        print(f"wrote {path.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
