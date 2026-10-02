#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Create the first atlas for a map from existing spawns, quest givers and props (all placeholders).

    python tools/world/bootstrap_atlas.py --map 0

Refuses to overwrite an existing atlas (it holds the user's names and confirmations) unless --force.
"""

from __future__ import annotations

import argparse
import sys

from worldkit.atlas import save_atlas
from worldkit.bootstrap import bootstrap_atlas
from worldkit.cli_query import open_map
from worldkit.paths import atlas_path


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--map", type=int, default=0)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args(argv)

    target = atlas_path(args.map)
    if target.exists() and not args.force:
        print(f"refusing to overwrite {target}; it may contain confirmed places. Use --force to replace it.")
        return 1
    data, map_entry, query = open_map(args.map)
    atlas = bootstrap_atlas(data, map_entry, query)
    save_atlas(atlas, target)
    print(f"wrote {target}: {len(atlas.zones)} zones, {len(atlas.pois)} places, {len(atlas.roads)} roads (all placeholders)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
