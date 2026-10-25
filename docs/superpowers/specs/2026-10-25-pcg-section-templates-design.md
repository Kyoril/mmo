# PCG Section Templates — Design (phase 2, step 1)

Status: design, awaiting approval (2026-10-25)
Builds on: `2026-10-24-pcg-oasis-poc-design.md` (PoC, on `feature/pcg-oasis-poc`)
Research: `docs/superpowers/research/2026-10-25-unreal-pcg-architecture.md`,
`docs/superpowers/research/2026-10-25-korash-decomposition.md`

## Intent

The oasis proof of concept showed that rule-driven generation with non-destructive terrain
edits, ownership and determinism works in this engine. It also showed the limit of rules written
in C++: quality comes from hand-made pieces, and rules alone produced "a tidy ring in empty sand".

The user's model for the real system (their words, condensed):

> Build sections by hand (to ensure quality). Then scale them using graphs with the support of
> control actors. That's basically it.

Example: build one road section by hand — deform the terrain, paint it, place foliage, place
lanterns and fences here and there — save it as a reference asset, and have a graph lay that
section along every tagged spline, with seeded randomness.

The Korash analysis supports this: the canyon look is stepped terrain (median 40 m local relief)
skinned with sunk cliff meshes; that is exactly the kind of piece that is good when made by hand
and impossible to get right from generic rules.

Unreal has the placement half of this (`PCGLevelToAsset`, "Load PCG Data Asset") but cannot write
the classic landscape from PCG at all in 5.8; terrain-carrying sections are our own extension.

This document covers **step 1 of phase 2: section templates, proven with a road along a spline.**
Data-driven control types, the graph asset and node editor, and the full desert set come later and
build on it (see Roadmap).

## User decisions

- **Hand-built sections are the source of quality.** A section contains terrain shape, terrain
  paint, foliage and placed props (entities, incl. lanterns/fences); the graph places sections.
- **Sections live in their own mini-world**, not a real map with full page/tile support. It has its
  own local coordinate system: the **origin is the start of the centre line** (the first spline
  point), **forward is local +Z**.
- **Sections bend and repeat along the spline** (like a spline mesh): terrain and props follow the
  curve and rise with it; copies repeat end to end.
- **Sections are editor-only assets.** Like everything PCG, they never ship to clients; they only
  modify real worlds, which are shipped.
- **First proof: a road along a spline**, in the style of the Oakenshire/Briarwatch roads. Roads
  are also a feature the user needs in their own right, so the road must be production-worthy, not
  only a demo of templates.
- Carried over from the PoC: everything a generator produces is owned by it (delete removes all,
  incl. terrain changes); deterministic with seeds; editor-only; tooling in C++ (`mmo_edit` UI +
  unattended flags), Catch2 tests; nothing reaches develop before the user has judged the result.

## Goals

1. **Author a section** in a section mini-world using the normal world-editor tools (sculpt,
   paint, foliage, entity placement), and define one or more **variants** of it.
2. **Bake** a section mini-world into a **section asset**: captured terrain (heights relative to the
   centre line, splat), placements (entities and foliage instances in local space) and per-variant
   metadata. Baking is deterministic and repeatable.
3. A new control object **Section Path**: an open 3D spline (the existing shared spline editing with
   tangents) plus a section asset, seed and a few parameters.
4. **Rebuild** lays the section along the spline: picks a variant per copy (seeded), bends terrain
   and props along the curve, blends joins, applies per-prop variation, and writes everything
   through the existing non-destructive terrain contribution and placement reconciliation.
   **Delete** removes all of it.
5. Same guarantees as the PoC: rebuild twice → identical files; moving a spline point re-lays only
   the two spans it touches; data-repo-friendly deterministic records.
6. A road section built from existing assets (FalwynPlains road paint, `Lantern`, `SM_Fence_0x`)
   laid along a test spline in a meadow area of map 0, compared by the user against the existing
   hand-made Oakenshire/Briarwatch roads.

## Non-goals (this step)

- Graph asset / node editor; data-driven control types and property groups (next step).
- Closed-loop application (rim sections around a loop), area fill, stamping a section once at a
  point — the oasis keeps its C++ rules for now.
- Start/end caps and junction/crossing pieces; multiple sections on one spline.
- Water, holes, area ids and tile materials inside sections (paint uses the target tile's
  material layers, see "Paint").
- Deforming meshes themselves (props are moved/rotated, never bent).
- Overlap handling between generators beyond the PoC's "refuse overlapping influence".
- Navmesh, minimap and far-terrain LOD refresh (reported, run by hand as today).

## Anti-goals

- Anything section- or PCG-related reaching clients or servers.
- A template that only looks right on a straight, flat spline (bending and rising are the point).
- Visible steps or cracks at the joins between copies, or at the sides where a section meets the
  world terrain.
- Non-determinism (same rules as the PoC: hash-based randomness keyed by copy and prop, no
  `std::` distributions, no unordered containers on output paths).

## Design

### Section mini-world (authoring)

- A section mini-world is an ordinary editor world (`.hwld` + pages + entities + foliage), stored
  under the non-shipped editor folder: `data/client/Editor/Pcg/Sections/<Name>/<Name>.hwld`.
  Reusing the world format means **every existing world-editor tool works unchanged** — no new
  sculpt/paint/placement code.
- New worlds of this kind are created from the editor ("New Section…") with the 2×2 pages around
  world origin, flat terrain at height 0, a default terrain material, and an origin marker.
  World origin (0, 0, 0) is the section origin: the centre line starts there and runs along +Z;
  X is "across" (+X = right when looking along +Z), Y is up.
  *(Builder: verify the editor opens worlds outside `Worlds/`; modes that assume
  `Worlds/<name>/<name>.hwld`, such as spawns/atlas, may simply be unavailable for sections.)*
- **Variants:** a section world can hold several variants side by side. Each variant is a
  rectangle with its own local origin: `originX` (offset across, variants are built next to each
  other, e.g. 60 m apart), `length` (along +Z), `halfWidth` (across, to each side of the centre
  line) and `sideFalloff` (blend band beyond halfWidth where the section fades into world terrain).
  Variants of one section share length and width (so they are interchangeable); the editor shows
  the rectangles as overlays and lets the user add/move/remove them in a **Section** panel.
- **Prop variation rules** are authored in the Section panel per asset path (applies to every
  placement of that mesh in the section): keep chance (0–1), position jitter (m), yaw jitter (°),
  scale jitter (±), and an optional swap group (other meshes it may be replaced with). Defaults:
  keep 1, no jitter.

### Section asset (bake)

- **Bake** (button in the Section panel + `mmo_edit --bake-section <world>`) writes
  `data/client/Editor/Pcg/Sections/<Name>.hsec` (binary, chunked, versioned, deterministic):
  - per variant: a terrain grid in local space covering `[-halfWidth - sideFalloff,
    +halfWidth + sideFalloff] × [0, length]` at the terrain's outer-vertex spacing (and inner
    vertices), storing **height relative to 0** and **splat weights** (4 layers); a coverage mask
    marking where the author actually changed the terrain/paint vs. untouched flat default;
  - per variant: placements (asset path, kind entity|foliage, collides, local position relative
    to the variant origin, local rotation, scale, and **height above the section's own terrain**
    at that spot);
  - section metadata: name, length, halfWidth, sideFalloff, variant count, prop variation rules,
    the terrain material/layer names the paint refers to.
- The baked asset is the only thing the generator reads; the mini-world is the editable source.
  Re-baking after edits is explicit (like `--bake-terrain-lod`); the generator warns when the
  `.hsec` is older than its mini-world files.

### Section Path control object

- New control kind next to the oasis in the Procedural mode (list shows kind), stored like the
  oasis in `data/client/Editor/Pcg/Worlds/<world>/<id>.hpcg` with a generator-kind field (record
  format bump). Fields: id, name, seed, open 3D spline (Catmull-Rom with tangents), section asset
  path, parameters:
  - `variantMode`: random (seeded per copy) | sequence;
  - copies are fitted per control-point span (see "Resolved after the builder check", item 2);
  - `joinBlend` (m, default 2): terrain cross-fade between neighbouring copies;
  - `propDensity` (0–1, multiplies keep chance), `heightMode`: follow spline height (default) |
    follow ground (centre line height = base terrain under the spline).
- Created with "New Section Path" (2 points along the camera's forward on the ground) and
  `mmo_edit --pcg-new-path <world> --points x,z;x,z;... --section <asset> [--seed]`.

### Applying a section along a spline (generator)

Pure functions in `src/shared/pcg/` (testable headless), editor adapter as in the PoC.

- **Frame along the spline:** sample the spline by XZ arc length `s`; at each `s` a frame
  (position incl. y, forward = horizontal tangent, right = forward × up, up = world up). Sections
  stay upright; the spline's slope only lifts heights, it does not tilt props.
- **Copies:** fitted per control-point span (item 2 below): span `j` of length `S_j` holds
  `n_j = max(1, round(S_j / L))` copies of stretched length `L'_j = S_j / n_j`; copy `k` of span `j`
  covers that span's arc range `[k·L'_j, (k+1)·L'_j)`. Variant per copy from a hash of the span's
  endpoint ids and `k`. In the formulas below `s` is the arc position within the span.
- **Terrain:** for every world sample (outer/inner vertex, splat pixel) inside the path's influence
  (spline bounds + halfWidth + sideFalloff + margin), find the nearest spline point → `(s, v)` with
  `v` the signed distance across. Local section coordinates: `z = (s - k·L') · L / L'`, `x = v`.
  Sample the variant's grid at `(x, z)` (bilinear) → `h`, splat, coverage.
  - Height target = centre-line height(s) + h, where centre-line height comes from the spline
    (or the base ground under the spline in follow-ground mode).
  - Height weight = 1 within `halfWidth`, fading to 0 across `sideFalloff`; paint weight = paint
    coverage × the same falloff (item 1 below).
  - **Joins:** within `joinBlend` of a copy boundary, cross-fade the targets of the two adjacent
    copies (each copy's grid is sampled slightly beyond its end — the template is clamped at its
    ends for that).
  - Inner corners of tight bends where several spline parts are equally near: nearest wins
    (deterministic tie-break by `s`); a minimum bend radius warning is shown when the spline curves
    tighter than `halfWidth + sideFalloff`.
  - Written through `pcg::Apply` / `Unapply` exactly like the oasis (base' logic, compare-and-restore
    for splat), so hand edits, rebuild and delete behave as in the PoC.
- **Paint:** section layers are matched to the target tile's layers by texture (item 3 below);
  unmatched layers are skipped on that tile and reported.
- **Props:** each baked placement of copy `k` → local `(x, z)` → `s = k·L' + z·L'/L`, frame at `s` →
  world position = frame.position + right·x (+ forward·0), y = **final** ground at that point + the
  placement's height above the section terrain; rotation = frame yaw · local rotation (yaw only from
  the frame; local pitch/roll kept). Variation per placement from `Hash(seed, k, placementKey)`:
  keep chance × propDensity, jitter, swap group (one of {original} ∪ swapGroup, so the original
  stays possible; a swap takes its kind from the new asset, `.hwmo` = world model, and foliage stays
  foliage). Stable key = hash of the span's endpoint ids, `k`, the variant and the baked placement's
  key (the section object's unique id, not its index), so the PoC's reconcile keeps unchanged props
  untouched, also when another prop of the section is added or removed.
- **Open ocean:** ocean placeholder pages are materialised only where a section copy's influence
  rect overlaps them, not across the path's whole bounding box.
- Everything else (ownership, reconcile, record/terrain drift guard, page readiness, recipe-free
  validation, overlap refusal) is reused from the PoC unchanged.

### Code organisation

- `src/shared/pcg/section_asset.*` — data + deterministic read/write of `.hsec`.
- `src/shared/pcg/section_path.*` — the generator: frames, copy mapping, `EvaluateTerrain`-style
  target function and `BuildPlacements` for sections.
- `src/shared/pcg/section_bake.*` — pure part of baking (grid capture from an `ITerrainAccess` +
  placement list → section asset); editor adapter collects entities/foliage.
- `mmo_edit`: Section panel (variants, rules, bake) shown when the open world is a section world;
  "New Section…" command; Section Path in the Procedural mode; flags `--bake-section`,
  `--pcg-new-path`.

### Testing

- Pure tests: bake of a synthetic section (grid + props) round-trips; frame/copy mapping on
  straight, curved and rising splines (a prop at local (x, z) lands at the expected world spot);
  heights follow spline height; side falloff and join blend produce no step (max height difference
  across a join below a threshold on a synthetic section with matching edges); stretch-to-fit copy
  counts; variant choice deterministic; prop variation deterministic and keyed; moving the last
  spline point leaves copies before the changed part untouched.
- Editor/manual: author a road section mini-world (2 variants), bake, lay along a curving, rising
  test spline, rebuild twice (clean tree), delete (restored), capture screenshots next to an existing
  Oakenshire/Briarwatch road.

## Roadmap after this step (for orientation, not part of this step)

1. **Section stamping and loops:** place a section once at a point / control; apply rim sections
   around closed loops; fill areas → the oasis becomes "rim section + inner fill" instead of C++
   rules.
2. **Graph asset + node editor and data-driven control types** (property groups defined as data,
   tags, actor/spline/landscape inputs, difference/intersection by priority, the Unreal node
   vocabulary where it fits).
3. **Desert set** (per the Korash decomposition): background fill, nested plateau splines (stepped
   terrain + cliff skin sections), river spline with per-point water levels and falls, oasis as a
   hole in a rock field, path/opening subtraction, rock-field area.

## Resolved after the builder check (designer's calls)

1. **Height ownership inside the corridor.** A flat road bed at height 0 is exactly the untouched
   default, so a coverage mask must not gate height. Rule: within `halfWidth` the section **owns
   the height everywhere** (weight 1); across `sideFalloff` it blends into the world. Paint has its
   own coverage: a splat pixel is written only where the author's paint differs from the section
   world's default paint. The `.hsec` therefore stores heights for the whole rectangle and a
   separate paint-coverage mask.
2. **Locality: copies are fitted per control-point span.** Copies are not laid by arc length from
   the spline start (that shifts every copy after any edit). Each span between two control points
   gets `round(spanLength / L)` copies (at least 1), stretched to fit that span. Moving a point
   only re-lays the two spans it touches; inserting a point only re-lays the span it splits.
   Variant choice and placement keys hash the span's two endpoint ids (stable per point, stored in
   the record) and the copy index within the span, not a global copy number. `stretchToFit` is
   therefore always on for this step; `variantMode` stays.
3. **Paint across terrain materials: match layers by texture.** A section layer is mapped to the
   target tile's layer that uses the same texture (by the material's layer texture asset). On map 0
   that keeps grass, ground and stone_road working across `_Inst`, `_Forest` and `_Mountains`. A
   section layer with no matching layer on a tile is not painted there, and the rebuild report
   lists the tiles and layers it skipped. No refusal.
4. **Placements carry everything a world placement can carry.** Sections may contain plain meshes,
   world models (`.hwmo`, e.g. the wood lantern variants) with their prop-kit paths, and foliage
   instances. Mesh placements keep material overrides. Names and categories of generated entities
   come from the generator (as in the PoC). The PoC's `pcg::Placement` and reconcile are extended
   to these kinds, and a change of kind, kit or overrides counts as an asset change (remove and
   create).

## Calls made by the designer (check these)

- Section mini-worlds reuse the world format and the world editor (instead of a new editor) and
  live under `data/client/Editor/Pcg/Sections/`.
- Variants of one section are rectangles side by side in the same mini-world, sharing length and
  width.
- Props are moved and turned with the curve but stay upright; their height follows the final
  ground plus their authored height above the section terrain.
- Paint is matched by texture to the target tile's layers; unmatched layers are skipped and reported.
- Baking is an explicit step producing a `.hsec`; generators read only baked assets.
- Water, caps, junctions and loop/area application are left to later steps.

## Left to the builder

- Grid resolution details (outer + inner vertices vs. a single resampled grid), exact `.hsec` chunk
  layout, the bilinear/nearest sampling details, the default `sideFalloff`/`joinBlend` values.
- How the editor recognises a section world (marker file next to the `.hwld`, or path prefix).
- The test spline location in map 0 and the exact road section content (agent may build a
  placeholder section to verify plumbing; the user builds the real one).
