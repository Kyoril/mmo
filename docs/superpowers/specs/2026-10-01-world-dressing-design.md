# World Dressing — Design (Spec 2 of 3)

**Date:** 2026-10-01
**Branch:** `feature/world-dressing` (based on `feature/content-foundation`)
**Status:** approved in brainstorming, pending written-spec review
**Builds on:** [2026-09-27-content-foundation-design.md](2026-09-27-content-foundation-design.md)

## 1. Problem

Spec 1 gave the agent eyes for the world: terrain queries, spawn lint, an atlas of named places and a
world bible. The user has since confirmed places on the Oakenshire mountain ring, but each one exists
only as an atlas pin. Nothing a player can see stands there.

A survey on 2026-10-01 found:

- **Props:** `data/client/Models` holds 578 meshes and WMOs. The Development world uses 38 of them,
  147 times in total. Unused assets include five bridges, five towers, an inn, a mine WMO and 104 rocks.
- **No picture of the assets.** The agent cannot see what an asset looks like:
  - mmo_edit's asset thumbnails are rendered into memory only
    (`src/mmo_edit/preview_providers/mesh_preview_provider.cpp`), with no disk cache;
  - WMOs have no preview provider at all.
- **No size data.** `.hmsh` files store no bounds; the reader computes them from the vertices.
  `.hwmo` files store an AABB in their `MOHD` header.
- **No writer.** Nothing tool-side writes props:
  - `.wobj` files are one file per entity, at `Entities/<pageIndex>/<uniqueId>.wobj`, and are found
    by listing that directory at startup;
  - trees are instances in one `.hfol` file per page.
- **No walkability check.** No tool answers "can a player walk here?":
  - `nav_builder` builds the navmesh from terrain, `.wobj` collision and colliding foliage;
  - `nav::Map` can find paths, but has no command-line front end.
- **Already working:** the Hollow Choir entrance. Area trigger 4 at (107, 30, 345) on map 0 teleports
  to map 1, and area trigger 3 brings players back. That spot is the user's confirmed abbey pin.
- **No waterfall.** Water is a flat or ramped per-page heightfield (`MCWQ`). There is no vertical water,
  waterfall mesh or waterfall particle effect.

## 2. Goal and program context

Let the agent **dress** confirmed atlas places with props and trees on its own, so that the user
reviews finished-looking sites instead of placing every rock by hand. Every pass must be cheap to
throw away.

| Spec | Scope |
|---|---|
| 1 — Foundation | worldkit, atlas and pins, bible, spawn lint, reachability report (done) |
| **2 — Dressing (this)** | asset catalog and offline renderer, prop and tree writers with per-pass undo, prop placement checks, site templates, navmesh queries; pilot: five sites on the Oakenshire ring |
| 3 — Content pilot | spawns, NPCs and quests on the dressed ring (kobold cave, quarry, hunters, abbey approach) |

## 3. Decisions (from brainstorming, 2026-10-01)

| Topic | Decision |
|---|---|
| Autonomy | **Direct writes, tracked per pass.** The agent writes real `.wobj` and `.hfol` data. Each pass records exactly what it wrote, and one command undoes a whole pass. The user fixes single props in the editor. |
| Scope | **Dressing only.** Spawns and quests for the ring are spec 3. |
| Seeing assets | **Offline Python renderer.** Contact sheets, multi-view sheets and perspective site previews, textured where the base texture can be resolved. |
| Waterfall | **Dress around the missing water.** Rocks, basin and herbs; the site is marked "awaiting water". No water tech in this spec. |
| Trees | **Into the per-page `.hfol` foliage files**, with exact per-instance undo. |
| Architecture | **Python first, plus one small C++ CLI** (`nav_query`) wrapping `nav::Map` for walkability. The navmesh is rebuilt with the existing `nav_builder`. |

## 4. Components

All Python lives in `tools/world/` and extends `worldkit` from spec 1.

### 4.1 Asset catalog: `worldkit/assets.py`

- Scans `data/client/Models` for `.hmsh` and `.hwmo` files.
- For each mesh (`.hmsh`), it parses:
  - `MESH` for the version;
  - `VERT` for the bounds (V2 stride 64 bytes, position first);
  - `COLL` only for presence: a mesh has collision exactly when this chunk exists;
  - `SUBM` for submesh count and material names.
- For each WMO (`.hwmo`), it reads the bounds from `MOHD` (6 floats at header offset 32) and lists the
  meshes it references.
- It derives:
  - `footprint` (the half extents of the X/Z rectangle) and `radius`;
  - `height`;
  - `base_offset`: how far the model's origin sits above its lowest vertex.
- **Material resolution.** Each material name is resolved, best-effort, to a base-colour texture
  (`.htex`), via the `.hmi` parent chain to the `.hmat` texture parameters. The format knowledge comes
  from `mmo-material-editor/scripts/htex_tool.py`. If resolution fails, the asset is drawn plainly
  shaded, with no error.
- **Cache:** `generated/world/assets/catalog.json`, keyed by each file's path, size and mtime plus a
  hash of the parser code. Only changed assets are re-parsed.
- **Tags:** `tools/world/asset_tags.json` (tracked, hand-edited) maps glob patterns to tags and
  placement defaults. Later rules override earlier ones, and the user's edits always win. Example:

  ```json
  {"version": 1, "rules": [
    {"match": "Models/Desert/Rocks/*", "tags": ["rock"], "scale": [0.6, 1.6], "align_to_slope": true,
     "max_slope": 90, "sink": 0.4, "may_overlap": ["rock"]},
    {"match": "*/SM_hc_Camptent*", "tags": ["tent", "camp"], "max_slope": 10, "sink": 0.05}
  ]}
  ```

  Tag vocabulary for v1: `rock`, `cliff_rock`, `rubble`, `ruin`, `wall`, `tower`, `building`, `tent`,
  `camp`, `fire`, `crate`, `barrel`, `cart`, `tool`, `fence`, `light`, `tree`, `tree_dead`, `bush`,
  `plant`, `herb`, `mine`, `bridge`, `pier`, `dungeon`, `food`, `furniture`, `prototype`.
  Prototype and dungeon-interior assets are never picked by templates unless a template names them.
  - **Per-tag placement defaults:**
    - `sink` (metres the base may go into the ground);
    - `max_slope`;
    - `align_to_slope`;
    - `may_overlap`;
    - `collides_override` (foliage only).

### 4.2 Offline renderer: `worldkit/meshrender.py`

- A numpy rasterizer with a z-buffer, Lambert lighting from the north-west, nearest-neighbour texture
  sampling of the resolved base texture, and back-face culling consistent with the engine's front-face
  convention. Plainly shaded when no texture resolves.
- **Outputs:**
  - **Contact sheets.** One per model folder, at `generated/world/assets/sheets/<folder>.png`. Each tile
    is 128 px, uses a fixed 3/4 camera and is labelled with the file name, size in metres, and a
    collision marker.
  - **Asset views.** Front, side and top views of one asset, with a 1 m grid.
  - **Site previews.** Terrain (heights from the snapshot, coloured by splat kind and water) plus all
    props and trees within a radius, from 2–3 perspective cameras around a site.
  - **Previews for drafts.** The same previews with the planned items, showing new items with a subtle
    outline.
- **Determinism:** same inputs, same image.
- Quality target: tell a tent from a cart, a tower from a wall, a rock pile from a crate. Not
  engine-accurate lighting.

### 4.3 Writers: `worldkit/formats/wobj.py`, `worldkit/formats/hfol.py`

**`.wobj` writer**

- Writes version 3: `WVER` (uint32 3), then a `WMSH` chunk (mesh) or a `WWMO` chunk (WMO), in the
  layout spec 1's parser reads.
- Path: `Entities/<pageIndex>/<uniqueId>.wobj`.
  - `pageIndex = (px << 8) | pz`, where `px = floor(x / PageSize) + 32` and likewise for `pz`.
- `uniqueId` is a random 64 bit, the editor's `EntityFactory::GenerateUniqueId` scheme. It is checked
  against every id already on disk.
- Name and category: empty name; category `dressing/<template>`, so the editor's scene outline groups
  a pass's props.

**`.hfol` reader and writer** (`FVER`, `FMSH`, `FINS`; layout as in
`src/shared/game_common/world_foliage.cpp`)

- **Append:** keeps the existing mesh names and instances in order and adds new mesh names and new
  instances at the end. This matches mmo_edit's own writer order, so a later editor save does not
  reshuffle the file.
- **Undo:** removes instances by id, and drops a mesh name only when no instance uses it any more,
  remapping mesh indices.
- A version-1 file (no collision byte) is written back as version 2, as the editor does.

### 4.4 Dressing passes: `tools/world/dress.py` (CLI) and `worldkit/dressing.py`

| Command | Effect |
|---|---|
| `dress.py plan --poi ID --template NAME [--seed N] [--pass-id ID]` | Places the template on a pin. It writes a draft and a before map, runs the placement checks and renders previews. Nothing in the world changes. |
| `dress.py check PASS` | Re-runs the placement checks and re-renders after the agent edited the draft by hand. |
| `dress.py apply PASS` | Writes the world files and the manifest, builds the scratch navmesh, runs the walkability checks and writes the review packet. |
| `dress.py undo PASS [--force]` | Removes exactly what the pass wrote. |
| `dress.py list` | Lists passes with their status: planned, applied, undone, or applied with walkability unchecked. |
| `dress.py publish-nav` | Rebuilds the real navmesh (`data/editor/nav`) with `nav_builder`. It runs only on explicit request. |

**Pass id:** `<yyyymmdd>-<poi>-<n>`.

**Locations:**

- Drafts, previews and scratch output: `generated/world/passes/<pass-id>/`.
- Applied manifests: `data/world/passes/<pass-id>.json` (tracked, main repo).

**Draft and manifest format** (UTF-8, 2-space indent, floats rounded to 0.01):

```json
{
  "version": 1, "pass_id": "20261001-south_ridge_quarry-1", "map": 0, "world": "Development",
  "poi": "south_ridge_quarry", "template": "quarry", "seed": 7, "created": "2026-10-01T18:00:00",
  "world_fingerprint": "<hash of tiles + entities + foliage at plan time>",
  "items": [
    {"role": "stone_pile", "asset": "Models/Desert/Rocks/SM_Rock_12.hmsh", "store": "wobj",
     "position": [181.2, 27.43, 806.9], "yaw": 34.0, "tilt": [2.1, -0.8], "scale": 1.2,
     "unique_id": "0x3f2a...", "file": "Worlds/Development/Development/Entities/8225/....wobj",
     "written_hash": "<sha1 after apply>"}
  ],
  "checks": {"placement": [], "walkability": []},
  "status": "applied", "nav": {"built": "2026-10-01T18:02:11", "path": "generated/world/nav/Development"}
}
```

- Each item has a `store` of `wobj` or `hfol`.
- `yaw` is in degrees.
- `tilt` is pitch and roll for slope alignment.
- A `.wobj` uses position, quaternion rotation and a scale vector; the manifest stores the readable
  form, and the writer converts.

### 4.5 Site templates: `tools/world/templates/<name>.json` and `worldkit/templates.py`

A template is a list of roles. Each role has:

| Field | Meaning |
|---|---|
| `query` | Tags required or excluded, an optional size class (`small` < 1.5 m, `medium` < 4 m, `large`), and an optional asset allow-list |
| `count` | `[min, max]` |
| `spacing` | Minimum distance to items of the same role, in metres |
| `store` | `wobj` or `hfol` |
| `rule` | Placement rule, below |
| `scale` / `yaw` | Optional ranges |

The placement rules:

- `at_anchor`: an exact spot, for example the teleport or the pin centre.
- `ring`: around the centre, between radii r1 and r2.
- `scatter`: anywhere in the pin, using Poisson-disc sampling, never a grid.
- `cluster`: n items close together around a seed point.
- `against_cliff`: on cells next to terrain steeper than a threshold, facing away from it.
- `along_path`: offset from the nearest atlas road or painted path.
- `near_role`: within a distance of another role's items.

The placer:

- is deterministic for a given seed;
- tries several candidate positions per item and keeps the first that passes the placement checks;
- records unfillable roles, either because no asset matches the query or because no valid position
  exists. Those become **asset gaps** or **space gaps** in the packet, never silent drops.

The pilot templates are `quarry`, `cave_mouth`, `waterfall_basin`, `hunting_camp` and
`abbey_surround`, described in §7.

### 4.6 Placement checks: `worldkit/prop_lint.py`

These run on every item, checking its rotated footprint rectangle rather than only its centre. Each
rule's threshold can be relaxed by the item's tags.

| Rule | Severity | When |
|---|---|---|
| `prop_floating` | error | Any footprint corner is more than 0.3 m above the terrain, beyond the tag's `sink` allowance |
| `prop_buried` | error | Base below the terrain by more than `sink` + 0.3 m at the footprint centre |
| `prop_slope` | error | Maximum slope under the footprint exceeds the tag's `max_slope` |
| `prop_overlap` | error | The footprint intersects another new or existing prop or a WMO's footprint, unless allowed by `may_overlap` |
| `prop_on_road` | error / warning | Error for a colliding prop on painted road or within 3 m of an atlas road line; warning for a non-colliding prop |
| `prop_in_water` | error | Base under more than 0.3 m of water (except `pier` and `bridge`) |
| `prop_over_hole`, `prop_terrain_edge` | error | As for spawns |
| `prop_on_spawn` | warning | The footprint covers an existing active spawn point |
| `prop_outside_poi` | warning | Outside the pin's radius or polygon |
| `prop_grid_pattern` | warning | Spec 1's grid detector, applied to same-asset groups |

- **Existing props** are linted once by the new `props` domain of the content audit. Today's findings
  go into a `props` section of `tools/world/lint_baseline.json`, so only new errors fail.
- **Footprints:** a `.wobj` item's footprint is its catalog footprint times its scale, rotated by its
  yaw. This replaces spec 1's per-folder default radius wherever the catalog knows the asset.

### 4.7 Navigation: `src/tools/nav_query/` and `worldkit/nav.py`

- **`nav_query`** is a C++ console tool, built with `MMO_BUILD_TOOLS`. It loads a nav directory
  through `nav::Map` and answers JSON lines on stdin:
  - `{"op":"path","from":[x,y,z],"to":[x,y,z]}` → `{"ok":true,"length":123.4,"points":[...]}` or
    `{"ok":false}`;
  - `{"op":"on_mesh","at":[x,y,z],"radius":2}` → `{"ok":true,"nearest":[x,y,z],"distance":0.4}`.

  It follows project code style and needs no game data beyond the nav files.
- **`worldkit/nav.py`:**
  - runs `nav_builder -d data/client -w Development -o generated/world/nav` for the scratch build;
  - wraps `nav_query`;
  - implements the walkability checks.
- **Walkability checks**, run on the scratch build after apply:

  | Rule | Severity | When |
  |---|---|---|
  | `site_unreachable` | error | The site's entry point (template `entry` role or the pin centre) has no path from the nearest point of the protected-route network, or the path is longer than 3× the straight line |
  | `route_broken` | error | A protected route no longer has a path |
  | `route_longer` | warning | A protected route got more than 25% longer than its stored baseline length |
  | `spawn_off_mesh` | error | An active spawn within the site radius is more than 1.5 m from the navmesh |

- **Protected routes:** `tools/world/nav_routes.json` (tracked) lists named point sequences, each
  with its baseline length:
  - Oakenshire Town Hall → West Gate;
  - West Gate → Old Stone Bridge;
  - Barrowfront Camp → abbey teleport (107, 30, 345);
  - Oakenshire → each confirmed ring site.

  `dress.py nav-baseline` records the lengths.

### 4.8 Review packet

`generated/world/review/<pass-id>/` contains:

- `README.md` (template, seed, item table, check results, asset and space gaps, "awaiting water" and
  similar notes, how to undo);
- `before.png` / `after.png` (spec 1 renderer with `--diff`);
- `preview_1..3.png` (site previews);
- `manifest.json`.

A batch of passes also gets a `generated/world/review/<batch>/README.md` index.

## 5. Safety rules

1. **The editor must be closed.** `apply`, `undo` and `publish-nav` refuse while `mmo_edit.exe`
   runs, because mmo_edit keeps loaded pages in memory and its save would overwrite `.hfol` files.
   This also covers the editor only discovering new `.wobj` files at startup.
2. **Fingerprint.** `apply` refuses when the world fingerprint differs from the draft's; `check`
   re-fingerprints after the agent re-validates.
3. **Undo conflicts.** `undo` compares each `.wobj` file's hash and each foliage instance's
   position, rotation and scale with the manifest. Changed items are reported and kept unless
   `--force`. Missing items are reported as already removed.
4. **No commits to `data/client`.** The agent never commits `data/client`: passes may touch `.hfol`
   files that hold the user's uncommitted edits. The user commits accepted passes. Manifests in the
   main repo are committed on the feature branch.
5. **The real navmesh is the user's.** It is only rebuilt by `publish-nav`; checks use the scratch
   build.
6. **Navmesh build failure.** If `nav_builder` fails after files were written, the pass is
   `applied (walkability unchecked)` and the packet says so; `dress.py check PASS --nav` retries.
7. **Instance maps.** Passes refuse instance maps (map 1 is a WMO interior with no terrain).

## 6. Skill integration

- A new skill, `.agents/skills/world-dresser/`, contains:
  - `SKILL.md`, with a `<world_aware_design>` section like the quest and NPC skills;
  - the workflow: plan → look at the previews → refine the draft → check → apply → packet;
  - a guide to writing templates;
  - tag-editing rules.

  It is linked into `.claude/skills` by `tools/sync_skills.ps1`.
- `tools/world/README.md` gains the new commands and files.
- The bible's ring dressing palette (§3.2) is the style reference the skill points to.

## 7. Pilot: five passes on the Oakenshire ring

| Site | Pin (confirmed) | Template | Intent |
|---|---|---|---|
| South Ridge quarry | `south_ridge_quarry` (185, 800) | `quarry` | A working quarry at the cliff foot: cut-rock piles against the cliff, crates, a cart, a tent or shed, tools, a fire. Separate from the bandit camp at (235, 758): it keeps 20 m clear of the bandit tents. |
| Kobold cave mouth | `hidden_cave` (421, 644) | `cave_mouth` | A hidden-looking opening at the east cliff: large cliff rocks framing a gap, rubble, bushes partly screening it. Entry point = the gap. Kobolds and the chest belong to spec 3. |
| Waterfall basin | `ring_waterfall` (427, 763) | `waterfall_basin` | Cliff rocks around a basin-shaped spot at the cliff foot, herb plants nearby, a few trees. The packet marks it "awaiting water". |
| High Ridge hunting grounds | `high_ridge_hunting_grounds` (250, 320) | `hunting_camp` | A small hunters' camp: one or two tents, a fire, barrels or crates, a few trees on the plateau. |
| Hollow Choir abbey | `dungeon_entrance` (103, 346) | `abbey_surround` | Ruined walls, foundation pieces and rubble around the existing teleport (area trigger 4 at (107, 30, 345), radius 2), with a tower stand-in for the bell tower. The teleport circle stays clear and walkable. The prototype Cylinder marker at (107, 345) stays in place until the user removes it. |

**Expected asset gaps**, reported as art requests:

- an abbey or church ruin;
- a bell tower;
- a cave entrance mesh;
- pines;
- waterfall art.

Stand-ins are allowed: `Building_Tower_0x` for the bell tower, and large rocks framing the cave.

## 8. Acceptance

1. `dress.py` plan, check, apply, undo and list work on the real Development world.
2. All five pilot passes are applied with zero new placement errors and passing walkability checks:
   - every site is reachable;
   - every protected route is intact;
   - no spawn is off the mesh.
3. Each site has a review packet with before/after maps and at least two perspective previews.
4. `undo` of a test pass restores every touched `.hfol` file byte for byte and removes its `.wobj`
   files.
5. The user has walked all five sites in mmo_edit and kept, fixed or undone each. An undone site
   still counts as a completed review.
6. Tool tests and the gate are green.

## 9. Testing

**Python unit tests** (`tools/tests/test_world_dressing_*.py`), on synthetic fixtures built by
extending `world_fixtures.py`:

- `.hmsh` bounds, submesh and material parsing, and `COLL` detection;
- `.hwmo` `MOHD` bounds;
- catalog cache invalidation;
- tag rule precedence;
- `.wobj` write → spec 1 reader round trip (mesh and WMO, rotation and scale);
- `.hfol` read → append → undo gives back the original bytes (v2, and v1 upgrade);
- mesh-index remapping when a mesh name is dropped;
- manifest write and read; undo conflicts (changed file, moved instance, missing file);
- every placement rule, on a fixture with a slope, a road, water, a hole and an existing prop;
- template placer: determinism per seed, Poisson spacing, unfillable roles reported as gaps;
- renderer: deterministic, non-empty images, and a known triangle covers the expected pixels;
- the editor-running guard (process probe injected).

**Live-data tests** (`requires_live_data`):

- the catalog builds over `data/client/Models` and finds collision on a known mesh;
- `nav_query` finds a path Town Hall → West Gate on the scratch build;
- a plan for every pilot template on its pin yields no errors.

**C++:** `nav_query` builds in the gate's tools build. Its logic is a thin wrapper over `nav::Map`,
which has its own tests.

**Gate:** all of the above runs in `tool_tests`; the weekly content audit gains the `props` domain.

## 10. Out of scope

- Water and waterfall technology, and new art assets.
- Terrain sculpting and texture painting.
- Spawns, NPCs, quests and the kobolds (spec 3).
- Ghost previews or a dressing UI in mmo_edit.
- Automatic publishing of the real navmesh.
- `.hpak` packaging of new entity files.
- Dressing instance maps.

## 11. Risks

- **Material resolution** through `.hmi` and `.hmat` graphs may fail for many assets. Mitigation:
  plain-shaded fallback, and a count of unresolved assets in the catalog summary.
- **Navmesh build time** for the whole Development world is unknown. If it is slow, `nav.py` builds
  only the pages a pass touched plus their neighbours, if `nav_builder` allows a page subset.
  Otherwise the whole world is built once per batch of passes.
- **Asset scale.** Some meshes may be authored at unusual scales. The catalog reports their size in
  metres so a mis-scaled asset is visible on the contact sheet, and tags can carry a corrective scale.
- **Editor coordination.** Props written while the user has mmo_edit open would be lost or invisible;
  the guard refuses, and the skill tells the agent to ask the user to close the editor.
