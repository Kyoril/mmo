# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Asset tags and placement defaults (tools/world/asset_tags.json).

Each rule has a case-insensitive glob `match` on the asset path (relative to data/client, '/'
separators; '*' also crosses folders) and any of the fields below. Rules apply in file order: tags
accumulate, every other field is overwritten by later matches. The user's edits to the JSON win over
anything an agent wrote; agents only add rules for assets that have none.
"""

from __future__ import annotations

import fnmatch
import json
from dataclasses import dataclass, replace
from pathlib import Path

from .paths import REPO

TAGS_PATH = REPO / "tools" / "world" / "asset_tags.json"
VOCABULARY = frozenset({
    "rock", "cliff_rock", "rubble", "ruin", "wall", "tower", "building", "tent", "camp", "fire", "crate", "barrel",
    "cart", "tool", "fence", "light", "tree", "tree_dead", "bush", "plant", "herb", "mine", "bridge", "pier",
    "dungeon", "food", "furniture", "prototype",
    "log", "banner",         # fallen timber, branches and stumps; flags and banners
    "character", "marker",   # not scenery: characters, creatures, weapons; quest marks and gathering nodes
})
NEVER_PICK = frozenset({"prototype", "dungeon", "character", "marker"})   # only when a query asks for them by tag or allow-list
_FIELDS = {"match", "tags", "scale", "align_to_slope", "max_slope", "sink", "may_overlap", "collides_override",
           "footprint_scale"}


@dataclass(frozen=True)
class AssetTags:
    tags: frozenset = frozenset()
    scale: tuple[float, float] = (1.0, 1.0)
    align_to_slope: bool = False
    max_slope: float = 20.0
    sink: float = 0.05             # metres the base may go into the ground
    may_overlap: frozenset = frozenset()
    collides_override: bool | None = None
    footprint_scale: float = 1.0   # trees: only the trunk blocks (e.g. 0.15)


class TagRules:
    def __init__(self, rules: list[dict]):
        for index, rule in enumerate(rules):
            unknown = set(rule) - _FIELDS
            if unknown or "match" not in rule:
                raise ValueError(f"asset tag rule {index}: unknown fields {sorted(unknown)} or no 'match'")
            bad = (set(rule.get("tags", [])) | set(rule.get("may_overlap", []))) - VOCABULARY
            if bad:
                raise ValueError(f"asset tag rule {index}: tags {sorted(bad)} are not in the vocabulary")
        self.rules = rules

    def for_asset(self, path: str) -> AssetTags:
        result = AssetTags()
        lowered = path.replace("\\", "/").lower()
        for rule in self.rules:
            if not fnmatch.fnmatchcase(lowered, rule["match"].lower()):
                continue
            fields = {}
            for key, value in rule.items():
                if key == "match":
                    continue
                if key == "tags":
                    fields["tags"] = result.tags | frozenset(value)
                elif key == "may_overlap":
                    fields["may_overlap"] = frozenset(value)
                elif key == "scale":
                    fields["scale"] = (float(value[0]), float(value[1]))
                else:
                    fields[key] = value
            result = replace(result, **fields)
        return result


def load_tag_rules(path: Path = TAGS_PATH) -> TagRules:
    doc = json.loads(Path(path).read_text(encoding="utf-8"))
    return TagRules(doc.get("rules", []))
