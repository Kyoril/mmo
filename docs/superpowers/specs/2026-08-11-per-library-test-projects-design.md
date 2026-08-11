# Per-Library Test Projects

**Date:** 2026-08-11
**Status:** Implemented. See *Implementation notes* at the end for what the work turned up
that this design did not anticipate.

## Problem

Alestia's tests live in five executables scattered across `src/`:

| Target | Files | Scope |
|---|---|---|
| `unit_tests` | 54 | base, base64, math, binary_io, network, auth_protocol, game_protocol, game, game_server, hpak, terrain, terrain_io, deferred_shading, graphics, frame_ui, game_client, scene_graph |
| `game_server_unit_tests` | 26 | game_server |
| `login_server_tests` | 4 | login_server |
| `realm_server_tests` | 5 | realm_server |
| `movement_tests` | 7 | client-side movement/collision |

`unit_tests` is the problem. It spans seventeen libraries, so it must link the union of
their dependencies, and several of those dependencies do not exist in a headless build.
The workarounds are visible in `src/unit_tests/CMakeLists.txt`: individual shared `.cpp`
files compiled directly into the test target to dodge a transitive render dependency, and
a `set_source_files_properties(... HEADER_FILE_ONLY TRUE)` block that silently excludes
three test files from compilation on server-only builds. Adding a test for a library that
pulls anything new into that union means understanding all seventeen.

The sibling project (rose, `D:\Src\rose`) solved this with `src/tests/<lib>_tests/`, one
folder and one executable per library, each declared by a one-line `CMakeLists.txt`
calling a `rose_add_test(<name> <libs...>)` function. This spec applies the same shape to
Alestia, and adds coverage for six libraries that currently have none.

## Goals

1. One test executable per library, under `src/tests/`.
2. Adding a test suite costs one folder, one three-line `CMakeLists.txt`, and one
   `add_subdirectory` line — no edits to the gate, CI, or any global list.
3. No behavior change to any existing test. Every test that runs today still runs.
4. New coverage for `game_common`, `updater`, `paging`, `http`, `tex`, `virtual_dir`,
   `xml_handler`, plus the uncovered `hpak` header path.

## Non-goals

- **Catch2 v3 upgrade.** Alestia vendors Catch2 v2 as a single header in `deps/catch`.
  Migrating to v3 would touch the includes of every one of ~100 test files and risks
  macro/API drift in the middle of a structural change. It stays available as a separate,
  later change; nothing in this design blocks it.
- **Renaming existing test files.** The two conventions in the tree (`test_foo.cpp` in
  `unit_tests`, `foo_test.cpp` in `game_server_unit_tests`) both survive the move. Files
  move with `git mv` and are not otherwise edited, so the diff reads as a move. New files
  use `<thing>_tests.cpp`.
- **A shared test-support library.** Rose has `src/tests/support` as an INTERFACE lib for
  helpers used by several suites. Alestia's existing helpers (`login_server_tests/mock_database.h`,
  `realm_server_tests/mock_databases.h`, `movement_tests/test_helpers/`) each have exactly
  one consumer. `src/tests/support/` gets created when a second consumer appears, not before.
- **Fixing bugs the new tests uncover** as part of the restructure commits. See
  *Characterization test policy* below.

## Design

### Directory layout

```
src/tests/
  CMakeLists.txt              # add_subdirectory per suite, gated where required
  base_tests/
    CMakeLists.txt            # mmo_add_test(base_tests base)
    test_clock.cpp
    test_sha1.cpp
    ...
  math_tests/
  game_server_tests/
  ...
```

`src/unit_tests`, `src/game_server_unit_tests`, `src/login_server_tests`,
`src/realm_server_tests` and `src/movement_tests` are removed from `src/` entirely.

### CMake foundation

Four additive pieces. All of them can land before anything moves, leaving the build green.

**`deps/catch/CMakeLists.txt`** (new). Declares:

- `catch2` — an INTERFACE library exposing `deps/catch` as an include directory.
- `catch_main` — a STATIC library built from a single committed `catch_main.cpp`
  containing `#define CATCH_CONFIG_MAIN` followed by `#include "catch.hpp"`, linking
  `catch2` PUBLIC.

This replaces the global `include_directories("${CMAKE_CURRENT_SOURCE_DIR}/deps/catch/")`
at `cmake/mmo_external_dependencies.cmake:129` with a target, and removes the need for a
`main.cpp` in every test folder.

**`mmo_add_test(name libs...)`** in `cmake/mmo_macros.cmake`. Behavior:

- Recursive glob of `*.cpp` / `*.h` under the current source dir, so subdirectories such
  as `movement_tests/test_helpers/` continue to work. Empty source list is a
  `FATAL_ERROR`, not a silent no-op.
- `add_executable`, then `target_include_directories(... PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})`.
- `target_link_libraries(... PRIVATE catch_main ${ARGN})`.
- `source_group(TREE ... PREFIX "src")` and `set_target_properties(... FOLDER "tests")`.
- `mmo_deploy_runtime_dependencies(${name})`, matching what `add_exe` does today.
- `add_test(NAME ${name} COMMAND ${name})` with a `TIMEOUT` of 120 seconds, so a hung
  test fails the run instead of blocking it.
- Appends the target to the `all_tests` aggregate.

**`all_tests`** — a custom target that every `mmo_add_test` call adds itself to as a
dependency. One build target covers the whole suite.

**`enable_testing()` moves to the root `CMakeLists.txt`.** It is currently called inside
each test subdirectory. That does work — verified: `ctest -N` from `build/` lists all five
suites either way — so this is tidiness, not a bug fix. It becomes *necessary* at stage 2,
when each suite's `CMakeLists.txt` collapses to a single `mmo_add_test()` line and there is
nowhere left for a per-directory call to live.

### Target map

Derived by reading the `#include` lines of every existing test file. Twenty-one targets
replace the current five.

**Always built (headless-safe):**

| Target | Links | Sources |
|---|---|---|
| `base_tests` | `base` | `test_assign_on_exit`, `test_big_number`, `test_box`, `test_clock`, `test_erase_by_move`, `test_id_generator`, `test_linear_set`, `test_localization`, `test_non_copyable`, `test_sha1`, `test_task_system`, `test_async_database`, `test_database_pool` |
| `base64_tests` | `base64` | `test_base64` |
| `math_tests` | `math` | `test_aabb`, `test_aabb_tree`, `test_math`, `test_matrix`, `test_easing_functions` |
| `binary_io_tests` | `binary_io_hdrs` | `test_binaryio` |
| `simple_file_format_tests` | `simple_file_format_hdrs` | `test_simple_file_format` |
| `network_tests` | `network_hdrs` | `test_network_connection` |
| `auth_protocol_tests` | `auth_protocol` | `test_auth_protocol` |
| `game_protocol_tests` | `game_protocol` | `test_crypt`, `test_game_connection`, `test_game_protocol` |
| `game_tests` | `game`, `game_server` | `test_chat_channel_membership`, `test_field_map` |
| `game_server_tests` | `game_server`, `game`, `proto_data`, … | the 26 files from `game_server_unit_tests` **plus** `test_game_unit_s`, `test_line_of_sight`, `test_creature_separation`, `test_creature_combat_separation_integration`, `test_engagement_range_validation`, `test_reactive_positioning`, `test_reactive_positioning_integration` |
| `hpak_tests` | `hpak`, `hpak_v1_0` | `test_allocation_map` (+ new header round-trip tests) |
| `terrain_tests` | `terrain` | `test_terrain_region_math`, `test_terrain` (see *Open question*) |
| `terrain_io_tests` | `terrain_io` | `test_terrain_page_io` |
| `deferred_shading_tests` | (headers only) | `test_ssao_settings`, `test_contact_shadow_settings` |
| `graphics_tests` | `math` + `graphics/float_curve.cpp` compiled directly | `test_float_curve` |
| `frame_ui_tests` | `base` + `frame_ui/text_wrap.cpp` compiled directly | `test_text_wrap` |
| `game_client_tests` | `game`, `math` + `game_client/remote_movement_{queue,renderer}.cpp` compiled directly | `test_remote_movement_queue`, `test_remote_movement_renderer`, `test_path_arrival`, `test_path_move_speed`, `test_path_movement_integration`, `test_path_sampling`, `test_unit_cast_info`, `test_console_var`, `test_crossfading_sound_loop` (guarded) |
| `login_server_tests` | unchanged | unchanged |
| `realm_server_tests` | unchanged | unchanged |

**Gated on `MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR`:**

| Target | Sources |
|---|---|
| `scene_graph_tests` | `test_camera_frustum`, `test_animation_notify_deferral` |
| `movement_tests` | unchanged |

Notes on specific placements:

- `test_async_database` and `test_database_pool` include `base/async_database.h` and
  `base/database_pool.h`, not `sql_wrapper` headers. They belong in `base_tests`.
- `test_console_var` includes `mmo_client/console/console_var.h` — a header from the
  client *executable*, not a library. `game_client_tests` gains
  `target_include_directories(... ${CMAKE_SOURCE_DIR}/src/mmo_client)`, exactly as
  `movement_tests` does today.
- `test_crossfading_sound_loop` needs `client_data`, and lives in `game_client_tests`
  behind the same `MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR` guard used today. The guard
  shrinks from three files inside a 54-file monolith to one file inside an 8-file target.

**Renames:** `game_server_unit_tests` becomes `game_server_tests`; `unit_tests` ceases to
exist. Both names appear in the gate script, CI, `.claude/settings.json` and the docs, all
of which are updated (see below).

**Single-file targets.** `base64_tests`, `binary_io_tests`, `network_tests` and
`simple_file_format_tests` each hold one file. They stay separate: a three-line
`CMakeLists.txt` costs nothing, and the folder is where the next test for that library
goes. The Visual Studio solution ends with roughly 28 projects under the `tests` folder.

### What the split fixes

- The `set_source_files_properties(... HEADER_FILE_ONLY TRUE)` exclusion block disappears.
  Files that need render dependencies live in a gated target instead of being silently
  dropped from compilation.
- The "compile this shared `.cpp` directly" workaround survives — it is the mechanism that
  keeps `text_wrap`, `float_curve` and the remote-movement code under headless test
  coverage on Linux CI — but it is now scoped to three small targets whose `CMakeLists.txt`
  is short enough that the comment explaining *why* stays accurate.
- A new test for, say, `paging` no longer requires reasoning about `unit_tests`' entire
  link line.

### Gate and CI

**`tools/gate/verify.ps1`.** The `-Targets` default becomes
`login_server, realm_server, world_server, e2e_client, all_tests`. The four-executable
loop collapses to a single step:

```powershell
Push-Location build
ctest -C Debug --output-on-failure
Pop-Location
```

`ctest --test-dir` requires CMake 3.20 and the project floor is 3.12, hence
`Push-Location`. `/ship` inspects only the `"e2e"` step name and the top-level `passed`
flag, so renaming the test steps is safe. `last_report.json` gains one `"tests"` step in
place of four named ones; the failing suite is still identified inside
`tools/gate/logs/tests.log` via `--output-on-failure`.

**`.github/workflows/ccpp.yml`.** `cd bin && ./unit_tests` becomes
`cd build && ctest --output-on-failure`.

**Accepted risk, and the one thing this change cannot verify locally.** Linux CI today
runs only `unit_tests`. `game_server_unit_tests`, `login_server_tests` and
`realm_server_tests` are compiled by `make` but have never been executed on Linux.
Switching to `ctest` starts running them. If any holds a latent GCC/Linux failure, the
first push to `develop` turns CI red. This is a real coverage gap and closing it is worth
the risk; the remedy if it fires is a follow-up commit, not a revert of this work.

**Also updated:**

- `.claude/settings.json` — the three `Bash(./bin/Debug/*_tests.exe:*)` permission entries
  are replaced by `Bash(ctest:*)`.
- `CLAUDE.md` and `AGENTS.md` — the *Run Tests* section, plus three lines describing how
  to add a suite.
- `copilot-instructions.md:248-249` — points at `main.cpp` files that will no longer exist.
- `.claude/skills/terrain-author/references/pitfalls.md:47` — stale path to
  `src/unit_tests/test_terrain_page_io.cpp`.

Historical plan documents under `docs/superpowers/plans/` are left untouched; they record
what was true when written.

No wire format changes, so no `mmo::auth::ProtocolVersion` / `mmo::game::ProtocolVersion`
bump and no `tools/protocol_version_check.py --update` run.

### New coverage

Seven new targets, plus new files in the existing `hpak_tests`.

| Target | Coverage |
|---|---|
| `game_common_tests` | `WorldFoliageSerializer::Write` ↔ `WorldFoliageLoader` round-trip over `io::MemorySource`. The version gate specifically: `Version_0_0_0_1` files must load with `collides` defaulting to `true`, while `Version_0_0_0_2` reads the stored flag. `WorldEntityLoader` chunk handling. |
| `updater_tests` | `updating::UpdateURL` — http/https/filesystem scheme, explicit port, empty path defaulting to `/`, empty host throwing, non-numeric port. `updating::accumulate` summing `Estimates::downloadSize`/`updateSize`. `updating::parseEntry` fed a literal SFF string. |
| `paging_tests` | `PagePOVPartitioner::UpdateCenter` visible/invisible deltas as the viewer moves, **including at grid edge 0**, where the `max(center, radius) - radius` clamp guards an unsigned underflow. `ForEachPageInSquare`, `IsInRange`, `Distance`. |
| `http_tests` | `net::http::IncomingRequest::Start` over `io::MemorySource` — method and path parsing, query-string split, header folding, POST form decoding, malformed input. `net::http::authorize` Basic-auth base64 decode. `OutgoingAnswer::finishWithContent`. |
| `tex_tests` | `tex::loadPreHeader` rejecting bad magic and unknown `VersionId`. `tex::v1_0::HeaderSaver::finish` → `loadHeader` round-trip across the mip offset/length arrays. |
| `virtual_dir_tests` | `virtual_dir::appendPath` (double-slash, single-slash, empty operands), `splitLeaf`, `splitRoot`. |
| `xml_handler_tests` | `XmlAttributes::GetValueAsBool/GetValueAsInt/GetValueAsFloat` default fallback and malformed input. `ChainedXmlHandler::ElementStart`/`ElementEnd` nesting and `Completed()` state machine. |
| `hpak_tests` (existing) | `hpak::loadPreHeader` magic/version rejection; `hpak::v1_0::HeaderSaver` → `loadHeader` round-trip with multiple `FileEntry` records. |

All seven are headless: they operate on in-memory streams and pure logic, requiring no
GPU, database, socket, or on-disk data file.

**Characterization test policy.** These describe code that already ships. A failing new
test means a bug has been found, not that the test is wrong — the `paging` edge clamp is
the most likely candidate. When that happens: report it, and fix it in its own commit. Do
not write an assertion that encodes the buggy behavior, and do not bundle a behavior fix
into a restructure commit.

## Verification

The build being green is not sufficient evidence that the move preserved the suite — a
file silently omitted from a glob compiles nothing and fails nothing.

**The gating check is test-case count.** Before any file moves, run all five current
suites and record the `M` from each Catch2 summary line (`All tests passed (N assertions
in M test cases)`). After the split, the summed test-case count across all new targets
must equal the recorded baseline. Any discrepancy is a dropped file and blocks progress.

Per-stage: full Debug build succeeds and every test executable exits 0.

Final: `/gate` green (protocol check, build, tests, E2E), then `/ship`.

## Staging

Every stage leaves the build green and every existing test passing.

| Stage | Content |
|---|---|
| 0 | Branch in a worktree. **Record the baseline test-case counts for all five current suites.** |
| 1 | Foundation only: `deps/catch/CMakeLists.txt` + `catch_main`, `mmo_add_test`, `all_tests`, `enable_testing()` at root, empty `src/tests/CMakeLists.txt`. Existing targets untouched. |
| 2 | `git mv` the four non-`unit_tests` suites into `src/tests/`, convert to `mmo_add_test`, delete their `main.cpp`, rename `game_server_unit_tests` → `game_server_tests`. |
| 3 | Split `unit_tests` into seventeen per-library targets (plus seven files folded into `game_server_tests`); delete `src/unit_tests`. **Test-case count check against the stage-0 baseline.** |
| 4 | Gate, CI, docs, `.claude/settings.json`. |
| 5 | The seven new suites plus the `hpak` additions, one commit per library. |
| 6 | `/gate`, then `/ship`. |

## Implementation notes

What the work turned up that this design did not anticipate.

**The `enable_testing()` claim was wrong.** The original draft called the per-directory
`enable_testing()` calls a live bug that stopped `ctest` discovering anything. Verified
empirically: `ctest -N` from `build/` lists all five suites with or without the root call.
The move is tidiness plus a stage-2 prerequisite, and the design text above has been
corrected.

**`test_terrain.cpp` is dead code.** The open question is resolved: the file is wrapped in
`#if false` in its entirety — three test cases that have never compiled, which is why it
linked without `scene_graph` or `terrain`. It stays in `terrain_tests` with a note in that
suite's `CMakeLists.txt` rather than being quietly deleted. Enabling it needs a `Scene` and
a `MaterialManager`, so it would have to move to a gated suite first. **Open for the
maintainer:** enable it or delete it.

**`movement_tests` did not have a plain Catch main.** Its `main.cpp` brought up the null
`GraphicsDevice` before `Catch::Session().run()` and tore it down after, because `Scene`'s
constructor calls `GraphicsDevice::Get()`. Deleting it made the suite hang rather than
fail. That setup is now a Catch listener (`graphics_device_listener.cpp`) with the same
run-start/run-end ordering, so the suite still shares `catch_main`. Two Catch2 v2 details
cost a build each and are recorded in that file: listener interfaces need
`CATCH_CONFIG_EXTERNAL_INTERFACES` defined before `catch.hpp`, and `CATCH_REGISTER_LISTENER`
token-pastes its argument into an identifier, so it cannot take a namespace-qualified type.

**Four libraries had to widen their build gate.** The design assumed every library a suite
needs is built whenever `MMO_BUILD_TESTS` is on. Not so:

- `updater` was built only for `MMO_BUILD_LAUNCHER OR MMO_BUILD_TOOLS`.
- `xml_handler`, `tex` and `tex_v1_0` were built only for `MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR`,
  despite none of them needing a graphics device.

All four now also build under `MMO_BUILD_TESTS`, matching the pattern `hpak` already used.
Consequence: the Linux CI build compiles these four for the first time. All are plain
portable C++ with no platform branches, and the headless configuration was verified (see
below), but this is the same class of risk as the CI change in § Gate and CI.

**One real bug found, in `game_common`.** `WorldFoliageSerializer::Write` emits an empty
`FMSH` chunk when there are no instances, and `WorldFoliageLoader` rejected any `FINS`
chunk arriving with an empty mesh name table — so the serializer produced a `.hfol` its own
loader refused. Clearing every tree from a page and saving made that page fail to load.
Fixed in its own commit ahead of the suite that found it: the guard moved after the record
count and now only fires when there is at least one record that has to resolve a name.

**`paging` was clean.** The suspected unsigned underflow at the grid origin is properly
guarded by the existing `max(center, radius) - radius` clamp. The tests pin it so a
simplification cannot reintroduce the wrap.

**Two behaviours are documented rather than changed**, because fixing them is a judgement
call for the maintainer, not a test-suite decision:

- `XmlAttributes::GetValueAsInt` / `GetValueAsFloat` `ASSERT` on input that fails to parse
  at all, aborting a debug build instead of returning the caller's default —
  inconsistent with `GetValueAsBool`, which falls back gracefully. Not exercised by the
  suite; noted in the test file and in `xml_handler_tests/CMakeLists.txt`.
- `IncomingRequest` header lookup is case sensitive (a client sending `content-length` is
  not found by a lookup for `Content-Length`), while the `Content-Length` probe itself is
  case insensitive.

## Verification performed

- **Stage 3 count gate:** 656 test cases across the 21 post-split suites, exactly matching
  the pre-split baseline of 656 (`unit_tests` 419, `game_server_unit_tests` 183,
  `login_server_tests` 17, `realm_server_tests` 13, `movement_tests` 24). Nothing dropped.
- **Full build, client on:** 28 suites, `ctest` 28/28 green. 773 test cases total, up
  from the 656 the tree started with.
- **Headless build** (`MMO_BUILD_CLIENT=OFF`, editor/tools/launcher off): configures,
  builds `all_tests`, and runs 26/26 green — `movement_tests` and `scene_graph_tests`
  correctly excluded. This is the check that the widened library gates hold, though it is
  still MSVC rather than GCC.
- **Gate:** `tools/gate/verify.ps1 -SkipE2E` GREEN through the new `ctest` step.
