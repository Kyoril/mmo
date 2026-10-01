# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""worldkit command line.

    python -m worldkit query --map 0 --at -262 405
    python -m worldkit layers --world Development --page 32_32 --out generated/world/layers_32_32.png
    python -m worldkit assets [--rebuild] [--untagged] [--sheets] [--views ASSET [--out FILE]]

(Run from tools/world, or put tools/world on PYTHONPATH.)
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from .formats.tile import parse_tile
from .paths import REPO, terrain_dir


def cmd_layers(args) -> int:
    page = parse_tile(terrain_dir(args.world, REPO) / f"{args.page}.tile")
    panels = []
    for index in range(4):
        weights = ((page.layers >> np.uint32(8 * index)) & np.uint32(0xFF)).astype(np.uint8)
        # Rows are +z, i.e. north (-z) is the first row: the same north-up view as the minimap.
        panel = Image.fromarray(np.ascontiguousarray(weights)).convert("RGB").resize((504, 504))
        ImageDraw.Draw(panel).text((8, 8), f"layer {index}", fill=(255, 0, 0))
        panels.append(panel)
    sheet = Image.new("RGB", (1008, 1008))
    for index, panel in enumerate(panels):
        sheet.paste(panel, ((index % 2) * 504, (index // 2) * 504))
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)
    names = sorted({name or "<world default>" for name in page.materials})
    print(f"wrote {out}; tile materials on this page: {', '.join(names)}")
    return 0


def cmd_query(args) -> int:
    from .cli_query import run_query
    return run_query(args)


def cmd_assets(args) -> int:
    from .assets import build_catalog
    from .tags import load_tag_rules
    catalog = build_catalog(rebuild=args.rebuild)
    rules = load_tag_rules()
    errors = [i for i in catalog.values() if i.error]
    print(f"{len(catalog)} assets, {len(errors)} unreadable, "
          f"{sum(1 for i in catalog.values() if i.has_collision)} with collision, "
          f"{sum(1 for i in catalog.values() if i.kind == 'mesh' and not i.error and not any(i.textures))} meshes without a resolved texture")
    for info in errors[:20]:
        print(f"  unreadable: {info.path}: {info.error}")
    if args.untagged:
        for rel in sorted(catalog):
            if not catalog[rel].error and not rules.for_asset(rel).tags:
                print(f"  untagged: {rel}")
    if args.sheets or args.views:
        from .assets import GeometryCache
        from .materials import TextureCache
        from .paths import assets_cache_dir, client_root
        from .previews import asset_views, write_contact_sheets
        geometry, textures = GeometryCache(), TextureCache(client_root())
        if args.sheets:
            paths = write_contact_sheets(catalog, geometry, textures, assets_cache_dir() / "sheets")
            print(f"wrote {len(paths)} contact sheets to {assets_cache_dir() / 'sheets'}")
        if args.views:
            out = args.out or assets_cache_dir() / "views" / (Path(args.views).stem + ".png")
            out.parent.mkdir(parents=True, exist_ok=True)
            asset_views(args.views, catalog, geometry, textures).save(out)
            print(f"wrote {out}")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="worldkit")
    sub = parser.add_subparsers(dest="command", required=True)

    layers = sub.add_parser("layers", help="render the 4 splat layer weights of one terrain page")
    layers.add_argument("--world", required=True, help="world directory, e.g. Development")
    layers.add_argument("--page", required=True, help="page as <x>_<z>, e.g. 32_32")
    layers.add_argument("--out", required=True)
    layers.set_defaults(func=cmd_layers)

    query = sub.add_parser("query", help="describe a world point (height, slope, kind, POI, placeable)")
    query.add_argument("--map", type=int, required=True)
    query.add_argument("--at", type=float, nargs=2, metavar=("X", "Z"), required=True)
    query.add_argument("--no-cache", action="store_true")
    query.set_defaults(func=cmd_query)

    assets = sub.add_parser("assets", help="build the asset catalog and print a summary")
    assets.add_argument("--rebuild", action="store_true", help="ignore the cache")
    assets.add_argument("--untagged", action="store_true", help="list assets no tag rule matches")
    assets.add_argument("--sheets", action="store_true", help="write contact sheets to generated/world/assets/sheets")
    assets.add_argument("--views", metavar="ASSET", help="write front/side/top views of one asset")
    assets.add_argument("--out", type=Path, help="output file for --views")
    assets.set_defaults(func=cmd_assets)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
