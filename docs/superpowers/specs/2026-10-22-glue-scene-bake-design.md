# Glue Scene Bake — Design

Date: 2026-10-22
Status: draft for review
Builds on: `2026-10-21-plaza-character-select-design.md` (the Haven glue scene).

## Intent

The login, realm and character screens show Haven's gate and plaza as a live 3D backdrop.
Today the client loads this backdrop by streaming the real world (`Worlds/Development`):

- terrain pages
- every entity file on those pages
- the full Haven town WMO with ~22 child buildings, ~860 mesh refs and prop kits
- foliage

That takes several seconds of black screen, then terrain, then buildings popping in. The user
finds this the worst part of the experience: "that all takes a LOT of load time".

The user wants a **build-time optimized** version instead: one model, or a few flat, merged
models, cut out of the existing assets. It contains only what the glue camera sees, with no
interiors and nothing streamed.

## Goals

- Add a **bake step in mmo_edit** (user rule: content tooling lives in the C++ editor, never in
  committed scripts). It turns the live Haven world plus the glue camera path into a small set of
  merged meshes, a baked terrain patch and a manifest.
- At runtime the client's `GlueScene` loads only that manifest and its assets:
  - no `ClientWorldInstance`
  - no page streaming
  - no `.wobj` or `.hfol` reading
  - no WMO preparation steps
- **Load target:** the gate view is ready in **< 1 s** after `LoginState::OnEnter` on a warm
  disk in a Debug build, and nothing pops in afterwards.
- The bake output looks like the live scene from every camera pose the glue screens use: the gate
  idle view with its drift, every point of the flight, and the plaza view.

## Non-goals

- Making Haven itself prettier (materials, set dressing). That is the separate Haven detail
  assessment; a rebake picks up whatever it changes.
- Changing the camera path, spots or characters. Those stay in `GlueScene.json`.
- Baked lighting. Sky, sun, environment profile and dynamic shadows stay live as today.
- A general "bake any area" tool for other uses. It may become one later; for now it only has to
  serve the glue scene.

## Anti-goals

- A bake that needs a manual chain of steps to reproduce. One command must rebuild everything.
- Visible differences between the baked and live scene from the glue camera: missing buildings,
  wrong materials, seams in the terrain.
- Committing large intermediate files or anything the runtime does not load.

## Design

### Bake input

- The world asset path and the camera path both come from `GlueScene.json`: `world`,
  `flightPath`, the gate view, the plaza view, spots, entrance and `flyOutSeconds`.
- mmo_edit does not link `game_client` today, where `ParseGlueSceneDefinition` lives. Move the
  parser and definition into a library both executables link (e.g. `game_common`), or link
  `game_client` into mmo_edit if that is clean. This is the builder's call; there must be exactly
  one parser.

### Visibility set

- **Camera samples:**
  - ~48 samples along the flight path (`EvaluateGluePath` over t ∈ [0,1])
  - the gate idle pose ± its drift amplitude
  - the plaza pose
  - the push-in end pose toward each spot
  - the visitor entrance path

  Each sample uses the client's field of view and aspect range: 16:9 up to 21:9, and 4:3 to be
  safe.
- **What is collected:** everything whose world bounds intersect at least one sample frustum,
  expanded by a small margin, out to a far distance. That covers:
  - terrain
  - WMO groups, recursively through child WMOs with their TRS
  - WMO doodads and prop kits
  - free-standing mesh entities from `.wobj`
  - foliage instances
- **Interiors are dropped:** WMO groups with the Interior flag and without the Exterior flag are
  never included.
- **Small-object cull:** anything whose largest projected size from every sample stays below
  ~2 px at 1080p is dropped.

### Output

The output goes to `data/client/Interface/GlueUI/GlueBake/`, or a similar folder next to
`GlueScene.json`. It contains:

- **Merged meshes.**
  - Geometry is merged **per material**, keeping material overrides, transformed into world
    space with the existing `AppendTransformedGeometry`, which handles mirrored transforms.
  - The geometry is split into **spatial chunks** so the octree can still cull and index counts
    stay sane. One `.hmsh` per chunk and material bucket, with 32-bit indices where needed.
  - Reuse the merge helpers in `world_model_instance_merge.cpp`: move `ReadSubMesh`,
    `MaterialBucket` and `CreateMergedMesh` out of the anonymous namespace into a shared
    header.
  - Meshes that cannot be merged (skinned, or an unusual vertex layout) are copied as-is, as
    single entities with a transform in the manifest.
- **Foliage.**
  - Trees are emitted as instances (mesh + transform) in the manifest, not merged, because
    their materials may depend on instancing or wind.
  - The client creates plain entities for them; there are only a few dozen in view.
  - If they render correctly as merged static geometry, merging them is allowed.
- **Terrain (user decision: option A, keep the near ground crisp).**
  - **Geometry:** the visible pages are clipped to the visibility area and emitted as baked
    meshes. Heights and normals come from the real page data, at full resolution near the
    camera samples. Far away they may be decimated, as long as the camera path shows no
    visible seams.
  - **Near tiles** (the plaza and the gate street, i.e. tiles within a near distance of any
    camera sample):
    - They keep the **real layered terrain material**: the same per-tile material, layer
      textures and normal maps the live terrain uses, so the cobblestones stay as crisp as
      today.
    - The tile's small blend data (layer coverage, and vertex colours if used) is baked into
      the mesh or into small per-tile textures, in whatever form the terrain material reads
      it.
    - No terrain paging and no `Page` objects at runtime.
  - **Far tiles:** colour is baked into an albedo texture per chunk with the existing `.tlod`
    approach (a top-down orthographic deferred render of only the terrain node, read back from
    albedo + emissive), using the `AlbedoNormal_Opaque_Base` instance that far terrain uses.
  - **Holes** stay holes.
  - **Near versus far:** the boundary must not show a visible colour or brightness jump from the
    camera path. Place it where the camera never looks closely, or blend it.
  - **Sharpness versus speed:** if crispness and the < 1 s target conflict, crispness on the
    near tiles wins and the far tiles get coarser.
- **Lights, sounds and particles.**
  - WMO and prop-kit point lights (e.g. lanterns), ambient sound emitters and particle emitters
    on doodads and kits that are in view, or audible from the camera path, are written to the
    manifest with their parameters and world transforms.
  - The runtime recreates them, so the baked scene is not darker or quieter than the live one.
- **Water.**
  - Visible water surfaces, such as the river under the bridge, are emitted as flat meshes with
    their water profile's surface material.
  - If that material only works through the terrain/water system, the builder reports it and
    keeps the water out of the bake rather than faking it. The user then decides.
- **Manifest.** A small JSON file, `GlueBake.json`, that lists:
  - the meshes with an optional transform
  - the foliage instances
  - the terrain chunks
  - the bounds
  - the bake date and the input hash of `GlueScene.json` + the world files, so a stale bake can
    be detected

### Runtime

- `GlueScene` loads `GlueBake.json` if present. It creates entities for every listed mesh under
  its world root, with no rendering distance limit or a generous one, and keeps the sky,
  environment, camera director, avatars and nameplates exactly as today.
- **Readiness:** the gate view is ready once all bake meshes and their textures are created.
  The loading can be done synchronously on enter, or in a few steps across the first frames,
  as long as the target holds.
- **Fallback:** if the manifest is missing, or its recorded hash of `GlueScene.json` does not
  match the current file, log a warning and fall back to today's world streaming, so a camera
  edit without a rebake still works in development.
- **Staleness of the world input (user decision: option b, warn only):**
  - The manifest also records a fingerprint of the world input files the bake read: the
    `.wobj`, `.hfol`, WMO, kit and page files it actually used.
  - A small checker run by the fast gate and by the weekly content audit compares that
    fingerprint and prints a **warning, not a failure**, naming the changed inputs and the
    rebake command.
  - The client does not check the world fingerprint at runtime, since that would mean reading
    the files the bake exists to skip.
  - Add a reminder line where Haven is edited (the world editor's save path for that map, or
    the relevant docs) that the glue bake should be rerun.
- **Plaza characters:** they keep their authored spot positions. No ground queries against the
  bake are needed.

### Tooling

- `mmo_edit --bake-glue-scene <path to GlueScene.json>` follows the existing headless job
  pattern:
  - log to a file
  - exit code 0 or 1
  - the world opened through `OpenAsset` and `WorldEditorInstance`, with pages force-loaded the
    way the minimap bake does
- A menu entry or button in the editor runs the same bake.
- It prints a summary: triangle count, number of meshes and materials, texture memory, number
  of interior groups skipped, and anything dropped or copied unmerged.

## Constraints

- Project rules apply (CLAUDE.md): code style, no exceptions, tests for headless logic.
- Engine and content tooling goes into C++ mmo_edit only.
- The bake output is committed in the `data/client` submodule (its branch and gitlink as
  usual). Keep it lean; note the total size in the report.
- Client threading rules apply (`docs/threading.md`) if any loading moves to a worker.
- Do not break the in-world streaming path or `WorldState`; the bake only reads world data.

## Left to the builder

- The exact chunk size, far distance, small-object threshold and texel densities, within the
  targets above.
- Whether terrain decimates with distance.
- How the visibility test samples aspect ratios.
- Where the shared parser and merge helpers live.

## Verification

- **Unit tests (headless):**
  - frustum sampling along the path
  - visibility collection on synthetic boxes
  - the interior filter
  - the manifest round-trip
  - the stale-hash detection
- **Bake run:** on the real Haven world, record the summary numbers.
- **Client check, with launch options only (the user plays on the other monitor):**
  - the gate-ready time before and after
  - screenshots of the gate view, mid-flight and the plaza, side by side with the live
    streaming version
  - the user approves the look
