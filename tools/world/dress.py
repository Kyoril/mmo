#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Dress confirmed atlas places with props and trees, one undoable pass at a time.

    python tools/world/dress.py plan --poi south_ridge_quarry --template quarry --seed 7 [--keep-away forest_bandit_grounds:20]
    python tools/world/dress.py check 20261001-south_ridge_quarry-1          # after editing the draft by hand
    python tools/world/dress.py apply 20261001-south_ridge_quarry-1 [--skip-nav]
    python tools/world/dress.py check --nav PASS [PASS ...]                   # one scratch nav build, walkability for all
    python tools/world/dress.py undo PASS [--force]
    python tools/world/dress.py list
    python tools/world/dress.py nav-baseline                                  # store protected-route lengths
    python tools/world/dress.py packet-index --name ring-pilot PASS [PASS ...]
    python tools/world/dress.py publish-nav --yes                             # rebuild the REAL navmesh (user request only)

apply, undo and publish-nav refuse while mmo_edit runs. data/client is never committed by this tool.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from worldkit.dress_commands import after_map, check_pass, nav_check, open_session, pass_images, plan_pass
from worldkit.dressing import PassError, apply_pass, editor_running, list_docs, load_doc, undo_pass
from worldkit.nav import NavError, NavQuery, build_nav, load_routes, route_length, save_routes, scratch_nav_root
from worldkit.packet import write_batch_index, write_packet
from worldkit.paths import REPO, passes_dir


def _summary(doc: dict) -> str:
    errors = [v for v in doc["checks"]["placement"] + doc["checks"]["walkability"] if v["severity"] == "error"]
    warnings = [v for v in doc["checks"]["placement"] + doc["checks"]["walkability"] if v["severity"] == "warning"]
    return (f"{doc['pass_id']}: {doc['status']}, {len(doc['items'])} items, {len(errors)} errors, "
            f"{len(warnings)} warnings, {len(doc['gaps'])} gaps")


def _packet(session, doc) -> Path:
    images = pass_images(session, doc)
    after_map(session, doc)                                  # lands in the pass's working folder; the packet embeds it
    return write_packet(doc, images, REPO)


def _session(args, docs):
    """A session on the map the passes were planned for. An explicit --map that disagrees is refused."""
    maps = {doc["map"] for doc in docs}
    if len(maps) != 1:
        raise PassError(f"the passes are on different maps ({sorted(maps)}): check them separately")
    map_id = maps.pop()
    if args.map is not None and args.map != map_id:
        raise PassError(f"{docs[0]['pass_id']} belongs to map {map_id}, not --map {args.map}")
    session = open_session(map_id)
    for doc in docs:
        if doc["world"] != session.directory:
            raise PassError(f"{doc['pass_id']} was planned for world {doc['world']!r}, but map {map_id} is {session.directory!r}")
    return session


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--map", type=int, default=None, help="map id (plan, nav-baseline, publish-nav; default 0)")
    sub = parser.add_subparsers(dest="command", required=True)
    plan = sub.add_parser("plan")
    plan.add_argument("--poi", required=True)
    plan.add_argument("--template", required=True)
    plan.add_argument("--seed", type=int, default=1)
    plan.add_argument("--anchor", type=float, nargs=2, metavar=("X", "Z"))
    plan.add_argument("--keep-away", action="append", default=[], metavar="POI:METRES")
    plan.add_argument("--allow-placeholder", action="store_true")
    check = sub.add_parser("check")
    check.add_argument("passes", nargs="+")
    check.add_argument("--nav", action="store_true", help="walkability for applied passes (one scratch build)")
    apply = sub.add_parser("apply")
    apply.add_argument("pass_id")
    apply.add_argument("--skip-nav", action="store_true")
    undo = sub.add_parser("undo")
    undo.add_argument("pass_id")
    undo.add_argument("--force", action="store_true")
    sub.add_parser("list")
    sub.add_parser("nav-baseline")
    index = sub.add_parser("packet-index")
    index.add_argument("--name", required=True)
    index.add_argument("passes", nargs="+")
    publish = sub.add_parser("publish-nav")
    publish.add_argument("--yes", action="store_true")
    args = parser.parse_args(argv)

    try:
        if args.command == "list":
            for doc in list_docs():
                print(_summary(doc))
            return 0
        if args.command == "packet-index":
            print(write_batch_index(args.name, [load_doc(p) for p in args.passes]))
            return 0
        if args.command == "undo":
            doc = load_doc(args.pass_id)
            if args.map is not None and args.map != doc["map"]:
                raise PassError(f"{doc['pass_id']} belongs to map {doc['map']}, not --map {args.map}")
            report = undo_pass(doc, force=args.force)
            print(f"{doc['pass_id']}: {doc['status']}; removed {len(report.removed)}, kept (changed by you) {len(report.changed)}, "
                  f"already gone {len(report.missing)}")
            for item in report.changed:
                print(f"  kept: {item}")
            return 0
        map_id = args.map if args.map is not None else 0
        if args.command == "publish-nav":
            if not args.yes:
                print("publish-nav rebuilds data/editor/nav (the real navmesh). Re-run with --yes when the user asked for it.")
                return 1
            if editor_running():
                raise PassError("close mmo_edit first")
            session = open_session(map_id)
            print(build_nav(session.directory, REPO / "data" / "editor", REPO))
            return 0
        if args.command == "check":
            docs = [load_doc(p) for p in args.passes]
            session = _session(args, docs)
            if args.nav:
                try:
                    nav_check(session, docs)
                except NavError:
                    for doc in docs:                          # nav_check saved the unchecked ones; show where each stands
                        print(_summary(doc))
                    raise
                for doc in docs:
                    print(_summary(doc), "->", _packet(session, doc))
            else:
                for doc in docs:
                    print(_summary(check_pass(session, doc)))
        elif args.command == "apply":
            doc = load_doc(args.pass_id)
            session = _session(args, [doc])
            doc = apply_pass(doc)                            # saved as applied-unchecked; the check promotes it
            nav_error = None
            if not args.skip_nav:
                try:
                    nav_check(session, [doc])
                except NavError as exc:
                    nav_error = exc                          # nav_check already saved the pass as applied-unchecked
            recovery = (f"Run `python tools/world/dress.py check --nav {doc['pass_id']}` to re-check it, or "
                        f"`python tools/world/dress.py undo {doc['pass_id']}` to remove the props.")
            try:
                packet = _packet(session, doc)               # the props are in the world: always leave a reviewable packet
            except PassError as exc:
                print(_summary(doc))
                note = f"; the navmesh check also failed ({nav_error})" if nav_error is not None else ""
                print(f"error: pass {doc['pass_id']} was already applied (the props are written){note}, but building its "
                      f"review packet failed: {exc}. {recovery}", file=sys.stderr)
                return 1
            print(_summary(doc), "->", packet)
            if nav_error is not None:
                print(f"error: the props are placed but the navmesh check failed ({nav_error}); the pass is applied-unchecked. "
                      f"{recovery}", file=sys.stderr)
                return 1
        else:
            session = open_session(map_id)
            if args.command == "plan":
                doc = plan_pass(session, args.poi, args.template, args.seed, args.anchor, args.keep_away, args.allow_placeholder)
                print(_summary(doc))
                for gap in doc["gaps"]:
                    print(f"  gap: {gap['role']} ({gap['kind']}): placed {gap['placed']} of {gap['wanted']}")
                print(f"draft and previews: {passes_dir() / doc['pass_id']}")
            elif args.command == "nav-baseline":
                nav_dir = build_nav(session.directory, scratch_nav_root(), REPO)
                routes = load_routes()
                with NavQuery(nav_dir, session.directory) as nav:
                    for route in routes:
                        route.baseline_length = route_length(nav, session.query, route.points)
                        print(f"{route.id}: {route.baseline_length}")
                save_routes(routes)
        return 0
    except (PassError, NavError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as exc:
        detail = ((exc.stderr or "") + (exc.stdout or "")) if isinstance(exc.stderr, str) or isinstance(exc.stdout, str) else ""
        print(f"error: {' '.join(map(str, exc.cmd))[:200]} failed (exit {exc.returncode}) {detail.strip()[-800:]}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
