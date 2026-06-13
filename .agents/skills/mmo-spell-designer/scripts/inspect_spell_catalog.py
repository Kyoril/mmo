#!/usr/bin/env python3
"""Inspect live MMO spell-related protobuf data."""

from __future__ import annotations

import argparse
import json

from spell_catalog_lib import (
    build_indexes,
    dump_json,
    find_spell,
    load_catalogs,
    spell_name_matches,
    summarize_spell,
    summarize_talent,
    summarize_talent_tab,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", default=None)
    parser.add_argument(
        "--section",
        choices=[
            "all",
            "spells",
            "spell",
            "classes",
            "races",
            "items",
            "categories",
            "visualizations",
            "proficiencies",
            "skills",
            "stacking",
            "ranges",
            "talents",
            "talent_tabs",
        ],
        default="spell",
    )
    parser.add_argument("--spell-id", type=int)
    parser.add_argument("--talent-id", type=int)
    parser.add_argument("--name-contains")
    parser.add_argument("--class-name")
    parser.add_argument("--limit", type=int, default=25)
    parser.add_argument("--pretty", action="store_true")
    args = parser.parse_args()

    catalogs = load_catalogs(args.project_root)
    indexes = build_indexes(catalogs)

    def compact_spell(spell):
        return {"id": spell.id, "name": spell.name}

    class_name_filter = args.class_name.lower() if isinstance(args.class_name, str) else None

    if args.section == "spell":
        if args.spell_id is None:
            raise SystemExit("--spell-id is required for --section spell")
        spell = find_spell(catalogs, args.spell_id)
        if not spell:
            raise SystemExit(f"Spell {args.spell_id} not found")
        output = {"spell": summarize_spell(spell, catalogs, indexes)}
    elif args.section == "spells":
        spells = list(catalogs["spells"].entry)
        if args.name_contains:
            spells = [spell for spell in spells if spell_name_matches(spell, args.name_contains)]
        output = {"spells": [compact_spell(spell) for spell in spells[: args.limit]]}
    elif args.section == "classes":
        classes = list(catalogs["classes"].entry)
        if class_name_filter:
            classes = [entry for entry in classes if entry.name.lower() == class_name_filter]
        output = {
            "classes": [
                {"id": entry.id, "name": entry.name, "internal_name": entry.internalName, "spellfamily": entry.spellfamily}
                for entry in classes
            ]
        }
    elif args.section == "races":
        output = {"races": [{"id": entry.id, "name": entry.name} for entry in catalogs["races"].entry]}
    elif args.section == "items":
        output = {
            "items": [
                {"id": entry.id, "name": entry.name, "itemclass": entry.itemclass, "subclass": entry.subclass}
                for entry in catalogs["items"].entry[: args.limit]
            ]
        }
    elif args.section == "categories":
        output = {
            "categories": [
                {"id": entry.id, "spells": list(entry.spells[:10]), "spell_count": len(entry.spells)}
                for entry in catalogs["spell_categories"].entry
            ]
        }
    elif args.section == "visualizations":
        output = {
            "visualizations": [
                {"id": entry.id, "name": entry.name, "icon": entry.icon}
                for entry in catalogs["spell_visualizations"].entry[: args.limit]
            ]
        }
    elif args.section == "proficiencies":
        output = {
            "proficiencies": [{"id": entry.id, "name": entry.name, "flags": entry.flags} for entry in catalogs["proficiencies"].entry]
        }
    elif args.section == "skills":
        output = {
            "skills": [{"id": entry.id, "name": entry.name} for entry in catalogs["skills"].entry[: args.limit]]
        }
    elif args.section == "stacking":
        output = {
            "stacking_categories": [{"id": entry.id, "name": entry.name} for entry in catalogs["aura_stacking_categories"].entry]
        }
    elif args.section == "ranges":
        output = {
            "ranges": [{"id": entry.id, "name": entry.name, "range": entry.range} for entry in catalogs["ranges"].entry]
        }
    elif args.section == "talent_tabs":
        tabs = list(catalogs["talent_tabs"].entry)
        if class_name_filter:
            matching_classes = {entry.id for entry in catalogs["classes"].entry if entry.name.lower() == class_name_filter}
            tabs = [entry for entry in tabs if entry.class_id in matching_classes]
        output = {"talent_tabs": [summarize_talent_tab(entry) for entry in tabs[: args.limit]]}
    elif args.section == "talents":
        talents = list(catalogs["talents"].entry)
        if args.talent_id is not None:
            talents = [entry for entry in talents if entry.id == args.talent_id]
            if not talents:
                raise SystemExit(f"Talent {args.talent_id} not found")
        if class_name_filter:
            matching_tabs = {
                tab.id
                for tab in catalogs["talent_tabs"].entry
                for class_entry in catalogs["classes"].entry
                if class_entry.name.lower() == class_name_filter and tab.class_id == class_entry.id
            }
            talents = [entry for entry in talents if entry.tab in matching_tabs]
        output = {"talents": [summarize_talent(entry, catalogs, indexes) for entry in talents[: args.limit]]}
    else:
        spells = list(catalogs["spells"].entry)
        if args.name_contains:
            spells = [spell for spell in spells if spell_name_matches(spell, args.name_contains)]
        output = {
            "spells": [compact_spell(spell) for spell in spells[: args.limit]],
            "classes": [{"id": entry.id, "name": entry.name} for entry in catalogs["classes"].entry],
            "races": [{"id": entry.id, "name": entry.name} for entry in catalogs["races"].entry],
            "talent_tabs": [summarize_talent_tab(entry) for entry in catalogs["talent_tabs"].entry[: args.limit]],
            "talents": [summarize_talent(entry, catalogs, indexes) for entry in catalogs["talents"].entry[: args.limit]],
            "proficiencies": [{"id": entry.id, "name": entry.name} for entry in catalogs["proficiencies"].entry],
        }

    if args.pretty:
        print(dump_json(output))
    else:
        print(json.dumps(output, ensure_ascii=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
