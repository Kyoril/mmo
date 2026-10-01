# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Orchestration behind tools/world/dress.py: plan, check, walkability and pictures for passes."""

from __future__ import annotations

import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from .assets import GeometryCache, build_catalog
from .atlas import Atlas
from .dressing import APPLIED, PassError, new_draft, new_pass_id, save_doc, world_fingerprint
from .foliage import all_instances, load_world_foliage
from .materials import TextureCache
from .nav import NavError, NavQuery, build_nav, load_routes, scratch_nav_root, walkability_violations
from .paths import REPO, client_root, passes_dir, templates_dir, world_tools
from .previews import site_previews
from .prop_lint import PropContext, lint_props, props_from_entities, props_from_foliage
from .spawns import object_spawn_records, unit_spawn_records
from .tags import load_tag_rules
from .templates import item_to_prop, load_template, place


@dataclass
class DressSession:
    map_entry: object
    query: object
    catalog: dict
    rules: object
    foliage: dict
    spawns: list
    repo: Path = REPO

    @property
    def directory(self) -> str:
        return self.map_entry.directory

    @property
    def atlas(self) -> Atlas:
        return self.query.atlas


def open_session(map_id: int = 0, repo: Path = REPO) -> DressSession:
    from .cli_query import open_map
    _, map_entry, query = open_map(map_id)
    if map_entry.instancetype != 0:
        raise PassError(f"map {map_id} is an instance map; dressing works on terrain maps only")
    return DressSession(map_entry, query, build_catalog(repo), load_tag_rules(), load_world_foliage(map_entry.directory, repo),
                        unit_spawn_records(map_entry) + object_spawn_records(map_entry), repo)


def _poi(session: DressSession, poi_id: str) -> dict:
    poi = session.atlas.poi(poi_id) if session.atlas else None
    if poi is None:
        raise PassError(f"no place {poi_id!r} in the atlas")
    return poi


def _context(session: DressSession, poi: dict | None) -> PropContext:
    existing = props_from_entities(session.query.snapshot.entities) + props_from_foliage(session.foliage)
    roads = [road["points"] for road in session.atlas.roads] if session.atlas else []
    return PropContext(session.query, session.catalog, session.rules, existing, session.spawns, poi, roads)


def _keep_away(session: DressSession, specs) -> list[tuple[float, float, float]]:
    circles = []
    for spec in specs:
        poi_id, _, metres = spec.partition(":")
        poi = _poi(session, poi_id)
        circles.append((poi["center"][0], poi["center"][1], float(poi.get("radius", 0.0)) + float(metres or 0.0)))
    return circles


def _lint(session: DressSession, doc: dict) -> list[dict]:
    context = _context(session, _poi(session, doc["poi"]))
    # An applied pass is already in the world: drop its own props from "existing" so it does not collide with itself.
    own = {str(int(item["unique_id"], 16)) for item in doc["items"] if item.get("unique_id")}
    context.existing = [p for p in context.existing if p.key.split(":", 1)[-1] not in own]
    props = [item_to_prop(item, f"{doc['pass_id']}:{i}") for i, item in enumerate(doc["items"])]
    return [v.to_dict() for v in lint_props(props, context)]


def _render_map(session: DressSession, doc: dict, name: str, extra: list[str]) -> Path:
    folder = passes_dir(session.repo) / doc["pass_id"]
    folder.mkdir(parents=True, exist_ok=True)
    out = folder / name
    try:
        subprocess.run([sys.executable, str(world_tools(session.repo) / "render_map.py"), "--map", str(session.map_entry.id),
                        "--poi", doc["poi"], "--margin", "60", "--out", str(out), *extra], check=True,
                       cwd=str(world_tools(session.repo)), capture_output=True, text=True, errors="replace")
    except subprocess.CalledProcessError as exc:
        detail = ((exc.stderr or "") + (exc.stdout or "")).strip()[-800:]
        raise PassError(f"rendering {name} for {doc['pass_id']} failed (exit {exc.returncode}): {detail}") from exc
    return out


def pass_images(session: DressSession, doc: dict) -> dict:
    poi = _poi(session, doc["poi"])
    radius = float(poi.get("radius", 30.0))
    previews = site_previews(session.query, session.catalog, GeometryCache(session.repo), TextureCache(client_root(session.repo)),
                             tuple(doc["anchor"]), radius, session.query.snapshot.entities, all_instances(session.foliage), doc["items"])
    return {f"preview_{k + 1}.png": image for k, (_, image) in enumerate(previews)}


def plan_pass(session: DressSession, poi_id: str, template_name: str, seed: int = 1, anchor=None, keep_away=(),
              allow_placeholder: bool = False, render: bool = True) -> dict:
    poi = _poi(session, poi_id)
    if poi.get("status") != "canon" and not allow_placeholder:
        raise PassError(f"place {poi_id!r} is {poi.get('status')}: dress confirmed (canon) places, or pass --allow-placeholder")
    template = load_template(template_name, templates_dir(session.repo))
    centre = tuple(anchor) if anchor else tuple(poi["center"])
    circles = _keep_away(session, keep_away)
    result = place(template, centre, float(poi.get("radius", 30.0)), _context(session, poi), seed, circles)
    doc = new_draft(new_pass_id(poi_id, session.repo), session.map_entry.id, session.directory, poi_id, template_name, seed,
                    centre, list(keep_away), result.entry, result.items, result.gaps, template.notes,
                    world_fingerprint(session.directory, session.repo))
    doc["checks"]["placement"] = _lint(session, doc)
    save_doc(doc, session.repo)
    if render:
        _render_map(session, doc, "before.png", ["--dump-state", str(passes_dir(session.repo) / doc["pass_id"] / "before.json")])
        for name, image in pass_images(session, doc).items():
            image.save(passes_dir(session.repo) / doc["pass_id"] / name)
    return doc


def check_pass(session: DressSession, doc: dict, render: bool = True) -> dict:
    """Re-lints a (hand-edited) draft and re-fingerprints the world it was checked against."""
    if doc["status"] != "planned":
        raise PassError(f"pass {doc['pass_id']} is {doc['status']}; only planned drafts are re-checked (use --nav for applied ones)")
    doc["checks"]["placement"] = _lint(session, doc)
    doc["world_fingerprint"] = world_fingerprint(session.directory, session.repo)
    save_doc(doc, session.repo)
    if render:
        for name, image in pass_images(session, doc).items():
            image.save(passes_dir(session.repo) / doc["pass_id"] / name)
    return doc


def nav_check(session: DressSession, docs: list[dict], runner=None, nav_factory=None) -> None:
    """One scratch navmesh build for all given applied passes, then their walkability checks.

    Any failure or interruption (including KeyboardInterrupt, which is re-raised unchanged) after the applied state
    is known leaves the not yet checked passes "applied-unchecked" with nav.error set (a partially-undone pass keeps
    its status); an ordinary failure raises NavError; passes checked before the
    failure keep their result.
    """
    for doc in docs:
        if doc["status"] not in APPLIED:
            raise PassError(f"pass {doc['pass_id']} is {doc['status']}; walkability is only checked for applied passes")
    pending = list(docs)

    def mark_unchecked(exc: BaseException) -> None:
        message = str(exc)[-500:] or type(exc).__name__
        for doc in pending:
            if doc["status"] != "partially-undone":
                doc["status"] = "applied-unchecked"
            doc["nav"] = {"error": message}
            try:
                save_doc(doc, session.repo)
            except Exception as save_exc:  # never let a failed save mask the exception that got us here
                print(f"warning: could not record {doc['pass_id']} as applied-unchecked: {save_exc}", file=sys.stderr)

    try:
        nav_dir = build_nav(session.directory, scratch_nav_root(session.repo), session.repo, runner=runner or subprocess.run)
        routes = load_routes()
        factory = nav_factory or (lambda: NavQuery(nav_dir, session.directory, session.repo))
        with factory() as nav:
            while pending:
                doc = pending[0]
                poi = _poi(session, doc["poi"])
                site = {"name": doc["poi"], "x": doc["entry"][0], "z": doc["entry"][1], "radius": float(poi.get("radius", 30.0))}
                violations = walkability_violations(nav, session.query, routes, [site], session.spawns)
                doc["checks"]["walkability"] = [v.to_dict() for v in violations]
                doc["nav"] = {"path": nav_dir.relative_to(session.repo).as_posix()}
                if doc["status"] != "partially-undone":
                    doc["status"] = "applied"
                save_doc(doc, session.repo)
                pending.pop(0)
    except BaseException as exc:  # a failed or interrupted check must never lose the applied state or the pass already checked
        mark_unchecked(exc)
        if isinstance(exc, NavError) or not isinstance(exc, Exception):
            raise                                              # KeyboardInterrupt / SystemExit propagate unchanged
        raise NavError(str(exc)) from exc


def after_map(session: DressSession, doc: dict) -> Path:
    before = passes_dir(session.repo) / doc["pass_id"] / "before.json"
    extra = ["--diff", str(before)] if before.is_file() else []
    return _render_map(session, doc, "after.png", extra)
