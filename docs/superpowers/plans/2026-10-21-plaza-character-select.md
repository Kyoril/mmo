# Plaza Character Select Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the static, WoW-like glue screens with a live 3D Haven scene: login and realm
selection in front of Haven's gate, a camera flight through the gate to the plaza where up to 5
characters stand, a reorderable roster for the rest, and a calm, non-WoW restyle of all glue
screens.

**Architecture:**
- Pure, headless-testable logic lives in `src/shared/game_client/`:
  - plaza slot assignment
  - roster reorder
  - last-played lookup
  - camera director and path
  - scene definition parsing
- A new `src/mmo_client/glue/` folder holds the runtime:
  - `GlueScene` owns an `OctreeScene`, sky, environment, a `ClientWorldInstance` and its
    loader thread.
  - `PlazaAvatars` builds character entities.
  - `LoginState` creates the scene and drives it.
  - The GlueUI draws it full-screen through the existing `WorldRenderer`, registered under a
    new name.
- On the realm side, a sort column, a `SetCharacterOrder` opcode and the realm time of day
  appended to `CharEnum` come in one protocol bump.

**Tech Stack:** C++17, Catch2 (`src/tests/*_tests`), nlohmann/json (`deps/json`, already used by
`perf_capture.cpp`), frame_ui XML + Lua, MySQL migration.

**Spec:** `docs/superpowers/specs/2026-10-21-plaza-character-select-design.md`

## Global Constraints

- **Branch:** all work on `feature/plaza-char-select`. It was branched from `develop`;
  check `git log develop..HEAD` before `/ship`.
- **Code style:**
  - Allman braces, tabs, `m_camelCase` members, `PascalCase` methods.
  - `#pragma once` and Doxygen comments on public members.
  - Copyright header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
  - No exceptions.
- **Characters:**
  - 5 plaza spots.
  - Visitors (order index ≥ 5) walk in and take the front spot. They walk back out when
    another character is selected.
  - Deleted characters walk out.
  - New characters go to the end of the order.
- **Buttons:** the primary button uses the existing `ENTER_WORLD` key, and change realm uses
  `CHANGE_REALM`.
- **Flights:**
  - The fly-in starts only after a realm is chosen **and** the char list has arrived.
  - It takes about 4–6 s and is skippable by any click or key.
  - "Change realm" flies back out.
  - Logout from the world lands directly on the plaza.
  - A full logout or disconnect cuts to the gate view.
- **No static backgrounds:** login and realm screens draw no static background image
  (`LoginBackgroundPH.htex`, `BackgroundPH.htex`). Character creation keeps its backdrop and
  layout and only picks up the new styling.
- **Localization:** every new string goes in all four `Localization.txt` files (enUS, deDE,
  frFR, ruRU). The hardcoded "N Characters" text in `CharSelect.lua` gets localized too.
- **Wire change:** bump `game::ProtocolVersion` from `0x0000001B` (27) to `0x0000001C` (28),
  run `python tools/protocol_version_check.py --update`, and add a row to
  `docs/protocol_versions.md`.
- **Migration:** additive only. The realm full schema (`data/realm/realm_db_schema_full.sql`)
  has no history inserts, so only the update file is added.
- **Threading:** client loader-thread code must not touch `signal<>`, Lua, `GraphicsDevice` or
  resource managers (`docs/threading.md`).
- **Font:** openly licensed, Latin + Cyrillic, not Friz Quadrata. Downloading a font file
  needs the user's approval first.

## Review Focus

1. **Character list arrives before the plaza is loaded** (fast login, cold disk). Expected: no
   flight, the plaza fades in when ready, nothing hangs. Covered by the
   `DecideGlueArrival` tests (Task 2).
2. **Account with zero characters.** Expected: an empty plaza showing 5 "New character"
   markers, and Enter World disabled. Covered by `AssignPlaza(0, -1)` (Task 2).
3. **Last-played character was deleted, or the realm changed.** Expected: falls back to order
   index 0, never an out-of-range selection. Covered by `ResolveInitialSelection` (Task 2).
4. **Malicious or stale `SetCharacterOrder`** (foreign GUID, duplicate, missing character,
   empty list). Expected: rejected as a whole, no partial write. Covered by
   `IsValidCharacterOrder` (Task 1).
5. **"Change realm" or a realm disconnect in the middle of the fly-in.** Expected: the camera
   reverses from where it is to the gate, with no jump to the start. Covered by the
   `GlueCameraDirector` reverse tests (Task 2).

---

### Task 1: Character order and realm time of day on the wire

**Files:**
- Create: `data/realm/updates/20261021_1_character_sort_index.sql`
- Create: `src/realm_server/character_order.h`, `src/realm_server/character_order.cpp`
- Create: `src/tests/realm_server_tests/test_character_order.cpp`
- Modify: `src/tests/realm_server_tests/CMakeLists.txt` (add `character_order.cpp` to `target_sources`)
- Modify: `src/realm_server/database.h` (~:179), `src/realm_server/mysql_database.h/.cpp` (:152 query)
- Modify: `src/realm_server/player.h/.cpp` (`DoCharEnum` :157, handler registration ~:3517)
- Modify: `src/shared/game_protocol/game_protocol.h` (:100 version; append opcode before `Count_` in `client_realm_packet`)
- Modify: `src/mmo_client/net/realm_connector.h/.cpp` (`OnCharEnum` :189)
- Modify: `src/shared/protocol_fingerprint.json` (via tool), `docs/protocol_versions.md`

**Interfaces:**
- Produces:
  - `bool IsValidCharacterOrder(const std::vector<uint64>& requested, const std::set<uint64>& owned)`
  - `IDatabase::SetCharacterOrder(uint64 accountId, std::vector<uint64> orderedGuids)` (`void`)
  - `game::client_realm_packet::SetCharacterOrder` (payload: `uint8` count, then `uint64` GUIDs in order)
  - `CharEnum` now ends with `uint64` time of day (ms since midnight, from `TimeOfDayManager::GetTimeOfDay()`)
  - Client:
    - `RealmConnector::SetCharacterOrder(const std::vector<uint64>& orderedGuids)`
    - `GameTime RealmConnector::GetRealmTimeOfDay() const`
    - `RealmConnector::GetCharacterViews()` now arrives in saved order

- [ ] **Step 1: Write the failing tests** in `test_character_order.cpp`, tag `[character_order]`:

```cpp
TEST_CASE("A permutation of the owned characters is a valid order", "[character_order]")
{
	CHECK(IsValidCharacterOrder({ 3, 1, 2 }, { 1, 2, 3 }));
}

TEST_CASE("Orders that are not exactly the owned set are rejected", "[character_order]")
{
	CHECK_FALSE(IsValidCharacterOrder({ 1, 2 }, { 1, 2, 3 }));        // missing
	CHECK_FALSE(IsValidCharacterOrder({ 1, 2, 2 }, { 1, 2, 3 }));     // duplicate
	CHECK_FALSE(IsValidCharacterOrder({ 1, 2, 9 }, { 1, 2, 3 }));     // foreign
	CHECK_FALSE(IsValidCharacterOrder({}, { 1 }));                    // empty
	CHECK(IsValidCharacterOrder({}, {}));                             // no characters, nothing to order
}
```

- [ ] **Step 2: Run the tests and confirm they fail**
  - Run: `cmake --build build --config Debug -t realm_server_tests && ./bin/Debug/realm_server_tests "[character_order]"`
  - Expected: the build fails because `character_order.h` is missing.

- [ ] **Step 3: Implement `IsValidCharacterOrder`** in `character_order.cpp`. The sizes must
  be equal, every requested GUID must be in `owned`, and there must be no duplicates.

- [ ] **Step 4: Run the tests and confirm they pass.** Same command. Expected: all pass.

- [ ] **Step 5: Server side**
  1. **Migration** `20261021_1_character_sort_index.sql`:
     `ALTER TABLE `characters` ADD COLUMN `sort_index` int unsigned NOT NULL DEFAULT 0;`
     with a one-line comment. A value of 0 means "never sorted".
  2. **Character list query:** `GetCharacterViewsByAccountId` appends
     ``ORDER BY (`sort_index` = 0), `sort_index`, `id` ``. Unsorted and new characters then
     follow sorted ones in creation order, so new characters land at the end and
     `CreateCharacter` needs no change.
  3. **`MySQLDatabase::SetCharacterOrder`:** one `UPDATE characters SET sort_index=<i+1> WHERE
     id=<guid> AND account_id=<accountId>` per GUID, inside a transaction if `mysql_wrapper`
     offers one.
  4. **`Player::OnSetCharacterOrder`:**
     - Read the count and GUIDs, and disconnect on a malformed packet.
     - Validate against the keys of `m_characterViews` under `m_charViewMutex`.
     - Warn and `Pass` on an invalid order.
     - Otherwise, `asyncRequestKeyed(m_accountId, ...)`.
     - Register it next to `DeleteChar`.
  5. **`DoCharEnum`:** write `io::write<uint64>(m_timeOfDayManager.GetTimeOfDay())` after the
     view range.

- [ ] **Step 6: Client side.**
  - `OnCharEnum` reads the trailing `uint64` into `m_realmTimeOfDay` before firing
    `CharListUpdated`.
  - `SetCharacterOrder` sends the new opcode.
  - The client also reorders `m_characterViews` locally to match, so the UI does not wait
    for a refresh.

- [ ] **Step 7: Bump and record the protocol version.**
  - Set `ProtocolVersion` to `0x0000001C`.
  - Run `python tools/protocol_version_check.py --update`. Expected: exit 0, fingerprint
    rewritten.
  - Add a row to `docs/protocol_versions.md`: "28 | 2026-10-21 | Character order:
    `SetCharacterOrder` client opcode, `CharEnum` gains trailing uint64 realm time of day",
    and update the "Current" table.

- [ ] **Step 8: Build every server target and the client.**
  - Run: `cmake --build build --config Debug -t realm_server world_server login_server mmo_client realm_server_tests`
  - Expected: builds with 0 errors, and `realm_server_tests` passes.

- [ ] **Step 9: Commit.**
  - `git add` the files above.
  - `git commit -m "feat(realm): persistent character order and realm time in char list"`

### Task 2: Headless plaza logic (roster, selection, camera director, scene definition)

**Files:**
- Create: `src/shared/game_client/plaza_roster.h/.cpp`
- Create: `src/shared/game_client/glue_camera_director.h/.cpp`
- Create: `src/shared/game_client/glue_scene_definition.h/.cpp`
- Create: `src/tests/game_client_tests/test_plaza_roster.cpp`
- Create: `src/tests/game_client_tests/test_glue_camera_director.cpp`
- Create: `src/tests/game_client_tests/test_glue_scene_definition.cpp`
- Modify: `src/tests/game_client_tests/CMakeLists.txt` (add the three `.cpp` files to `target_sources`)

**Interfaces:**
- Produces (namespace `mmo`):
  - `constexpr int32 PlazaSpotCount = 5;`
  - `struct PlazaAssignment { std::array<int32, PlazaSpotCount> spotCharacter; int32 frontCharacter; bool frontIsVisitor; };`
    - `spotCharacter[i]` is the order index standing at spot `i`, or -1.
    - The selected plaza character's own spot is -1, because they stand at the front.
  - `PlazaAssignment AssignPlaza(int32 numCharacters, int32 selectedIndex);`
  - `int32 ResolveInitialSelection(const std::vector<uint64>& orderedGuids, uint64 lastPlayedGuid);`
  - `void MoveRosterEntry(std::vector<uint64>& order, size_t from, size_t to);`
  - `uint64 GetLastCharacterForRealm(const String& cvarValue, uint32 realmId);`
  - `String SetLastCharacterForRealm(const String& cvarValue, uint32 realmId, uint64 guid);`
    The format is `"realmId:guid;realmId:guid"` with the GUID in decimal.
  - `enum class GlueArrival { FlyIn, FadeWhenReady, Snap };`
    `GlueArrival DecideGlueArrival(bool plazaReady, bool returningFromWorld);`
  - `struct GlueCameraKey { Vector3 position; Vector3 lookAt; };`
    `GlueCameraKey EvaluateGluePath(const std::vector<GlueCameraKey>& keys, float t);`
    - Catmull-Rom through the keys, with smoothstep easing on `t` in [0,1].
    - `t` is clamped.
    - One key returns that key.
  - `enum class GlueCameraMode { Gate, FlyingIn, Plaza, FlyingOut };`
  - `class GlueCameraDirector`:
    - `explicit GlueCameraDirector(float flightSeconds);`
    - `void FlyIn(); void FlyOut(); void Skip(); void SnapToPlaza(); void SnapToGate(); void Update(float deltaSeconds);`
    - `GlueCameraMode GetMode() const; float GetPathParameter() const;`
      The parameter is 0 at the gate and 1 at the plaza.
    - `bool ConsumeFlightFinished();` returns true once after a flight ends, whether naturally
      or skipped.
  - `struct GlueSpot { Vector3 position; float facingDegrees; String pose; GlueCameraKey camera; };`
  - `struct GlueSceneDefinition`:
    - `String world;`
    - `std::vector<std::pair<uint8, uint8>> gatePages, plazaPages;`
    - `uint32 environmentProfile;`
    - `std::vector<GlueCameraKey> flightPath;`
    - `float flightSeconds;`
    - `GlueSpot front;`
    - `std::array<GlueSpot, PlazaSpotCount> spots;`
    - `Vector3 entrance;`
  - `std::optional<GlueSceneDefinition> ParseGlueSceneDefinition(const String& json);`
    Returns nullopt when anything required is missing, or there are fewer than 2 path keys,
    or not exactly 5 spots.

- [ ] **Step 1: Write the failing tests**, tag `[plaza]`:

```cpp
TEST_CASE("No characters leaves every spot empty", "[plaza]")
{
	const auto a = AssignPlaza(0, -1);
	CHECK(a.frontCharacter == -1);
	for (const int32 c : a.spotCharacter) { CHECK(c == -1); }
}

TEST_CASE("A selected plaza character leaves their spot for the front", "[plaza]")
{
	const auto a = AssignPlaza(7, 2);
	CHECK(a.frontCharacter == 2);
	CHECK_FALSE(a.frontIsVisitor);
	CHECK(a.spotCharacter == std::array<int32, 5>{ 0, 1, -1, 3, 4 });
}

TEST_CASE("A visitor takes the front and all five stay at their spots", "[plaza]")
{
	const auto a = AssignPlaza(7, 6);
	CHECK(a.frontCharacter == 6);
	CHECK(a.frontIsVisitor);
	CHECK(a.spotCharacter == std::array<int32, 5>{ 0, 1, 2, 3, 4 });
}

TEST_CASE("Initial selection falls back to the first character", "[plaza]")
{
	CHECK(ResolveInitialSelection({ 10, 20, 30 }, 20) == 1);
	CHECK(ResolveInitialSelection({ 10, 20, 30 }, 99) == 0);   // deleted
	CHECK(ResolveInitialSelection({}, 20) == -1);
}

TEST_CASE("Roster entries move to the drop position", "[plaza]")
{
	std::vector<uint64> order{ 1, 2, 3, 4, 5, 6 };
	MoveRosterEntry(order, 5, 0);
	CHECK(order == std::vector<uint64>{ 6, 1, 2, 3, 4, 5 });
	MoveRosterEntry(order, 0, 5);
	CHECK(order == std::vector<uint64>{ 1, 2, 3, 4, 5, 6 });
}

TEST_CASE("Last character is remembered per realm", "[plaza]")
{
	String v = SetLastCharacterForRealm("", 1, 42);
	v = SetLastCharacterForRealm(v, 2, 7);
	v = SetLastCharacterForRealm(v, 1, 43);
	CHECK(GetLastCharacterForRealm(v, 1) == 43);
	CHECK(GetLastCharacterForRealm(v, 2) == 7);
	CHECK(GetLastCharacterForRealm(v, 3) == 0);
	CHECK(GetLastCharacterForRealm("garbage;1:x", 1) == 0);
}

TEST_CASE("Arrival depends on readiness and origin", "[plaza]")
{
	CHECK(DecideGlueArrival(true, false) == GlueArrival::FlyIn);
	CHECK(DecideGlueArrival(false, false) == GlueArrival::FadeWhenReady);
	CHECK(DecideGlueArrival(true, true) == GlueArrival::Snap);
	CHECK(DecideGlueArrival(false, true) == GlueArrival::FadeWhenReady);
}
```

  In `test_glue_camera_director.cpp`:

```cpp
TEST_CASE("Fly-in reaches the plaza after the flight time", "[glue_camera]")
{
	GlueCameraDirector d(5.0f);
	d.FlyIn();
	d.Update(2.5f);
	CHECK(d.GetMode() == GlueCameraMode::FlyingIn);
	CHECK(d.GetPathParameter() == Approx(0.5f));
	d.Update(3.0f);
	CHECK(d.GetMode() == GlueCameraMode::Plaza);
	CHECK(d.GetPathParameter() == 1.0f);
	CHECK(d.ConsumeFlightFinished());
	CHECK_FALSE(d.ConsumeFlightFinished());
}

TEST_CASE("Flying out mid-flight reverses from the current point", "[glue_camera]")
{
	GlueCameraDirector d(4.0f);
	d.FlyIn();
	d.Update(1.0f);                     // t = 0.25
	d.FlyOut();
	CHECK(d.GetPathParameter() == Approx(0.25f));
	d.Update(1.0f);
	CHECK(d.GetMode() == GlueCameraMode::Gate);
	CHECK(d.GetPathParameter() == 0.0f);
}

TEST_CASE("Skip jumps to the end of the current flight", "[glue_camera]")
{
	GlueCameraDirector d(5.0f);
	d.FlyIn();
	d.Skip();
	CHECK(d.GetMode() == GlueCameraMode::Plaza);
	CHECK(d.ConsumeFlightFinished());
}

TEST_CASE("Path evaluation hits the end keys", "[glue_camera]")
{
	const std::vector<GlueCameraKey> keys{ { { 0, 0, 0 }, { 0, 0, 1 } }, { { 10, 0, 0 }, { 10, 0, 1 } }, { { 20, 0, 0 }, { 20, 0, 1 } } };
	CHECK(EvaluateGluePath(keys, 0.0f).position.x == Approx(0.0f));
	CHECK(EvaluateGluePath(keys, 1.0f).position.x == Approx(20.0f));
	CHECK(EvaluateGluePath(keys, 0.5f).position.x == Approx(10.0f));
	CHECK(EvaluateGluePath(keys, 2.0f).position.x == Approx(20.0f));
}
```

  In `test_glue_scene_definition.cpp`, add two cases:
  - A minimal valid JSON with 2 path keys and 5 spots parses, and its `flightSeconds` and
    `spots[4].pose` round-trip.
  - The same JSON with 4 spots returns `std::nullopt`.

  The JSON keys are `world`, `gatePages`, `plazaPages`, `environmentProfile`, `flightPath`
  (array of `{position:[x,y,z], lookAt:[x,y,z]}`), `flightSeconds`, `front`, `spots`
  (`{position, facing, pose, camera}`) and `entrance`.

- [ ] **Step 2: Run the tests and confirm they fail.**
  - Run: `cmake --build build --config Debug -t game_client_tests`
  - Expected: compile errors for the missing headers.

- [ ] **Step 3: Implement the three units.** Use nlohmann/json with exception-free access
  (`json::parse(str, nullptr, false)` and `is_discarded()`, `contains` and `is_*` checks),
  because the project builds without exceptions.

- [ ] **Step 4: Run the tests and confirm they pass.**
  - Run: `./bin/Debug/game_client_tests "[plaza],[glue_camera],[glue_scene]"`
  - Expected: all pass.

- [ ] **Step 5: Confirm the suite still builds headless on Linux.**
  - Run: `wsl bash tools/gate/linux_gate.sh` (or the gate's Linux step).
  - Expected: `game_client_tests` builds and passes on gcc.

- [ ] **Step 6: Commit.**
  - `git commit -m "feat(client): headless plaza roster, glue camera director and scene definition"`

### Task 3: Glue scene renders Haven behind login and realm screens

**Files:**
- Create: `src/mmo_client/glue/glue_scene.h/.cpp`
- Create: `src/mmo_client/environment_apply.h/.cpp`
- Create: `data/client/Interface/GlueUI/GlueScene.json`
- Modify: `src/mmo_client/ui/world_renderer.cpp` (:122 attach check)
- Modify: `src/mmo_client/game_states/world_state.cpp` (:6923 `ApplyEnvironmentToRenderer` uses the new helper)
- Modify: `src/mmo_client/game_states/login_state.h/.cpp`
- Modify: `data/client/Interface/GlueUI/GlueParent.xml`, `AccountLogin.xml` (:27-34), `RealmList.xml` (:186-192)

**Interfaces:**
- Consumes (Task 2): `GlueSceneDefinition`, `ParseGlueSceneDefinition`, `GlueCameraDirector`, `EvaluateGluePath`
- Consumes (Task 1): `RealmConnector::GetRealmTimeOfDay()`
- Produces:
  - `void ApplyEnvironmentStateToRenderer(DeferredRenderer& renderer, const EnvironmentState& state, float brightness);`
    This is the non-fog part of today's `WorldState::ApplyEnvironmentToRenderer`, and
    `WorldState` calls it too.
  - `class GlueScene final : public NonCopyable`:
    - `GlueScene(const proto_client::Project& project, GlueSceneDefinition definition);`
    - `~GlueScene();` joins the loader thread.
    - `void Update(float deltaSeconds);` pumps the dispatcher with a 4 ms budget, advances
      the director and avatars, then sky and environment.
    - `bool IsGateReady() const; bool IsPlazaReady() const;`
    - `Scene& GetScene();`
    - `GlueCameraDirector& GetDirector();`
    - `const GlueSceneDefinition& GetDefinition() const;`
    - `void SetTimeOfDay(GameTime msSinceMidnight);`
    - `void SetRenderer(DeferredRenderer* renderer);` used for applying the environment.
  - Frame renderer name `"GlueSceneRenderer"`, registered by `LoginState` before
    `GlueUI.toc` loads, as `WorldRenderer(name, glueScene->GetScene())`.
  - `LoginState` members:
    - `std::unique_ptr<GlueScene> m_glueScene;`
    - `void OnIdle(float deltaSeconds, GameTime timestamp);` connected to `EventLoop::Idle`
      like `WorldState`.
  - Lua event `GLUE_SCENE_READY` fires once when the gate view is ready.

- [ ] **Step 1: Write `GlueScene.json`.**
  - **Interim world:** `world` = `Worlds/Development/Development`, the real map 0.
  - **Pages:** `gatePages` and `plazaPages` are the terrain pages under
    `Bridge_01_HavenGate` and Haven's plaza. Find them from the entity placements
    (`.../Entities/7713/...wobj`, page = floor(pos / `terrain::constants::PageSize`) + 32).
  - **Camera keys and spots:** authored by eye in Step 5.

  This "simplified Haven" loads only the listed pages, with no foliage, no far terrain and no
  units. If Step 6 measures more than 3 s from client start to `GLUE_SCENE_READY` on a warm
  disk, stop. Report to the user that a cut-down `Worlds/GlueHaven` world needs authoring in
  mmo_edit; only `world` and the page lists change.

- [ ] **Step 2: Implement `GlueScene`.**
  - **Loading, mirroring `WorldState`:**
    - Own the `asio::io_service` work queue, dispatcher and `work` guard, and a loader
      thread.
    - Run the `ClientWorldInstance` and `ClientWorldInstanceDeserializer` read from
      `LoadMap` (world_state.cpp:5929-5947).
    - Use `SkyComponent(*m_scene, nullptr)` with `SetNormalizedTimeOfDay`.
    - Use `EnvironmentController` with the definition's profile through the project's
      environment profiles.
    - Create a camera named `"Default"` on its own scene node.
  - **Page loading:**
    - Load the `gatePages` terrain pages and entities with the synchronous-prepare pattern of
      `WaitForWorldLoading` (world_state.cpp:2565-2608).
    - Do this incrementally from `Update` (one page per frame), never by blocking.
    - `IsGateReady()` turns true once the gate pages are loaded and
      `!HasPendingEntityLoads()`.
    - Then do the same for `plazaPages`, which drives `IsPlazaReady()`.
  - **Camera:** each `Update` places it at `EvaluateGluePath(def.flightPath, director.GetPathParameter())`,
    looking at `lookAt`. In the Gate mode, add a slow drift of ±0.3 m on a 20 s sine.
  - **Ambience:** give the world instance an `AmbientSoundEmitters`
    (`SetAmbientSounds`, as `WorldState::LoadMap` does), so placed town ambience plays. The
    login music stays as `LoginState` plays it today.
  - **Shutdown:** the destructor stops the work guard, joins the thread and resets the world
    instance before the scene.

- [ ] **Step 3: Wire up rendering.**
  - **`WorldRenderer::NotifyFrameAttached`:** keep `m_worldFrame = dynamic_cast<WorldFrame*>(m_frame)`
    for `GetWorldDeferredRenderer`. In `Render`, change the early-out to check `m_frame`
    instead of `m_worldFrame`.
  - **`GlueParent.xml`:** a full-screen `Frame name="GlueSceneFrame" renderer="GlueSceneRenderer"`
    as the first, lowest child. Above it goes a full-screen black `GlueSceneFade` texture
    frame, which `GlueParent.lua` fades out on `GLUE_SCENE_READY` over 1.0 s and which starts
    opaque.
  - **Backgrounds:** remove the `LoginBackgroundPH`/`BackgroundPH` image sections from
    `AccountLogin.xml` and `RealmList.xml`. Keep their gradient bars.
  - **`LoginState`:**
    - In `OnEnter`, create the scene, register the renderer, and call
      `SetTimeOfDay(<UTC ms since midnight from std::chrono::system_clock>)`.
    - In `OnLeave`, unregister the renderer, then reset `m_glueScene` after
      `FrameManager::ResetTopFrame()`.
  - **Renderer hookup:** `GlueScene::SetRenderer` gets the `WorldRenderer`'s
    `DeferredRenderer` after load, by looking up `GlueSceneFrame`'s renderer.

- [ ] **Step 4: Build and run the client.**
  - Run: `cmake --build build --config Debug -t mmo_client`
  - Launch `bin/Debug/mmo_client.exe` with no auto-login (temporarily rename
    `bin/Debug/config/RunOnce.cfg`, restore it afterwards).
  - Expected: the login UI over a dark fill that fades to a 3D view, with no image backdrop
    and no errors in `Client.log` (read with `Get-Content -Raw`).

- [ ] **Step 5: Author the gate and plaza camera keys and spots.**
  - Iterate on `GlueScene.json` with screenshots.
  - Use the recipe in the `client-visual-verification` memory, and add a `-glueSkipTo <t>`
    style cvar only if it is needed to inspect path points.
  - Show the gate view and the plaza view to the user and get approval before Task 4.

- [ ] **Step 6: Measure readiness time** (Log `GLUE_SCENE_READY` and plaza-ready timestamps
  relative to `OnEnter`.) If gate readiness takes over 3 s, apply the stop rule from Step 1.

- [ ] **Step 7: Commit.**
  - `git commit -m "feat(client): render Haven gate scene behind the glue screens"`

### Task 4: Characters on the plaza

**Files:**
- Create: `src/mmo_client/char_creation/character_avatar_builder.h/.cpp`
- Create: `src/mmo_client/glue/plaza_avatars.h/.cpp`
- Modify: `src/mmo_client/char_creation/char_select.h/.cpp`
- Modify: `src/mmo_client/glue/glue_scene.h/.cpp` (owns `PlazaAvatars`)

**Interfaces:**
- Consumes:
  - From Task 2: `AssignPlaza`, `GlueSpot`, `GlueSceneDefinition::front/spots/entrance`.
  - From Task 3: `GlueScene::GetScene()`.
- Produces:
  - `class CharacterAvatarBuilder final : public CustomizationPropertyGroupApplier`
    - `explicit CharacterAvatarBuilder(const proto_client::Project& project);`
    - `Entity* Build(Scene& scene, SceneNode& node, const CharacterView& view, const String& entityName, ItemDisplayAttachmentMap& attachments);`
    - `Build` holds the logic of `CharSelect::SelectCharacter`'s model branch and the three
      `Apply` overloads (char_select.cpp:63-237), applied to the entity it creates.
  - `class PlazaAvatars final : public NonCopyable`
    - `PlazaAvatars(Scene& scene, const proto_client::Project& project, const GlueSceneDefinition& definition);`
    - `void SetCharacters(const std::vector<CharacterView>& ordered);`
      Rebuilds entities for changed GUIDs only.
    - `void Select(int32 orderIndex, bool animate);`
      - `animate=false` places everyone instantly.
      - `animate=true` makes movers walk.
      - A visitor walks in from `entrance`, and the previous visitor walks back to
        `entrance` and is then hidden.
    - `void Leave(int32 orderIndex);` walks the character to `entrance`, used before a delete.
    - `void Rotate(float degrees);` turns the front character.
    - `std::optional<Vector3> GetNameplateAnchor(int32 orderIndex) const;`
      Gives a world position 0.4 m above the head bone, or the bounds top, and nullopt for
      characters not on the plaza.
    - `void Update(float deltaSeconds);`
  - `CharSelect`:
    - `void SetGlueScene(GlueScene* scene);` is called by `LoginState` on enter, and with
      `nullptr` on leave.
    - `SelectCharacter` drives `PlazaAvatars::Select` instead of `ModelFrame`.
    - Remove `m_modelFrame` and the `SetCharSelectModelFrame` binding.

- [ ] **Step 1: Extract the builder.** Move the code from `CharSelect` into
  `CharacterAvatarBuilder` with no behaviour change. Build `mmo_client`; expected: 0 errors.

- [ ] **Step 2: Implement `PlazaAvatars`.**
  - **Entities:** one scene node and entity per character.
  - **Poses:** a spot loops its `pose` animation, or `Idle` if that animation is missing. The
    front spot loops `Idle`.
  - **Walking:** play `Walk` while moving at 2.5 m/s in a straight line, and face the
    direction of travel. On arrival, face `facingDegrees` and switch to the target pose.
  - **Animation:** animation states are advanced the way `ModelFrame` does it
    (model_frame.cpp:181-201).
  - **Equipment:** goes through `ApplyItemDisplay` with `sheathed=false`, as today.
  - **Teardown:** clear attachments before destroying an entity (see the `SetModelFrame`
    comment on dangling attachments).

- [ ] **Step 3: Verify in the client.**
  - Build `mmo_client` and run it with auto-login (`RunOnce.cfg`) against the local dev stack
    (the ports are in the memory recipe).
  - Use an account with 7+ characters (create test characters if needed).
  - Expected:
    - 5 characters at their spots with varied poses, and the first at the front.
    - Calling `SelectCharacter(6)` from the console (`/run`, or a temporary Lua binding test)
      makes a visitor walk in.
  - Take screenshots.

- [ ] **Step 4: Commit.**
  - `git commit -m "feat(client): show up to five characters and visitors on the Haven plaza"`

### Task 5: Character select flow and UI on the plaza

**Files:**
- Modify: `data/client/Interface/GlueUI/CharSelect.xml`, `CharSelect.lua`, `RealmList.lua`, `GlueParent.lua`
- Modify: `src/mmo_client/char_creation/char_select.h/.cpp` (Lua bindings)
- Modify: `src/mmo_client/game_states/login_state.h/.cpp` (flow, input)
- Modify: `data/client/Locales/Locale_{enUS,deDE,frFR,ruRU}/Localization.txt`

**Interfaces:**
- Consumes:
  - From Task 1: `SetCharacterOrder`, `GetRealmTimeOfDay`.
  - From Task 2: `DecideGlueArrival`, `ResolveInitialSelection`, `MoveRosterEntry`,
    `Get/SetLastCharacterForRealm`.
  - From Task 3: `GlueCameraDirector`.
  - From Task 4: `PlazaAvatars`.
- Produces Lua bindings (registered in `CharSelect::RegisterScriptFunctions`):
  - `RotateCharacter(degrees)`.
  - `GetPlazaNameplatePosition(orderIndex) -> x, y, visible`, in UI-space units (see the
    `frame-ui-coordinate-spaces` memory).
  - `MoveCharacterInOrder(fromIndex, toIndex)` reorders locally and in the plaza, live.
  - `SaveCharacterOrder()` sends the order only if it changed.
  - `PlazaCharacterLeave(orderIndex)`.
  - `GlueFlyToGate()`, `GlueSkipFlight()`, `IsGlueFlightActive()`.
  - `GetInitialCharacterIndex()` uses the cvar and `ResolveInitialSelection`.
  - `GluePushIn()` makes the camera ease 40% of the way towards the front character over
    0.8 s, then fires `GLUE_PUSHIN_FINISHED`.
- Produces Lua events: `GLUE_FLIGHT_FINISHED`, `GLUE_PUSHIN_FINISHED`.
- Adds the cvar `lastCharacters`, a string defaulting to `""` and saved to config. It is set
  in `CharSelect_EnterWorld`.

- [ ] **Step 1: Wire the flow in `LoginState::OnCharListUpdated`.**
  - Call `m_glueScene->SetTimeOfDay(m_realmConnector.GetRealmTimeOfDay())`.
  - `returningFromWorld` is true when `OnEnter` took the `REQUEST_CHAR_LIST` branch.
  - Act on `DecideGlueArrival(IsPlazaReady(), returningFromWorld)`:
    - `FlyIn` → `GetDirector().FlyIn()`.
    - `Snap` → `SnapToPlaza()`.
    - `FadeWhenReady` → keep the fade frame up, then `SnapToPlaza()` and fade out once
      `IsPlazaReady()`.
  - **Realm disconnect** (`OnRealmDisconnected`) → `SnapToGate()`.
  - **Input:** mouse down or a key while `IsGlueFlightActive()` → `Skip()`. The Enter key
    during a flight only skips; it does not enter the world.

- [ ] **Step 2: Rebuild `CharSelect.xml` as minimal chrome over the scene.**
  - **Bottom bar:**
    - `ENTER_WORLD` at bottom centre.
    - `CHANGE_REALM` and `QUIT` at bottom left.
    - "All characters (N)" plus Create and Delete at bottom right.
    - Create becomes a normal button with 5+ characters.
  - **Remove** `CharModel`, `CharListFrame` and the full-screen `BackgroundPH` texture.
  - **Roster panel:**
    - A slim right-hand panel, hidden by default and toggled by the button.
    - Rows show name · level · class, an "on the plaza" marker for the top 5, and a divider
      before the rest.
    - Rows are dragged with mouse down/move/up on the row to reorder, calling
      `MoveCharacterInOrder`. Clicking a row selects that character.
    - Closing the panel calls `SaveCharacterOrder()`.
  - **Nameplates:**
    - 6 nameplate frames (5 spots + front) positioned every frame from
      `GetPlazaNameplatePosition`.
    - Each shows the name and "Level N Class", and the front one adds the zone name.
  - **"New character" markers:** at empty spots when there are fewer than 5 characters; they
    open `CharSelect_CreateCharacter`.
  - **Rotation:** dragging on empty screen space while on the plaza calls
    `RotateCharacter`.

- [ ] **Step 3: Update `CharSelect.lua`.**
  - Hide all chrome while flying and show it on `GLUE_FLIGHT_FINISHED`.
  - **Initial selection** comes from `GetInitialCharacterIndex()`.
  - **`CharSelect_ChangeRealm`:** hide the chrome, call `GlueFlyToGate()`, and show the realm
    list on `GLUE_FLIGHT_FINISHED`.
  - **`CharSelect_EnterWorld`:** set the last-character cvar, call `GluePushIn()`, and on
    `GLUE_PUSHIN_FINISHED` call `EnterWorld()`.
  - **`CharSelect_ConfirmDelete`:** call `PlazaCharacterLeave(index)` first, then delete
    after 1.5 s.
  - **Strings:** replace the hardcoded "N Characters" and "Level N Class" with localized
    keys. The new keys are `CHAR_COUNT_FORMAT`, `CHAR_LEVEL_CLASS_FORMAT`, `ALL_CHARACTERS`,
    `NEW_CHARACTER_SPOT`, `ON_THE_PLAZA`, `NOT_ON_THE_PLAZA` and `DRAG_TO_REORDER`, in all 4
    locales.

- [ ] **Step 4: Verify end to end in the client** with an account of 7+ characters. Take
  screenshots for each check.
  1. Realm pick → fly-in; a click skips it.
  2. Roster drag of character 7 into the top 5 → they walk in live; reopen after a client
     restart → the order persists.
  3. Select a visitor → they walk in; select a plaza character → the visitor walks out.
  4. Change realm → fly-out to the realm list; pick the realm → fly-in.
  5. Enter World → push-in, then loading; logout → plaza without a flight and with the
     last-played character at the front.
  6. Delete → the character walks out.
  7. 0 characters → 5 markers, and Enter World is disabled.

- [ ] **Step 5: Commit.**
  - `git commit -m "feat(ui): plaza character select flow, roster ordering and nameplates"`

### Task 6: Calm restyle of all glue screens

**Files:**
- Create: `data/client/Fonts/<ChosenFont>-Regular.ttf` and its `OFL.txt` license
- Modify: `data/client/Interface/GlueUI/GlueTemplates.xml` (fonts :5-6, button and text templates, `GlueBorderFrame`)
- Modify: `AccountLogin.xml`, `RealmList.xml`, `CharSelect.xml`, `CharCreate.xml` and `GlueDialog.xml`, wherever they hardcode WoW gold (`FFFFD100`, `FFEED37C`), parchment tints or `FRIZQT__`

**Interfaces:** this task uses only the existing template names (`GlueButton`,
`GlueButtonPrimary`, `GlueTextSmall/Large`, `GlueBorderFrame`, and the font names
`DefaultSmall`/`DefaultHeader`), so no layout file needs renaming.

- [ ] **Step 1: Mock up font and colour candidates and let the user pick.**
  - Fonts: Alegreya, Cormorant Garamond and EB Garamond. All are SIL OFL with Cyrillic.
  - Show them in the brainstorm companion, rendered over a gate-view screenshot from Task 3.
  - **Colours:** text ivory `FFF3EEE2`, secondary sage `FFCFE0D4`, panels `C0151A1F` with a
    1 px `40F3EEE2` border, primary button sage `CC2C3A30`. These are starting values the
    user may change.
  - Then ask the user to approve downloading the chosen font (name, source URL, size).

- [ ] **Step 2: Apply the font and colours** in `GlueTemplates.xml`, then sweep the other glue
  XMLs for gold, parchment and `FRIZQT__` with
  `rg -n "FFFFD100|FFEED37C|FFE3B0|FRIZQT" data/client/Interface/GlueUI`.
  Expected afterwards: no matches. In-game UI (`Interface/GameUI`) stays untouched.

- [ ] **Step 3: Verify in the client.** Take screenshots of the login, realm list, plaza,
  delete dialog and character creation screens. Show them to the user. Check ruRU text renders
  (switch locale cvar or use the Russian strings in a test label).

- [ ] **Step 4: Commit.**
  - `git commit -m "feat(ui): calm glue screen restyle replacing Friz Quadrata and gold"`

### Task 7: Gate and ship

- [ ] **Step 1: Check the shutdown wiring.** Run `python tools/shutdown_check.py`. It is
  client-side, so this only confirms nothing server-side regressed. Expected: exit 0.
- [ ] **Step 2: Run the full gate** with `/gate full`. This covers protocol, build, unit
  tests, the Linux gate, E2E and code review. E2E logs into characters, so it exercises
  `CharEnum`'s new trailing field. Expected: green.
- [ ] **Step 3:** `/ship` after the user's review. Do not push.
