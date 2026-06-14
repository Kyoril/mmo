#!/usr/bin/env python3
"""Shared helpers for inspecting and mutating MMO spell catalog data."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Iterable

from google.protobuf import json_format

from proto_runtime import find_project_root, load_modules


SPELL_EFFECT_NAMES = {
    0: "None",
    1: "InstantKill",
    2: "SchoolDamage",
    3: "Dummy",
    4: "PortalTeleport",
    5: "TeleportUnits",
    6: "ApplyAura",
    7: "EnvironmentalDamage",
    8: "PowerDrain",
    9: "HealthLeech",
    10: "Heal",
    11: "Bind",
    12: "Portal",
    13: "QuestComplete",
    14: "WeaponDamageNoSchool",
    15: "Resurrect",
    16: "AddExtraAttacks",
    17: "Dodge",
    18: "Evade",
    19: "Parry",
    20: "Block",
    21: "CreateItem",
    22: "Weapon",
    23: "Defense",
    24: "PersistentAreaAura",
    25: "Summon",
    26: "Leap",
    27: "Energize",
    28: "WeaponPercentDamage",
    29: "TriggerMissile",
    30: "OpenLock",
    31: "LearnSpell",
    32: "SpellDefense",
    33: "Dispel",
    34: "Language",
    35: "DualWield",
    36: "TeleportUnitsFaceCaster",
    37: "SkillStep",
    38: "Spawn",
    39: "TradeSkill",
    40: "Stealth",
    41: "Detect",
    42: "TameCreature",
    43: "SummonPet",
    44: "LearnPetSpell",
    45: "WeaponDamage",
    46: "ResetAttributePoints",
    47: "HealPct",
    48: "Charge",
    49: "ApplyAreaAura",
    50: "InterruptSpellCast",
    51: "ResetTalents",
    52: "Proficiency",
    53: "TriggerSpell",
    54: "CriticalBlock",
}

AURA_TYPE_NAMES = {
    0: "None",
    1: "Dummy",
    2: "PeriodicHeal",
    3: "ModAttackSpeed",
    4: "ModDamageDone",
    5: "ModDamageTaken",
    6: "ModHealth",
    7: "ModMana",
    8: "ModResistance",
    9: "PeriodicTriggerSpell",
    10: "PeriodicEnergize",
    11: "ModStat",
    12: "ProcTriggerSpell",
    13: "PeriodicDamage",
    14: "ModIncreaseSpeed",
    15: "ModDecreaseSpeed",
    16: "ModSpeedAlways",
    17: "ModSpeedNonStacking",
    18: "AddFlatModifier",
    19: "AddPctModifier",
    20: "ModHealingDone",
    21: "ModAttackPower",
    22: "ModHealingTaken",
    23: "ModDamageDonePct",
    24: "ModDamageTakenPct",
    25: "ModRoot",
    26: "ModSleep",
    27: "ModStun",
    28: "ModFear",
    29: "ModVisibility",
    30: "ModResistancePct",
    31: "ModStatPct",
    32: "ModSpellDamageDone",
    33: "ModSpellDamageDonePct",
    34: "ModDisorient",
    35: "DamageImmunity",
    36: "ModDodgeChance",
}

SPELL_ATTRIBUTE_FLAGS_0 = {
    0x00000001: "Channeled",
    0x00000002: "Ranged",
    0x00000004: "OnNextSwing",
    0x00000008: "OnlyOneStackTotal",
    0x00000010: "Ability",
    0x00000020: "TradeSpell",
    0x00000040: "Passive",
    0x00000080: "HiddenClientSide",
    0x00000100: "HiddenCastTime",
    0x00000200: "TargetMainhandItem",
    0x00000400: "CanTargetDead",
    0x00000800: "StartPeriodicAtApply",
    0x00001000: "DaytimeOnly",
    0x00002000: "NightOnly",
    0x00004000: "IndoorOnly",
    0x00008000: "OutdoorOnly",
    0x00010000: "NotShapeshifted",
    0x00020000: "OnlyStealthed",
    0x00040000: "DontAffectSheathState",
    0x00080000: "LevelDamageCalc",
    0x00100000: "StopAttackTarget",
    0x00200000: "NoDefense",
    0x00400000: "CastTrackTarget",
    0x00800000: "CastableWhileDead",
    0x01000000: "CastableWhileMounted",
    0x02000000: "DisabledWhileActive",
    0x04000000: "Negative",
    0x08000000: "CastableWhileSitting",
    0x10000000: "NotInCombat",
    0x20000000: "IgnoreInvulnerability",
    0x40000000: "BreakableByDamage",
    0x80000000: "CantCancel",
}

SPELL_ATTRIBUTE_FLAGS_1 = {
    1 << 0: "MeleeCombatStart",
    1 << 1: "HiddenAura",
    1 << 2: "Talent",
    1 << 3: "AutoRepeat",
    1 << 4: "UsableWhileStunned",
    1 << 5: "UsableWhileFeared",
    1 << 6: "UsableWhileSleeping",
    1 << 7: "IgnoreLineOfSight",
}

ITEM_TRIGGER_NAMES = {
    0: "OnUse",
    1: "OnEquip",
    2: "OnHitChance",
}

TRAINER_TYPE_NAMES = {
    0: "ClassTrainer",
    1: "MountTrainer",
    2: "SkillTrainer",
    3: "PetTrainer",
}

SPELL_TARGET_NAMES = {
    0: "Caster",
    1: "NearbyEnemy",
    2: "NearbyParty",
    3: "NearbyAlly",
    4: "Pet",
    5: "TargetEnemy",
    6: "SourceArea",
    7: "TargetArea",
    8: "Home",
    9: "SourceAreaEnemy",
    10: "TargetAreaEnemy",
    11: "DatabaseLocation",
    12: "CasterLocation",
    13: "CasterAreaParty",
    14: "TargetAlly",
    15: "ObjectTarget",
    16: "ConeEnemy",
    17: "TargetAny",
    18: "Instigator",
    19: "TargetSecondaryEnemy",
}

SPELL_CONDITION_NAMES = {
    0: "None",
    1: "TargetHasAuraFromCaster",
    2: "TargetMissingAuraFromCaster",
}

SPELL_CONDITION_TARGET_NAMES = {
    0: "PrimaryTarget",
    1: "Caster",
}


def load_catalogs(project_root: str | None = None) -> dict[str, object]:
    root = find_project_root(project_root)
    modules = load_modules(root)
    data_dir = root / "data" / "editor" / "data"

    def parse(filename: str, message):
        instance = message()
        instance.ParseFromString((data_dir / filename).read_bytes())
        return instance

    return {
        "project_root": root,
        "data_dir": data_dir,
        "modules": modules,
        "spells": parse("spells.data", modules["spells"].Spells),
        "items": parse("items.data", modules["items"].Items),
        "classes": parse("classes.data", modules["classes"].Classes),
        "races": parse("races.data", modules["races"].Races),
        "trainers": parse("trainers.data", modules["trainers"].Trainers),
        "talents": parse("talents.data", modules["talents"].Talents),
        "talent_tabs": parse("talent_tabs.data", modules["talent_tabs"].TalentTabs),
        "spell_categories": parse("spell_categories.data", modules["spell_categories"].SpellCategories),
        "spell_visualizations": parse("spell_visualizations.data", modules["spell_visualizations"].SpellVisualizations),
        "proficiencies": parse("proficiencies.data", modules["proficiencies"].Proficiencies),
        "skills": parse("skills.data", modules["skills"].Skills),
        "aura_stacking_categories": parse("aura_stacking_categories.data", modules["aura_stacking_categories"].AuraStackingCategories),
        "item_classes": parse("item_classes.data", modules["item_classes"].ItemClasses),
        "item_subclasses": parse("item_subclasses.data", modules["item_subclasses"].ItemSubclasses),
        "ranges": parse("ranges.data", modules["spells"].Ranges),
    }


def message_to_dict(message) -> dict:
    return json_format.MessageToDict(
        message,
        preserving_proto_field_name=True,
        always_print_fields_with_no_presence=False,
        use_integers_for_enums=True,
    )


def effect_to_summary(effect) -> dict:
    data = message_to_dict(effect)
    data["type_name"] = SPELL_EFFECT_NAMES.get(effect.type, f"Unknown({effect.type})")
    if effect.HasField("targeta"):
        data["targeta_name"] = SPELL_TARGET_NAMES.get(effect.targeta, f"Unknown({effect.targeta})")
    if effect.HasField("targetb"):
        data["targetb_name"] = SPELL_TARGET_NAMES.get(effect.targetb, f"Unknown({effect.targetb})")
    if effect.HasField("aura"):
        data["aura_name"] = AURA_TYPE_NAMES.get(effect.aura, f"Unknown({effect.aura})")
    if effect.HasField("conditiontype"):
        data["conditiontype_name"] = SPELL_CONDITION_NAMES.get(effect.conditiontype, f"Unknown({effect.conditiontype})")
    if effect.HasField("conditiontarget"):
        data["conditiontarget_name"] = SPELL_CONDITION_TARGET_NAMES.get(effect.conditiontarget, f"Unknown({effect.conditiontarget})")
    return data


def decode_attribute_words(words: Iterable[int]) -> list[dict]:
    names_by_index = {
        0: SPELL_ATTRIBUTE_FLAGS_0,
        1: SPELL_ATTRIBUTE_FLAGS_1,
    }
    decoded = []
    for index, value in enumerate(words):
        flags = []
        for bit, name in names_by_index.get(index, {}).items():
            if value & bit:
                flags.append(name)
        decoded.append({"word_index": index, "value": value, "flags": flags})
    return decoded


def make_mask_lookup(entries) -> dict[int, str]:
    return {entry.id: entry.name for entry in entries if entry.id > 0}


def decode_id_mask(mask: int, lookup: dict[int, str]) -> list[dict]:
    result = []
    for entry_id, name in sorted(lookup.items()):
        bit = 1 << (entry_id - 1)
        if mask & bit:
            result.append({"id": entry_id, "name": name, "bit": bit})
    return result


def build_indexes(catalogs: dict[str, object]) -> dict[str, dict]:
    indexes = {
        "spells": {entry.id: entry for entry in catalogs["spells"].entry},
        "items": {entry.id: entry for entry in catalogs["items"].entry},
        "classes": {entry.id: entry for entry in catalogs["classes"].entry},
        "races": {entry.id: entry for entry in catalogs["races"].entry},
        "trainers": {entry.id: entry for entry in catalogs["trainers"].entry},
        "talents": {entry.id: entry for entry in catalogs["talents"].entry},
        "talent_tabs": {entry.id: entry for entry in catalogs["talent_tabs"].entry},
        "categories": {entry.id: entry for entry in catalogs["spell_categories"].entry},
        "visualizations": {entry.id: entry for entry in catalogs["spell_visualizations"].entry},
        "proficiencies": {entry.id: entry for entry in catalogs["proficiencies"].entry},
        "skills": {entry.id: entry for entry in catalogs["skills"].entry},
        "stacking_categories": {entry.id: entry for entry in catalogs["aura_stacking_categories"].entry},
        "item_classes": {entry.id: entry for entry in catalogs["item_classes"].entry},
        "item_subclasses": {entry.id: entry for entry in catalogs["item_subclasses"].entry},
        "ranges": {entry.id: entry for entry in catalogs["ranges"].entry},
    }
    return indexes


def summarize_spell(spell, catalogs: dict[str, object], indexes: dict[str, dict]) -> dict:
    class_lookup = make_mask_lookup(catalogs["classes"].entry)
    race_lookup = make_mask_lookup(catalogs["races"].entry)
    item_refs = []
    for item in catalogs["items"].entry:
        for item_spell in item.spells:
            if item_spell.spell == spell.id:
                item_refs.append(
                    {
                        "item_id": item.id,
                        "item_name": item.name,
                        "trigger": item_spell.trigger,
                        "trigger_name": ITEM_TRIGGER_NAMES.get(item_spell.trigger, str(item_spell.trigger)),
                    }
                )

    class_sources = []
    for class_entry in catalogs["classes"].entry:
        levels = [grant.level for grant in class_entry.spells if grant.spell == spell.id]
        if levels:
            class_sources.append({"class_id": class_entry.id, "class_name": class_entry.name, "levels": levels})

    race_sources = []
    for race_entry in catalogs["races"].entry:
        sources = []
        for class_id, initial_spells in race_entry.initialSpells.items():
            if spell.id in initial_spells.spells:
                class_name = indexes["classes"].get(class_id).name if class_id in indexes["classes"] else f"Class {class_id}"
                sources.append({"class_id": class_id, "class_name": class_name})
        if sources:
            race_sources.append({"race_id": race_entry.id, "race_name": race_entry.name, "sources": sources})

    trainer_sources = []
    for trainer in catalogs["trainers"].entry:
        for trainer_spell in trainer.spells:
            if trainer_spell.spell == spell.id:
                class_entry = indexes["classes"].get(trainer.classid) if trainer.HasField("classid") else None
                trainer_sources.append(
                    {
                        "trainer_id": trainer.id,
                        "trainer_name": trainer.name,
                        "trainer_type": trainer.type,
                        "trainer_type_name": TRAINER_TYPE_NAMES.get(trainer.type, f"Unknown({trainer.type})"),
                        "class_id": trainer.classid if trainer.HasField("classid") else None,
                        "class_name": class_entry.name if class_entry else None,
                        "cost": trainer_spell.spellcost,
                        "required_skill": trainer_spell.reqskill if trainer_spell.HasField("reqskill") else 0,
                        "required_skill_value": trainer_spell.reqskillval if trainer_spell.HasField("reqskillval") else 0,
                        "required_level": trainer_spell.reqlevel if trainer_spell.HasField("reqlevel") else 1,
                    }
                )

    talent_sources = []
    for talent in catalogs["talents"].entry:
        matching_ranks = []
        for rank_index, rank_spell_id in enumerate(talent.ranks):
            if rank_spell_id == spell.id:
                matching_ranks.append(rank_index)
        if matching_ranks:
            tab = indexes["talent_tabs"].get(talent.tab)
            class_entry = indexes["classes"].get(tab.class_id) if tab else None
            talent_sources.append(
                {
                    "talent_id": talent.id,
                    "tab_id": talent.tab,
                    "tab_name": tab.name if tab else None,
                    "class_id": tab.class_id if tab else None,
                    "class_name": class_entry.name if class_entry else None,
                    "row": talent.row,
                    "column": talent.column,
                    "rank_indexes": matching_ranks,
                }
            )

    data = message_to_dict(spell)
    data["attribute_words"] = decode_attribute_words(list(spell.attributes))
    data["effects"] = [effect_to_summary(effect) for effect in spell.effects]
    data["allowed_classes"] = decode_id_mask(spell.classmask, class_lookup) if spell.classmask else []
    data["allowed_races"] = decode_id_mask(spell.racemask, race_lookup) if spell.racemask else []
    data["item_references"] = item_refs
    data["class_sources"] = class_sources
    data["race_sources"] = race_sources
    data["trainer_sources"] = trainer_sources
    data["talent_sources"] = talent_sources

    if spell.category and spell.category in indexes["categories"]:
        data["category_name"] = indexes["categories"][spell.category].id
    if spell.visualization_id and spell.visualization_id in indexes["visualizations"]:
        data["visualization_name"] = indexes["visualizations"][spell.visualization_id].name
    if spell.stacking_category_id and spell.stacking_category_id in indexes["stacking_categories"]:
        data["stacking_category_name"] = indexes["stacking_categories"][spell.stacking_category_id].name
    if spell.skill and spell.skill in indexes["skills"]:
        data["skill_name"] = indexes["skills"][spell.skill].name
    if spell.rangetype and spell.rangetype in indexes["ranges"]:
        data["range_name"] = indexes["ranges"][spell.rangetype].name
    return data


def find_spell(catalogs: dict[str, object], spell_id: int):
    for spell in catalogs["spells"].entry:
        if spell.id == spell_id:
            return spell
    return None


def spell_name_matches(spell, needle: str) -> bool:
    lowered = needle.lower()
    return lowered in spell.name.lower() or lowered in getattr(spell, "description", "").lower()


def dump_json(data) -> str:
    return json.dumps(data, indent=2, ensure_ascii=True)


def summarize_talent_tab(tab) -> dict:
    return {
        "id": tab.id,
        "name": tab.name,
        "class_id": tab.class_id,
        "icon": getattr(tab, "icon", ""),
        "background": getattr(tab, "background", ""),
    }


def summarize_talent(talent, catalogs: dict[str, object], indexes: dict[str, dict]) -> dict:
    rank_spells = []
    for rank_index, spell_id in enumerate(talent.ranks):
        spell = indexes["spells"].get(spell_id)
        rank_spells.append(
            {
                "rank_index": rank_index,
                "spell_id": spell_id,
                "spell_name": spell.name if spell else None,
            }
        )

    tab = indexes["talent_tabs"].get(talent.tab)
    class_entry = indexes["classes"].get(tab.class_id) if tab else None

    return {
        "id": talent.id,
        "tab": talent.tab,
        "tab_name": tab.name if tab else None,
        "class_id": tab.class_id if tab else None,
        "class_name": class_entry.name if class_entry else None,
        "row": talent.row,
        "column": talent.column,
        "rank_count": len(talent.ranks),
        "ranks": rank_spells,
    }


def summarize_trainer(trainer, catalogs: dict[str, object], indexes: dict[str, dict]) -> dict:
    class_entry = indexes["classes"].get(trainer.classid) if trainer.HasField("classid") else None
    spell_rows = []
    for trainer_spell in trainer.spells:
        spell = indexes["spells"].get(trainer_spell.spell)
        skill = indexes["skills"].get(trainer_spell.reqskill) if trainer_spell.HasField("reqskill") else None
        spell_rows.append(
            {
                "spell_id": trainer_spell.spell,
                "spell_name": spell.name if spell else None,
                "cost": trainer_spell.spellcost,
                "required_skill": trainer_spell.reqskill if trainer_spell.HasField("reqskill") else 0,
                "required_skill_name": skill.name if skill else None,
                "required_skill_value": trainer_spell.reqskillval if trainer_spell.HasField("reqskillval") else 0,
                "required_level": trainer_spell.reqlevel if trainer_spell.HasField("reqlevel") else 1,
            }
        )

    return {
        "id": trainer.id,
        "name": trainer.name,
        "type": trainer.type,
        "type_name": TRAINER_TYPE_NAMES.get(trainer.type, f"Unknown({trainer.type})"),
        "class_id": trainer.classid if trainer.HasField("classid") else None,
        "class_name": class_entry.name if class_entry else None,
        "title": trainer.title if trainer.HasField("title") else "",
        "spell_count": len(trainer.spells),
        "spells": spell_rows,
    }
