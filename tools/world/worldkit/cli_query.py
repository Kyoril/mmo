# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Shared map opening for the worldkit CLIs, and the `python -m worldkit query` command."""

from __future__ import annotations

import json

from .atlas import load_atlas_or_none
from .data import GameData, load_game_data
from .paths import atlas_path
from .query import WorldQuery
from .snapshot import load_snapshot
from .terrain_kinds import load_kinds


def open_map(map_id: int, use_cache: bool = True):
    """Returns (game_data, map_entry, query) for a map id; raises SystemExit for an unknown map
    and NoTerrainError for a map whose world has no terrain pages."""
    data: GameData = load_game_data()
    map_entry = data.maps.get(map_id)
    if map_entry is None:
        raise SystemExit(f"unknown map id {map_id}")
    snapshot = load_snapshot(map_entry.directory, use_cache=use_cache)
    zone_names = {zone_id: zone.name for zone_id, zone in data.zones.items()}
    query = WorldQuery(snapshot, load_atlas_or_none(atlas_path(map_id)), load_kinds(), zone_names)
    return data, map_entry, query


def run_query(args) -> int:
    _, _, query = open_map(args.map, use_cache=not args.no_cache)
    print(json.dumps(query.describe(args.at[0], args.at[1]), indent=2, ensure_ascii=False))
    return 0
