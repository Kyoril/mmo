# Content cooking for client releases — design

Date: 2026-10-31. Status: draft for review.

## 1. Intent

The repository's `data/client` is the *editable* form of the game's client content: one file per
entity placement so several people can work on one map without merge conflicts, material graphs
and collision recipes the editor needs, shaders for whatever platform the author happened to save
on. Players should get a *prepared* form of the same content instead. A "cooking" step turns the
first into the second while a nightly release is built.

Players never notice cooking: they start the launcher, it patches, the game runs. Success means the
shipped content is correct for every platform and cheaper to load, while editing and local
development keep working exactly as they do today.

The immediate driver is the macOS client (2026-10-31): materials must ship Metal shaders that are
compiled ahead of time and always match the material graph, without the Windows editor having to
produce them. Stripping editor-only data and packing entity placements per terrain page follow.

## 2. Goals

1. **Release shaders are always rebuilt.** Every nightly rebuilds the shaders of every material
   for every shipped platform (D3D11 and Metal) from its material graph. Whatever shaders the
   editor stored in the repository do not matter for what players get.
2. **Metal ships precompiled.** Cooked materials carry Metal libraries compiled with Apple's
   tools (`.metallib`), not Metal source compiled at load time.
3. **Editors and development clients keep themselves current.** The editor compiles the shaders
   of the platform it runs on when it saves a material. Editor and development client detect
   missing or stale shaders of their own platform when loading a material and compile them on the
   spot. The editor stores them in the `.hmat`; the development client keeps them in a local cache
   outside version control (5.4).
4. **Release clients never compile.** A shipped client that finds no shaders for its platform in
   a material aborts with an error that names the material.
5. **Editor-only data is stripped** from shipped files: the material graph chunk `GRPH` in `.hmat`,
   the collision recipe chunk `CSRC` in meshes, and any other editor-only chunk the builder's audit
   finds.
6. **One entity file per page in cooked builds.** The `.wobj` placements of each page are packed
   into a single per-page file; the individual `.wobj` files are left out of the patch.
7. **Cooking code is reusable C++**: a library (`src/shared/content_cook/`, linkable from the editor
   and other tools later) plus a thin console tool `mmo_cook` on top, the way `nav_build` and
   `nav_builder` are split.

## 3. Non-goals

- A macOS editor. `mmo_edit` is D3D11/Windows only today. The design only requires that the
  editor's save and load paths work per platform, so a future macOS editor writes Metal.
- Cooking anything for the servers. World server and nav builder keep reading uncooked data
  (`.wobj` included); cooking changes the player patch only.
- Precompiling the engine's built-in shaders (deferred passes etc., converted by `hlsl2msl` at
  build time and still compiled from Metal source at load time). Worth doing, but a build-system
  change, not cooking. See section 9.
- Changing how the editor stores data. The repository keeps one `.wobj` per placement, graphs in
  `.hmat`, and so on. The editor never writes per-page entity files.

## 4. Anti-goals (failure even if it technically works)

- Local development gets slower or needs a cook step before the client can run from the repo.
- Cooked builds behave differently from what developers run and nobody can reproduce it locally.
- A shader that fails to cook blocks the whole nightly (see 5.6: warn and continue).
- Cooked output depends on the machine or run (non-deterministic files make every nightly patch
  re-download unchanged content).

## 5. Design

### 5.1 Material shader freshness

Decided with the maintainer:

- When the editor saves a material it computes a **graph hash** and stores it.
- Each platform's shaders in a `.hmat` record the graph hash and the **material compiler version**
  (an engine constant) they were compiled with.
- On load, freshness is a comparison of stored values against the current graph hash and the
  current constants. Nothing is regenerated or compiled just to check.
- Changing the material compiler's output in general means bumping the constant, which makes every
  material stale at once.

Calls made by the designer (not discussed in detail; change if wrong):

- The graph hash covers semantics only: nodes, their settings and values, links, and material
  parameters. Node positions and other layout do not count, so tidying a graph does not make it
  stale.
- Material functions (`.hmf`) count too, without making loads more expensive: the editor stores a
  hash in each `.hmf` when it saves it, and each platform's shaders record the hash of every
  function they were compiled with (recursively). On load, those recorded hashes are compared with
  the hashes stored in the function files. That means reading a stored value, not parsing a graph.
  Editing a function makes every material using it stale without re-saving those materials.
- There is one shared generator version plus one version per platform backend (D3D11 compile,
  HLSL→Metal translation and Metal compile). A fix to the Metal path only invalidates Metal shaders.
  Each platform's shaders store both numbers.
- These values live per platform next to the existing per-platform shader groups of the `.hmat`
  format (v0.5+), introduced with a new material format version. Older files simply have no
  values, and so count as stale wherever a graph is available to rebuild them.

The constants need a bump whenever generated code changes. No tool can detect that reliably, so a
rule goes into CLAUDE.md next to the protocol version rule.

### 5.2 Shared material graph compiler

Today the graph (`material_graph.cpp`, `material_node.cpp`, `material_function*.cpp`, node registry)
lives in `src/mmo_edit/editors/material_editor/` and drags in ImGui and the node editor, which is why
`metal_material_baker` compiles ImGui sources. The loading, hashing and compile parts move into a
shared library without UI dependencies (for example `src/shared/material_graph/`). The editor keeps
its UI on top of it. Editor, development client and `content_cook` all compile graphs through this
one library.

### 5.3 Editor behaviour

- **Save:** compile the graph for the current platform (`shader_platform::GetCurrentPlatform()`),
  store those shaders with graph hash and versions, keep other platforms' shaders untouched.
- **Open/load:** if the current platform's shaders are missing or stale, compile and keep them; the
  file is written back the next time it is saved (or immediately, builder's choice).

### 5.4 Development client vs release client

- A CMake option (designer's call, e.g. `MMO_MATERIAL_RUNTIME_COMPILE`) decides whether a client may
  compile materials. It is ON for local and Debug builds and OFF for nightly release builds.
  "Release configuration" alone is not enough, because nightly clients are Release builds with dev
  commands enabled.
- **Development client** (option ON): when a material loads without fresh shaders for the platform
  and with a graph, it first looks in a local shader cache, then compiles through the shared library
  and stores the result in that cache. Decided with the maintainer: the client never writes into the
  tracked data. A recompile would otherwise leave every developer checkout, and automated checkouts
  like the bug loop's, with modified `.hmat` files after each compiler-version bump. The cache lives
  in `Cache/Shaders/` under the data directory (`data/client/Cache` is already git-ignored and never
  packed into the patch). Entries are keyed by material path, graph hash, function hashes and
  versions, so stale entries are simply never hit. Failing to write the cache is a warning, not an
  error. On macOS the compile
  uses Apple's toolchain (`xcrun metal` / `metallib`, available with the Xcode command line tools).
  If that is unavailable, falling back to Metal source compiled by the device is acceptable for
  development.
- **Release client** (option OFF): checks that the shader variant it is about to use (default, UI
  and so on) exists for its platform. If not, it shows an error naming the material and the variant
  and exits. It does not compare hashes or versions.
- Material instances (`.hmi`) carry no shaders and are unaffected beyond their parent material.

### 5.5 Metal shader format

The Metal shader blob (`src/shared/msl_cross/metal_shader_blob.h`: stage and threadgroup header,
then MSL source) gains a payload kind: MSL source or a compiled Metal library. The Metal device
loads both (`newLibraryWithSource` / `newLibraryWithData`). Cooked builds always contain libraries,
compiled for arm64 / macOS 11 (the client's minimum, see the `client-macos` job in
`.github/workflows/nightly-release.yml`).

### 5.6 Release pipeline

Cooking runs inside the nightly workflow and produces a **cook overlay**: the files that differ in
the cooked build (rewritten materials and meshes, per-page entity files) plus a list of files to
leave out (the `.wobj`). The deployer applies the overlay to its data checkout before running
`update_compiler`, as part of staging (`deploy/deployer/mmo_deployer/stage.py`).

> **Designer's call, deviates from what was sketched in chat:** in the chat the platform-independent
> steps ran *on the deployer*. Here all cooking runs in the workflow and the deployer only applies a
> finished overlay. The deployer image is updated on its own cycle, so cooking code inside it would
> drift away from the release commit it cooks (wrong page format for that night's client). Cooking
> in the workflow always uses the release commit's own code, and the deployer stays a file copier.

Jobs, all at the release commit:

1. **Windows client job** (`client`): `mmo_cook` compiles D3D11 shaders for every material. Only the
   `.hmat` and `.hmf` files of `data/client` are needed (sparse checkout; 134 materials, 8.6 MB
   today). Output: a D3D11 shader artifact.
2. **macOS client job** (`client-macos`): the same for Metal, including the `.metallib` compile.
3. **Cook job** (Linux, new, or part of an existing Linux job): merges both shader artifacts into
   the materials, strips editor-only chunks (after merging, since the shader cook needs the graph),
   packs entity pages, and writes the overlay plus its delete list as a release asset listed in
   `release.json`.
4. **Deployer:** downloads the overlay, checks its checksum like `client-bin.zip`, applies it to the
   data checkout, then builds the patch as today. Releases without an overlay stage unchanged.

**Failure policy (maintainer: warn and continue).** Failures are handled per shader variant
(designer's call):
- If a variant fails to compile from the current graph while the others succeed, that variant is
  left out and the freshly cooked ones ship. Older repository shaders are never mixed in, because
  they may come from a different graph version with a different parameter layout. The release
  client aborts only if it asks for the missing variant. Known case today: the UI variant of some 3D
  materials, which is never used.
- If a material cannot be cooked for a platform at all (no graph, the graph does not load), it ships
  with whatever shaders of that platform the repository file has, or without them. Known cases:
  `Models/Mine/Mine.hmat` and `Textures/Desert/Rocks/Rock.hmat`.

Every warning is shown as a GitHub
annotation in the run summary and carried into the deployer's notification. Errors that are not
about one asset (tool crash, unreadable data) fail the job as usual.

### 5.7 Per-page entity files

- Only cooked builds contain them. The repository keeps `Entities/<page>/<uid>.wobj`, and the
  editor never writes page files.
- Per page with placements, cooking writes one file (for example `Entities/<page>.wpage`) holding
  every placement of that page as chunks. The individual `.wobj` of the page go on the overlay's
  delete list.
- The client (`ClientWorldInstance::LoadPageEntities`, `src/mmo_client/world_deserializer.cpp`)
  loads the page file if it exists, otherwise the `.wobj` files as today. Worlds without terrain
  (WMO-only maps) work the same way, because page files are independent of `.tile` files. (`.tile`
  has no entity chunk today. The `MENT` chunk exists only in the legacy `.hwld` layout.)
- Server and nav builder do not change (they read uncooked data).
- Patch effect, accepted: editing one placement re-downloads that page's file instead of one small
  `.wobj`.

### 5.8 Code layout

- `src/shared/content_cook/`: cooking steps as library functions with explicit inputs and
  outputs: shader cook per platform, shader merge, chunk stripping, entity page packing, overlay
  writing. No global state, no UI. Catch2 suite `src/tests/content_cook_tests/` (registered via
  `mmo_add_test`). Steps that need a graphics compiler sit behind the platform's shader compiler, so
  the rest builds and tests on headless Linux.
- `src/mmo_cook/`: console tool with subcommands mirroring the steps (for example
  `mmo_cook shaders --platform D3D11|METAL`, `mmo_cook assemble`, `mmo_cook all`). Builds on Windows,
  Linux and macOS.
- `metal_material_baker` is retired once `mmo_cook` covers it.

### 5.9 Running a cooked build locally

`mmo_cook all --data data/client --out <dir>` produces a full cooked copy of `data/client` for the
platforms the machine can compile (D3D11 on Windows, Metal on macOS). The client can be started
against that directory. This is how anyone reproduces a problem that only shows in cooked builds.

## 6. Constraints

- C++17, project code style (CLAUDE.md). No new committed Python tooling for this; the deployer's
  existing Python changes only to download and apply the overlay.
- No exceptions (`SIMPLE_NO_EXCEPTIONS`).
- Deterministic output for everything cooking itself writes: same input and commit give
  byte-identical page files, stripped files and material containers (sorted placements, no
  timestamps). Compiled shader bytes may change when a CI runner image updates its compiler
  (fxc, Xcode `metal`); that costs a one-off re-download of the affected materials (8.6 MB in total)
  and is accepted (designer's call). Toolchains are not pinned.
- The nightly's time budget (gate up to 180 min, client builds up to 120 min, staged before 04:45).
  Shader cooking of 134 materials is small; entity packing reads a few hundred files.
- Cooked and uncooked data must both load in development clients.
- Server images and their data are untouched.

## 7. Testing

- Catch2: graph hash stability (layout-only edits keep it, semantic edits and function edits change
  it), freshness decisions, shader cache hits and misses, chunk stripping, page file round trip (write → client reader yields the
  same placements as the `.wobj`), overlay delete list, determinism (cook twice, compare bytes).
- Client: unit coverage of "page file preferred, `.wobj` fallback".
- Deployer: tests for overlay download, checksum, apply, and releases without an overlay (Python
  tests in `deploy/deployer/tests`).
- Before switching the nightly to cooked data for the first time, run the client against a locally
  cooked copy (5.9) on Windows and macOS.

## 8. Order of work (suggested)

Anything merged to `develop` reaches players the next morning, so every step must be safe to ship
on its own (designer's call). In particular, the nightly keeps runtime compiling allowed and never
enables the release-client abort before step 4 delivers cooked shaders, and the Metal device keeps
accepting source payloads.

1. Shared material graph library out of the editor (5.2).
2. Graph hash, versions and freshness in the `.hmat` format; editor save/load; development-client
   compile and release-client abort (5.1, 5.3, 5.4).
3. Metal library payload (5.5).
4. `content_cook` + `mmo_cook` shader steps; workflow jobs and overlay; deployer apply (5.6, 5.8).
5. Editor-only chunk stripping (audit, then strip).
6. Per-page entity files and client loader (5.7).
7. Local cooked runs documented (5.9); `metal_material_baker` retired.

## 9. Left to the builder

- Exact chunk ids and the material format version for the freshness values; page file name and
  layout.
- Overlay asset format (zip plus a manifest of deletions, or similar).
- Whether the cook job is new or folded into an existing Linux job.
- How the editor marks a material as changed after a load-time recompile.
- Follow-up worth proposing separately: precompile built-in shaders to Metal libraries at build
  time, for the same launch-time reason as 5.5.
