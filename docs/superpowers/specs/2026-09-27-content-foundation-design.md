# Content Foundation — Design (Spec 1 of 3)

**Date:** 2026-09-27
**Branch:** `feature/content-foundation`
**Status:** approved in brainstorming, pending written-spec review

## 1. Problem

Agent-authored quests and spawns in Alestia Online are low quality because the authoring agent
works blind. A survey of the repository on 2026-09-27 found:

- **No named geography.** Zones are only per-tile area ids (`MCAR`) with no level band or
  description; ~95% of map 0 has no zone; Haven, Market, Mage Tower and Greystone Pass have no
  tiles on any world. Sub-areas ("Barrowfield", "Mirewater", "Kingsroad Waypost") exist only in
  spawn name strings and in agent memory notes.
- **No world perception.** The only terrain query (`inspect_terrain.py`) returns height and area
  id. Nothing reads placed entities (`.wobj`), water (`MCWQ`), holes (`MHOL`), texture layers
  (`MCLY`/`MCMT`, i.e. roads) or slope.
- **Visibly synthetic spawns.** Agent-made packs sit on a regular ~14 m grid, all stationary,
  all `respawndelay` 30000; one pack sits on the terrain edge (x = -498). 15 spawns are buried
  up to 6.7 m, 5 float up to 7.3 m, two stand on 45–50° slopes. Quest 30's targets are all
  `isactive = false`.
- **No lore.** No world bible, no naming conventions; the only design guide is a WoW Ghostlands
  case study.
- **No in-world verification.** The content audit round-trips data and checks chain graphs and XP;
  it never looks at spawns. The level 1–10 E2E path uses GM-spawned targets.
- **Skill drift.** `.agents/skills` (tracked) and `.claude/skills` (gitignored copy) have
  diverged; SKILL.md files point at `F:\mmo`; the `.claude` spell library is stale (the cause of
  this week's content-audit failure on spell 247); the quest SKILL.md contradicts itself about
  object-use counters; the item skill instructs Codex-only tools.

## 2. Goal and program context

Make the agent able to **know, see and check** the world, so that content is designed against real
places and real terrain and reviewed by the user on a map, not by walking the game.

This is spec 1 of a three-part program (approach "foundation first, then pilot"):

| Spec | Scope |
|---|---|
| **1 — Foundation (this doc)** | skill hygiene, world bible, atlas, Python world reader, map renderer, placement lint + content report, editor Atlas mode |
| 2 — Dressing | asset catalog with on-disk thumbnails, `.wobj` prop writer, navmesh query, encounter/camp templates |
| 3 — Pilot + in-world verification | Briarwatch March rework via map review packets, zone-walk E2E with real spawns, in-client camp screenshots |

Agreed working model for later content: **map-based review packets** — the agent proposes a slice
as an annotated top-down map plus a short brief, the user marks changes, the agent applies and
returns an after-map (and, from spec 3, in-client screenshots).

## 3. Non-goals (this spec)

- Placing props or writing `.wobj` files; asset catalog; thumbnails (spec 2).
- Navmesh walkability queries (spec 2).
- Encounter/camp generators (spec 2).
- Reworking any zone's content (spec 3). The only data changes here are the atlas, the bible and
  the lint baseline.
- Any engine, protocol, server or client runtime change. The atlas is never exported to client,
  server or ClientDB.

## 4. Architecture

```
 terrain .tile / .wobj / .hwld           data/editor/data/*.data (protobuf)
            │                                         │
            ▼                                         ▼
  tools/world/worldkit/formats/        tools/world/worldkit/data.py
  (pure-Python parsers, versioned)     (reuses skills' proto runtime)
            │                                         │
            └──────────────┬──────────────────────────┘
                           ▼
                tools/world/worldkit/ ── atlas.py  ◄── data/world/atlas/map_<id>.json
                (snapshot, query)                       ▲            ▲
                           │                            │            │
        ┌──────────────────┼────────────────┐     agents (text)   mmo_edit Atlas mode
        ▼                  ▼                ▼
  render_map.py         lint.py         report.py ──► tools/gate/content_audit.py
  (PNG review maps)  (placement)   (content health)
```

All world reading is **pure Python** (user decision). The C++ side is limited to the editor Atlas
mode (§9), which only reads/writes the atlas JSON.

## 5. Components

### 5.1 `tools/world/worldkit/formats/` — file-format parsers

One module per format, each the only place in Python that knows that format:

- `tile.py` — terrain page `.tile`: `MVER`, `MCVT` (129×129 heights), `MCAR` (16×16 area ids),
  `MCWQ` (water), `MHOL` (holes), `MCMT` (per-tile material path), `MCLY` (layer weights).
  Chunk layouts are taken from `src/shared/terrain/page.cpp` (`Read*Chunk`).
- `wobj.py` — placed entity `.wobj` (`REVW` / `HSMW`: mesh path, position, rotation quaternion,
  scale), under `data/client/Worlds/<W>/<W>/Entities/<pageId>/` with `pageId = x*256 + z`.
- `hwld.py` — world header `.hwld` (`REVM` / `TERR` / `HSEM`), for page extents and mesh list.

**Drift protection:** every parser checks `MVER`/chunk versions and expected chunk sizes and
raises on anything unknown rather than guessing. Parsers are read-only in this spec (the `.wobj`
writer in spec 2 extends `wobj.py`).

Existing `inspect_terrain.py` is rewired to use `worldkit` (keeping its CLI and output shape) so
there is one terrain parser in the repo.

### 5.2 `tools/world/worldkit/` — snapshot and query

- `snapshot.py` builds a per-map in-memory world model, cached to
  `generated/world/<map>/snapshot.npz` (gitignored) and rebuilt when any source tile/entity file is
  newer than the cache. Grids at 4 m cells: height, slope (degrees), area id, water (bool + level),
  hole (bool), dominant terrain material/texture id. Entities: mesh path, transform, world AABB
  (AABB from mesh bounds when cheaply readable, otherwise a conservative default footprint per
  mesh folder, documented).
- `data.py` loads units, spawns (unit + object), quests, objects, items, loot from
  `data/editor/data/*.data` via the proto runtime already used by the skills.
- `query.py`:
  - `height_at(x, z)`, `slope_at`, `area_at`, `water_at`, `hole_at`, `terrain_kind_at`
    (road/grass/forest/rock/… via a material→kind table in `worldkit/terrain_kinds.json`)
  - `poi_at(x, z)` / `pois_near(x, z, r)` (from the atlas)
  - `entities_near`, `spawns_near`
  - `placeable(x, z) -> (ok, reasons[])` — slope, water, hole, inside entity footprint, map edge
- CLI: `python -m worldkit query --map 0 --at -262 405` prints a JSON summary of the point
  (height, slope, kind, area, POI, nearest entities/spawns).

### 5.3 Atlas — `data/world/atlas/map_<id>.json`

Tracked, pretty-printed JSON with stable key order (written by both agents and the editor with the
same formatting so diffs stay minimal). Schema (validated by `worldkit/atlas.py`,
`data/world/atlas/atlas.schema.json`):

```jsonc
{
  "map": 0,
  "version": 1,
  "zones": [
    { "areaId": 8, "name": "Briarwatch March", "levelMin": 5, "levelMax": 10,
      "theme": "wet lowland march, poachers, restless dead",
      "description": "...", "status": "canon" }
  ],
  "pois": [
    { "id": "kingsroad_waypost", "name": "Kingsroad Waypost",
      "kind": "hub",               // hub | camp | ruin | lair | landmark | resource | dungeon_entrance | note
      "center": [-262.0, 405.0],   // x, z (y derived from terrain)
      "radius": 25.0,              // or "polygon": [[x,z], ...]
      "areaId": 8, "levelMin": 7, "levelMax": 9,
      "faction": "human_npcs",     // free text key, see bible
      "description": "...",
      "status": "placeholder",     // placeholder | canon | note
      "ask": "Drag to where the waypost should stand and confirm.",
      "source": "agent-bootstrap" } // agent-bootstrap | agent | user
  ],
  "roads": [
    { "id": "westroad", "name": "Westroad", "points": [[x,z], ...],
      "status": "placeholder", "ask": "Place start/end and any bends." }
  ]
}
```

Status semantics:

- `placeholder` — created by an agent; position and/or facts are guesses. May carry an `ask`
  (a question for the user). Agents must not build content that depends on a placeholder's exact
  position without flagging it in the review packet.
- `canon` — confirmed by the user (editor Confirm button, or explicit instruction in chat).
  Only the user promotes to canon.
- `note` — user-authored design intent ("cave entrance here", "kobold ridge"). Agents treat notes
  as requirements/hints when planning and show them in review packets. `kind: "note"`.

### 5.4 World bible — `docs/world/bible.md`

Three provenance layers; every statement is tagged with its layer:

1. **Established** — from the user's setting notes (provided 2026-09-27).
2. **In-game canon** — extracted from the 57 quest texts, 80 unit names/subnames, item names and
   gossip text currently in `data/editor/data`.
3. **Open questions** — the user's gap table plus contradictions found between (1) and (2) and
   between the bible and the world (e.g. Haven has a zone row and a `Haven_001` WMO but no tiles).

Sections: identity & tone; geography (referencing atlas ids); peoples & factions; named characters
(role, location, quests, unit id); creatures & threats; classes in the fiction; naming guide
(people, places, items — derived from existing names); quest-writing guide (voice, text length,
the "local problem → road → Haven" arc, and the rule **every quest ends with a turn-in at an NPC
or object**). `questline-design-guide.md` stays as structural background; the bible's
quest-writing guide becomes the primary reference.

Agents may append to layer 2 when they author content; only the user promotes to layer 1.

### 5.5 Map renderer — `tools/world/render_map.py`

- Output PNG, default 1 px ≈ 1 m; `--zone`, `--poi`, `--bbox`, `--margin` to crop.
- Toggleable layers: shaded relief, water, roads (terrain-kind road cells + atlas roads), entity
  footprints, zone borders (from area ids), POIs (outlines + labels, colour by status), spawns
  (dots coloured by level or faction, patrol paths, inactive spawns hollow), quest arrows
  (giver → objective source, labelled with quest id).
- `--diff <snapshot-json>` highlights added/moved/removed spawns and entities.
- Legend and scale bar always included.
- Dependencies: Python + numpy + Pillow (already used by `tools/particle_gen` and
  `tools/color_grading`).

### 5.6 Placement lint — `tools/world/lint.py`

Runs on all spawns of a map, or on a draft NPC/encounter JSON (`--draft`). Rules:

| Rule | Severity |
|---|---|
| spawn Y vs terrain height: > 0.5 m | warning |
| spawn Y vs terrain height: > 2 m | error |
| slope > 35° | error |
| in water / in terrain hole | error |
| within 5 m of terrain edge | error |
| inside entity footprint | warning |
| regular grid pattern within a pack (low variance of nearest-neighbour distance and axis alignment) | warning |
| outside the POI it references, or level outside POI/zone band | warning |

A spawn "references" a POI by its name prefix (`"<POI name> - <unit>"`, the current convention)
or by an explicit atlas POI id in a draft.

### 5.7 Content report — `tools/world/report.py`

- Every quest kill/collect objective has at least one **active** source (unit spawn, object spawn,
  or loot from an actively spawned unit) within a configurable distance (default 250 m) of the
  quest giver's position.
- No quest completes without a turn-in (`AutoRewarded` flagged) — user rule.
- Spawn count and level range per POI and per zone; quest givers not inside any POI; zones without
  terrain; POIs with `placeholder` status and open `ask`s.
- Output: JSON (for the audit) and Markdown (for humans / review packets).

### 5.8 Baseline

`tools/world/lint_baseline.json` records the violations present when this spec lands (buried
spawns, quest 30's inactive targets, etc.). Lint/report fail only on violations **not** in the
baseline, so the audit is not red on day one. Baseline entries are keyed by spawn/quest identity
plus rule, so fixing one simply removes it (`--update-baseline` rewrites the file).

## 6. Integration

- **Content audit** (`tools/gate/content_audit.py`): new domains `placement` (lint) and
  `reachability` (report), both baseline-aware. Also switch its `SKILLS` path to the tracked source.
- **Skills** (`mmo-quest-creator`, `mmo-npc-designer`):
  - Design steps must: look up the target POI/zone in the atlas; read the relevant bible sections;
    render the target area before designing.
  - Validators additionally run: quest level within POI/zone band; spawn inside claimed POI;
    `worldkit` lint on draft spawns; naming-guide check (warning only); turn-in rule.
  - After apply: render the after-map with `--diff` for the review packet.
- **xp_audit.py / apply_spell_json.py** keep working unchanged against the tracked skill source.

## 7. Skill hygiene

- `.agents/skills` is the **single tracked source** (Codex reads it). `terrain-author` and
  `particle-author` move into it from the untracked `.claude/skills`.
- `tools/sync_skills.ps1` replaces `.claude/skills` with a directory junction to `.agents/skills`
  (idempotent; safe to run in any worktree; refuses if `.claude/skills` contains files that differ
  from the tracked source, listing them). `verify.ps1` warns (not fails) if the junction is missing
  or not a junction.
- Fix: `F:\mmo` defaults/paths in all SKILL.md and scripts (`inspect_item_catalog.py:128`,
  `validate_item_json.py:136`) — derive the root from the script location like the npc/quest
  scripts do; stale `.claude` spell lib goes away with the junction; quest SKILL.md object-use
  contradiction; `inspect_npc_catalog.py` silent 20-row cap (print a truncation notice); Codex-only
  instructions (`image_gen`, `codex_app.*`, `.codex` paths) rewritten tool-neutrally;
  `terrain-author/references/example-layout-zone.py` hardcoded scratchpad path.

## 8. Bootstrap (first data produced by this spec)

1. **Atlas for map 0**, generated by `tools/world/bootstrap_atlas.py`, everything
   `status: placeholder`, `source: agent-bootstrap`:
   - zones from area-id tile coverage (level band inferred from spawned unit levels in the zone);
   - candidate POIs from spawn clusters + spawn-name prefixes, hubs from quest-giver positions,
     prop groups from entities;
   - known-but-unplaced names from the user's notes (Westroad, Northroad, Forest Camp, Haven,
     the planned dungeon) as placeholders with an `ask`, parked at a best guess.
2. **Review packet** to the user: full-map render + POI list with guessed names/purposes.
3. **Bible v1** drafted from the user's notes + extracted in-game canon, with the open-questions
   layer listing every contradiction found.

## 9. Editor Atlas mode (last phase)

A new world editor edit mode, `src/mmo_edit/editors/world_editor/edit_modes/atlas_edit_mode.{h,cpp}`,
following the `area_trigger_edit_mode` / `fog_volume_edit_mode` precedent.

- Loads `data/world/atlas/map_<id>.json` for the open map (nlohmann json, already a dependency);
  saves with the same stable formatting as `worldkit/atlas.py` (shared rule: 2-space indent, keys
  in schema order, floats rounded to 0.1 m).
- Draws editor-only visuals: pin + name label per POI; radius ring or polygon outline on terrain;
  road polylines; colour by status (placeholder / canon / note).
- Interaction: click terrain to add a pin (kind picker); drag to move (snaps to terrain); details
  panel to edit name, kind, radius, level band, description, notes; add/insert/remove road points;
  delete; **Confirm** button (placeholder → canon, clears `ask`, sets `source: user` if the user
  moved it).
- **"Needs your input" panel**: lists all placeholders with `ask`, with jump-to buttons.
- Unknown JSON fields are preserved on round-trip (the editor must not drop fields agents add).
- Never exported; not part of `.hwld`, ClientDB, or server data.

## 10. Error handling

- Parsers raise with file path + chunk name on unknown versions or size mismatches; tools exit
  non-zero with that message. No silent fallback.
- Atlas schema violations fail `worldkit/atlas.py` load with the offending JSON path; the editor
  shows the same error and refuses to save over a file it could not fully parse.
- Missing snapshot sources (e.g. a map with no terrain) produce an explicit "no terrain" result, not
  zero heights.

## 11. Testing

- `tools/tests/test_world_*.py` (stdlib `unittest`, so the gate's existing `tool_tests` step —
  `python -m unittest discover -s tools/tests -p test_*.py` — picks them up with no gate edit):
  - parser invariants over all shipped `.tile` / `.wobj` / `.hwld` files (parse without error,
    heights finite, layer weights normalised, entity mesh paths exist);
  - `height_at` parity with the pre-change `inspect_terrain.py` output on sampled points;
  - `query` on known points (Oakenshire inn footprint, a water page, a hole page);
  - each lint rule on small synthetic spawn sets (including a grid pack and an edge spawn);
  - report reachability on a synthetic quest/spawn set;
  - atlas schema validation + stable-format round-trip (load → save → byte-identical);
  - renderer smoke test (fixed inputs → image of expected size, no exception).
- Editor Atlas mode: manual check (add/move/confirm/save; round-trip preserves unknown fields;
  file byte-identical after open+save without edits).
- No C++ runtime/protocol change → no protocol bump; gate runs as usual.

## 12. Open items deferred to later specs

- Walkability (navmesh) in `placeable()` — spec 2.
- Mesh-accurate entity footprints (current: bounds or per-folder default) — spec 2 with the asset
  catalog.
- Promotion of atlas data to engine data (map labels, subzone discovery) — only if gameplay needs it.
