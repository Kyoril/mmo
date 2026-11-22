# Traversal Test Field (Vigor, Dash, Glider) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A player who was given the Glider and Dash spells by a GM can sprint out of combat and glide (with updrafts) on the test map "Collision", both paid from a new Vigor resource. No mounts.

**Architecture:** All rules (Vigor drain and regen, glide velocity, updraft lookup) live in one shared, pure module `game/traversal`. The client uses it to predict and the world server uses it to validate. Dash reuses the existing speed-aura pipeline (ModIncreaseSpeed + ack handshake) plus one new aura type that drains Vigor. Gliding is a client-predicted movement flag (like jumping) with two new movement opcodes, which the server validates. Vigor is two appended player fields that are settled on transitions, with own-client prediction.

**Tech Stack:** C++17, Catch2, protobuf (proto2), frame_ui XML/Lua, Python protobuf for data authoring (scratchpad scripts only).

**Spec:** `docs/superpowers/specs/2026-11-21-traversal-pillar-design.md` (sections 1–3, "Weg in Schritten" step 1, minus mounts).

## Global Constraints

- Branch `feature/traversal-test-field` from `develop`. Use a worktree on `D:` (H: is short on space). Never touch `H:/mmo` itself, because another session's cutscene work is uncommitted there.
- Any wire change bumps `mmo::game::ProtocolVersion`, then run `python tools/protocol_version_check.py --update` and add a row to `docs/protocol_versions.md`.
- Opcodes, aura types and movement flags are **append-only**. Opcodes go directly before `Count_`. Aura types go after `PreventAuraApplication = 46`.
- Resource name: enUS **Vigor**, deDE **Schwung**. The general sprint is enUS **Dash**, deDE **Spurt**. The Scout's "Sprint" (spell 10) stays unchanged.
- deDE text addresses the player with Ihr/Euch/Euer, never du/dich/dein.
- Gating: nothing is available without a GM learning the spells (`.learn <id>`). A player with neither spell has `MaxVigor == 0` and sees no UI.
- Vigor is never spent in combat. Dash ends on entering combat. Damage ends gliding.
- Code style follows CLAUDE.md: Allman braces, tabs, `m_` members, PascalCase methods, copyright header, Doxygen on public members, no exceptions.
- No one-off scripts in `tools/`. Data-authoring scripts go to the scratchpad.

## Tuning values (initial, all in `TraversalSettings` defaults)

| Name | Value |
|---|---|
| `maxVigor` | 100 |
| `regenPerSecond` | 25 (only when grounded, not dashing, not gliding) |
| `regenDelaySeconds` | 1.0 (after the last drain) |
| `glideDrainPerSecond` | 8 (4 inside an updraft) |
| `glideStartCost` | 5 |
| Dash drain | 12 /s (aura basepoints of `DrainVigor`) |
| Dash speed | +35 % run speed (`ModIncreaseSpeed` basepoints 35) |
| `glideSinkSpeed` | 2.5 m/s |
| `glideForwardSpeed` | 10 m/s |
| `glideVerticalAccel` | 12 m/s² (how fast vertical speed approaches its target) |
| Updraft strength | per trigger, default 8 m/s upward target |

## Review Focus

1. **Lag at the empty point:** the client predicts 0 Vigor slightly earlier or later than the server. Expected: the client closes the glider itself, and the server tolerates 1.0 s before counting a violation. Test in Task 4 (`Glide past empty within grace is not a violation`).
2. **Landing from a glide after a long fall:** a player who opened the glider 80 m up and lands takes no fall damage. A player who closes the glider 3 m above ground takes none either. Test in Task 4 (`Fall height restarts at glide stop`).
3. **Combat while dashing or gliding:** getting hit while gliding forces a stop. Entering combat removes Dash, and regen pauses in combat. Tests in Tasks 3 and 4.
4. **Relog or teleport while gliding:** the flag must not survive. The server clears glide state on teleport, map change and logout, and Vigor is re-derived as full on login. Test in Task 4 (`Teleport clears glide`).
5. **Observers:** another client sees a glider sink slowly, not fall at gravity speed. Remote simulation uses the same flag path. Test in Task 5 (`Remote unit with Gliding flag sinks at glide speed`).

---

### Task 1: Shared traversal rules (`game/traversal`)

**Files:**
- Create: `src/shared/game/traversal.h`, `src/shared/game/traversal.cpp`
- Test: `src/tests/game_tests/traversal_test.cpp`

**Interfaces:**
- Produces:
  - `struct TraversalSettings` (fields as in the tuning table, with those defaults).
  - `enum class VigorActivity : uint8 { Idle, Gliding, GlidingInUpdraft, Draining }`. `Draining` uses an explicit drain rate.
  - `struct VigorState { float current; float max; float secondsSinceDrain; }`.
  - `void AdvanceVigor(VigorState& state, const TraversalSettings& s, VigorActivity activity, float extraDrainPerSecond, bool inCombat, bool grounded, float seconds)`. Drain = glide drain (by activity) + `extraDrainPerSecond`. Clamp to [0, max]. Regen only if no drain, grounded, not in combat, and `secondsSinceDrain >= regenDelaySeconds`.
  - `float SecondsUntilEmpty(const VigorState&, const TraversalSettings&, VigorActivity, float extraDrainPerSecond)`. Returns `+inf` when nothing drains.
  - `struct UpdraftVolume { Vector3 center; float radius; Vector3 halfExtents; float orientation; float strength; }`. `radius > 0` means a sphere, otherwise an oriented box, with the same math as `AreaTriggerManager::IsPointInBox`.
  - `float GetUpdraftStrength(const std::vector<UpdraftVolume>&, const Vector3& position)`. Returns the strongest containing volume, or 0.
  - `Vector3 StepGlideVelocity(const Vector3& velocity, const Vector3& desiredHorizontalDir, float updraftStrength, const TraversalSettings&, float seconds)`. Horizontal = normalized dir × `glideForwardSpeed`, or decays to 0 with no input. Vertical approaches the target (`-glideSinkSpeed`, or `+updraftStrength` in an updraft) at `glideVerticalAccel`. The engine is **Y-up** (gravity is on Y in `unit_movement.cpp:2469`).

- [ ] **Step 1: Write failing tests** in `traversal_test.cpp`, tag `[traversal]`:
  - `Glide drains 8 per second`: from 100, Gliding, 2 s → 84.
  - `Updraft halves glide drain`: GlidingInUpdraft, 2 s → 92.
  - `Extra drain adds` (Dash 12 /s while idle, 1 s → 88).
  - `No regen before delay`: after a drain, 0.5 s idle and grounded → unchanged.
  - `Regen after delay`: at 50 with `secondsSinceDrain` = 1.0, 1 s idle → 75.
  - `No regen in combat`, `No regen while airborne`.
  - `Clamps at zero and max`.
  - `SecondsUntilEmpty`: 16 Vigor while gliding → 2.0; idle → inf.
  - `Updraft sphere and rotated box containment`; `Strongest updraft wins`.
  - `Glide velocity converges`: from (0,-20,0), no input, 3 s → y ≈ -2.5 (±0.05), horizontal ≈ 0.
  - `Glide velocity rises in updraft`: strength 8, 3 s → y ≈ +8.
  - `Glide forward speed`: dir (1,0,0) → |horizontal| = 10.
- [ ] **Step 2: Run them and check they fail.** Run `cmake --build <build> --config Debug -t game_tests && <bin>/Debug/game_tests "[traversal]"`. Expected: compile failure or FAIL.
- [ ] **Step 3: Implement** the functions in `traversal.cpp`. Check that `game_tests`' CMake glob already picks up `src/shared/game`. It is part of the `game` lib, so add nothing.
- [ ] **Step 4: Run tests and check they pass.** Expected: all `[traversal]` PASS.
- [ ] **Step 5: Commit** `game: shared traversal rules (vigor, glide, updrafts)`.

### Task 2: Wire format (flag, opcodes, Vigor fields, protocol bump)

**Files:**
- Modify: `src/shared/game/movement_info.h` (flag `Gliding = 1 << 22`; add it to `PositionChanging`; `IsGliding()`).
- Modify: `src/shared/game/object_type_id.h:384-386`: append `Vigor` and `MaxVigor` (float fields, one uint32 slot each) after `BankBagSlotCount`, then `PlayerFieldCount = MaxVigor + 1`.
- Modify: `src/shared/game_protocol/game_protocol.h`:
  - client opcodes `MoveStartGlide` and `MoveStopGlide` (before `Count_`, both lists that carry move opcodes, like `MoveStartSwim`);
  - server opcodes `MoveStartGlide` and `MoveStopGlide` (observer broadcast) plus `ForceStopGlide` (payload: guid);
  - bump `ProtocolVersion`.
- Modify: `src/realm_server/player.cpp:3073-3080` (proxy the two client opcodes), `src/world_server/player.cpp:1199-1214` (dispatch to the existing movement handler), `src/shared/bot_core/bot_realm_connector.cpp` (ignore or forward like the swim opcodes).
- Modify: `src/shared/protocol_fingerprint.json` (via the tool), `docs/protocol_versions.md`.
- Test: `src/tests/game_protocol_tests/movement_info_glide_test.cpp`

**Interfaces:**
- Produces: `movement_flags::Gliding`, `MovementInfo::IsGliding()`, `player_fields::Vigor`, `player_fields::MaxVigor`, `game::client_realm_packet::MoveStartGlide/MoveStopGlide`, `game::realm_client_packet::MoveStartGlide/MoveStopGlide/ForceStopGlide`. Use the namespace names that already exist in `game_protocol.h`.
- Rule: Gliding is only valid together with `Falling`, so `jumpVelocity` is always serialized while gliding. No format change to `MovementInfo` beyond the flag.

- [ ] **Step 1: Write the failing test** `Gliding movement info round-trips with jump velocity`: write and read `MovementInfo{flags = Falling|Gliding|Forward, jumpVelocity=(3,-2.5,1)}` and check that the flags and velocity are equal.
- [ ] **Step 2: Run it and check it fails** (`game_protocol_tests "[movement]"`: `Gliding` undefined).
- [ ] **Step 3: Add the flag, fields and opcodes, wire realm/world/bot, bump the version, then run** `python tools/protocol_version_check.py --update`.
- [ ] **Step 4: Run** `game_protocol_tests` and `python tools/protocol_version_check.py`. Expected: PASS, and the check reports clean.
- [ ] **Step 5: Commit** `protocol: glide flag/opcodes, vigor player fields (version bump)`.

### Task 3: Data and server Vigor (Glider + Dash spells, DrainVigor aura, VigorTracker)

**Files:**
- Modify: `src/shared/game/aura.h`: `DrainVigor = 47` before `Count_`. Basepoints = Vigor per second.
- Create: `src/shared/game_server/objects/vigor_tracker.h/.cpp`. Owns the `VigorState`, sums the `DrainVigor` auras, and wraps `AdvanceVigor`.
- Modify: `src/shared/game_server/objects/game_player_s.{h,cpp}` (find the file that defines `GamePlayerS`). It owns a `VigorTracker`. MaxVigor is `settings.maxVigor` if the player knows the Glider spell or the Dash spell, otherwise 0. It is recomputed on spell learn/unlearn and on login (login = full).
- Modify: `src/shared/game_server/spells/aura_effect.cpp`: the `DrainVigor` apply/remove handler notifies the owner's tracker (settle first, then change the drain).
- Modify: `src/shared/game_server/objects/game_unit_s.cpp:3235-3256` (`SetInCombat`): settle Vigor on combat change.
- Modify: `src/mmo_edit` spell editor: add `DrainVigor` to the aura type name list.
- Modify: `src/shared/proto_data/area_triggers.proto` (`optional float updraft_strength = 14;`), `src/shared/client_data/area_triggers.proto` (`optional float updraft_strength = 12;`), the area trigger editor window (a float field), and the client-data export that copies area triggers (find it via `proto_client::AreaTriggerEntry` writers in `src/mmo_edit`).
- Data (scratchpad Python script against `data/editor/data/spells.data`, following the pattern of earlier visual passes such as `tools/scout_visuals`; commit in the data submodule):
  - **Glider** (passive, no effect beyond being known; next free spell id). Name/description localized enUS/deDE.
  - **Dash** (toggle like Stealth, see the memory note "disabled-while-active-spells"). `NotInCombat` attribute; effect ApplyAura `ModIncreaseSpeed` 35 + ApplyAura `DrainVigor` 12; aura interrupt `EnterCombat`; no duration; no cost; no cooldown; icon reuse is fine.
- Modify: `src/world_server/configuration.{h,cpp}`: `TraversalSettings` overrides are optional. Defaults are enough, so add nothing unless trivial.
- Test: `src/tests/game_server_tests/vigor_tracker_test.cpp`

**Interfaces:**
- Consumes: Task 1 `AdvanceVigor`, `SecondsUntilEmpty`, `VigorActivity`, `TraversalSettings`; Task 2 player fields.
- Produces:
  - `VigorTracker::Settle(GameTime now)`
  - `VigorTracker::SetGliding(GameTime now, bool gliding, bool inUpdraft)`
  - `VigorTracker::SetExtraDrain(GameTime now, float perSecond)`
  - `VigorTracker::SetInCombat(GameTime now, bool)`
  - `VigorTracker::SetGrounded(GameTime now, bool)`
  - `float VigorTracker::GetCurrent(GameTime now) const`
  - `GameTime VigorTracker::GetEmptyTime(GameTime now) const` (0 if not draining)
  - `signal<void()> VigorTracker::exhausted`, fired by the owner's timer at the empty time
  - Player: `bool GamePlayerS::CanUseTraversal(uint32 spellId) const`
  - Spell ids are recorded as constants `traversal_spells::Glider` and `traversal_spells::Dash` in `traversal.h`. They need the real ids from the data step.
- Replication: write `Vigor` with notify only on transitions (glide start/stop, drain change, exhaustion, regen reaching full, combat change). Between transitions the client predicts.
- On `exhausted`: remove all auras of type `DrainVigor` (this ends Dash), and if gliding, send `ForceStopGlide` (Task 4).

- [ ] **Step 1: Write failing tests**, tag `[vigor]`:
  - `Tracker drains while gliding and reports empty time`: start 100, glide at t=0 → current 95 (start cost 5, applied by `SetGliding(true)`), `GetEmptyTime` = 11875 ms (95 / 8 per s).
  - `Tracker regen waits for delay and grounded`.
  - `Extra drain from aura stacks with glide`.
  - `Combat stops regen`.
  - `Max vigor is zero without traversal spells` (player fixture with no spells → `MaxVigor` field 0; after learning Glider → 100).
  - `Entering combat removes Dash`: apply the Dash aura (use the test spell fixture pattern from `aura_spell_reference_lifetime`; keep the SpellEntry alive), `SetInCombat(true)` → the aura is gone.
- [ ] **Step 2: Run** `game_server_tests "[vigor]"` and check they fail.
- [ ] **Step 3: Implement** the tracker, aura handler, player ownership, editor fields, protos and data.
- [ ] **Step 4: Run** `game_server_tests` (all suites). Expected: PASS.
- [ ] **Step 5: Commit** the code (`game_server: vigor tracker, DrainVigor aura, updraft field`) and the data submodule (`spells: Glider, Dash`).

### Task 4: Server glide validation (world_server movement handler)

**Files:**
- Modify: `src/world_server/player.cpp` movement validation (~2455–2920):
  - **MoveStartGlide** is accepted only if the player is already `Falling`, knows Glider, Vigor > 0, is not swimming, and is not stunned or rooted. Otherwise count a violation and send `ForceStopGlide`. On accept: `tracker.SetGliding(true, inUpdraft)`, record the fall start height = current Y, and broadcast to observers.
  - **MoveStopGlide**: `SetGliding(false)`, restart the fall start height at the current Y, broadcast.
  - The `Gliding` flag may only appear and disappear through these opcodes. `MoveFallLand` and `MoveStartSwim` also end gliding (implicit stop, no violation).
  - **Speed check while gliding:** horizontal displacement / dt ≤ `glideForwardSpeed × 1.35`. While gliding, Y may rise only if `GetUpdraftStrength(serverUpdrafts, pos) > 0` (tolerance 0.5 m per packet).
  - **Grace:** gliding packets arriving more than 1.0 s after the tracker's empty time count one violation and trigger `ForceStopGlide`.
  - **Damage:** taking damage while gliding sends `ForceStopGlide`. Hook into the existing damage notification of the player unit.
  - Teleport, map change and logout clear glide state.
- Modify: world instance or map setup: build `std::vector<UpdraftVolume>` per map from `proto::AreaTriggers` where `updraft_strength > 0`. Keep it next to the existing area-trigger lookup (find the users of `proto::AreaTriggerEntry` in `src/world_server`/`game_server`).
- Test: `src/tests/game_server_tests/glide_validation_test.cpp`. If the validation is too tied to `Player`, extract the pure decision functions into `src/shared/game_server/movement/glide_rules.h/.cpp` first and test those:
  - `GlideStartDecision CanStartGlide(const MovementInfo& current, bool knowsGlider, float vigor, bool inCombatStunned)`
  - `bool IsGlideSpeedValid(const Vector3& from, const Vector3& to, GameTime dt, float updraftAtTo, const TraversalSettings&)`
  - `bool IsWithinEmptyGrace(GameTime now, GameTime emptyTime)`

**Interfaces:**
- Consumes: Tasks 1–3.
- Produces: the opcode behaviour that the client in Task 5 relies on. `ForceStopGlide` means "close the glider now and send MoveStopGlide".

- [ ] **Step 1: Write failing tests**, tag `[glide]`:
  - `Cannot start glide on the ground`, `Cannot start glide without Glider`, `Cannot start glide at zero vigor`.
  - `Glide speed within tolerance passes, 15 m/s fails`.
  - `Rising outside updraft fails, inside passes`.
  - `Glide past empty within grace is not a violation` (empty + 900 ms → ok; + 1100 ms → violation).
  - `Fall height restarts at glide stop`: helper `float ComputeFallDamagePercent(float startY, float landY, const Configuration&)`. Glide stop at Y=3 above ground → 0 %.
  - `Teleport clears glide` (on the tracker/player state, using a fixture as in the existing game_server tests).
- [ ] **Step 2: Run** `game_server_tests "[glide]"` and check they fail.
- [ ] **Step 3: Implement** the rules and wire them into `player.cpp`.
- [ ] **Step 4: Run** `game_server_tests` and `python tools/shutdown_check.py` (if a timer was added for exhaustion). Expected: PASS.
- [ ] **Step 5: Commit** `world_server: validate gliding, force stop on damage/exhaustion`.

### Task 5: Client glide movement (physics, input, remote sim)

**Files:**
- Modify: `src/shared/game_client/unit_movement.{h,cpp}`. When the owner's movement flags contain `Gliding`, `HandleFalling` uses `StepGlideVelocity` instead of gravity (`.cpp:1653+`, `NewFallVelocity` `.cpp:2445`). Add `void SetUpdraftProvider(std::function<float(const Vector3&)>)`.
- Modify: `src/shared/game_client/game_unit_c.{h,cpp}`:
  - `bool TryToggleGlide()`: while falling and the glider is usable → start; while gliding → stop.
  - `StartGlide()` / `StopGlide()` set or clear the flag and raise movement events `StartGlide` / `StopGlide`.
  - Landing or entering water clears gliding without a stop event (the server treats Land as an implicit stop).
  - Remote units: the flag from MovementInfo drives the same physics (`game_unit_c.cpp:739-748, 806-812`).
- Modify: `src/mmo_client/player_controller.cpp:585-619`. In `Jump()`, if the player is falling and not swimming, call `TryToggleGlide()` instead. Map the new movement events to the opcodes (~730–790). Handle `ForceStopGlide`. The glider is usable if the player knows `traversal_spells::Glider`, predicted Vigor > 0, and the player is not dead, rooted or stunned.
- Modify: `src/mmo_client/area_trigger_manager.{h,cpp}`: `float GetUpdraftStrength(const Vector3&) const`, built from `updraft_strength`. It is set as the provider for the own unit **and** for remote units.
- Modify: `src/mmo_client/net/realm_connector.{h,cpp}`: send the glide opcodes and receive the observer `MoveStartGlide/MoveStopGlide` and `ForceStopGlide`.
- Animation: while gliding, play the existing fall/jump-loop animation. There is no glider mesh in this plan (see the Out of scope section).
- Test: `src/tests/movement_tests/glide_movement_test.cpp`

**Interfaces:**
- Consumes: Tasks 1, 2 and 4 (opcode semantics).
- Produces: `GameUnitC::IsGliding() const`, used by Task 6 for prediction.

- [ ] **Step 1: Write failing tests**, tag `[glide]`, using the existing movement test harness in `movement_tests`:
  - `Own unit with Gliding flag sinks at glide speed` (after 2 s, vertical velocity is about -2.5).
  - `Remote unit with Gliding flag sinks at glide speed`.
  - `Updraft provider lifts a gliding unit`.
  - `Landing clears Gliding`.
- [ ] **Step 2: Run** `movement_tests "[glide]"` and check they fail.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `movement_tests` and `game_client_tests`. Expected: PASS.
- [ ] **Step 5: Commit** `client: glide movement, updrafts, glide opcodes`.

### Task 6: Client Vigor prediction, Lua API, UI, Dash key binding

**Files:**
- Modify: `src/mmo_client/game_states/world_state.{h,cpp}` (`UpdatePredictedResources` :969-997). Predict the own player's Vigor with `AdvanceVigor`, using `IsGliding()`, the updraft at the player's position, the summed `DrainVigor` aura basepoints of the own auras, `IsInCombat()`, and grounded. Re-anchor when the `Vigor` field changes. Raise the `PLAYER_VIGOR_CHANGED` Lua event at most 10 times per second, and when the predicted value hits 0, stop the glide (call `StopGlide`, which sends MoveStopGlide).
- Modify: `src/mmo_client/game_script.cpp`: Lua `UnitVigor(unit)` / `UnitVigorMax(unit)` (own player predicted; others 0), and `ToggleDash()` (casts or cancels `traversal_spells::Dash`).
- Modify: `data/client/Interface/Bindings.xml`: binding `DASH` → `ToggleDash()`, unbound by default (the player picks a key), category Movement.
- Create: GameUI vigor bar (`data/client/Interface/GameUI/VigorBar.xml` + `.lua`, added to the GameUI TOC). It is a slim bar centred above the action bar. It is hidden when `UnitVigorMax == 0`. It fades out 1.5 s after being full and turns red below 20 %. Use the material progress bar fill as the other bars do (memory note "ui-material-progress-bars").
- Modify: all `data/client/Locales/*/Localization.txt`: `VIGOR` (enUS "Vigor", deDE "Schwung"), `BINDING_NAME_DASH` (enUS "Dash", deDE "Spurt"), placeholders in other locales.
- Create: `tools/ui_test/scenarios/vigor_bar.lua` and mocks for `UnitVigor/UnitVigorMax` in `tools/ui_test/mocks/`.
- Test: the ui_test scenario renders without errors or unmocked calls. Add a unit test for the throttle and re-anchor only if the logic is extracted to a testable helper (`VigorPredictor` in `game_client`). Prefer extracting it, with tests `Predictor re-anchors on server value` and `Predictor reports empty once`.

**Interfaces:**
- Consumes: Tasks 1–5.

- [ ] **Step 1: Write failing tests** for `VigorPredictor` (`game_client_tests "[vigor]"`).
- [ ] **Step 2: Run them and check they fail.**
- [ ] **Step 3: Implement** the predictor, Lua API, binding, UI and locales.
- [ ] **Step 4: Run** `game_client_tests` and build `ui_test` Release. Run the vigor_bar scenario and check that `artifacts/ui_test/status.json` has no errors or unmocked calls. Look at `latest.png`.
- [ ] **Step 5: Commit** code and data (`client: vigor prediction, bar, Dash binding`).

### Task 7: Test field content on map "Collision" and end-to-end check

**Files:**
- Data: two or three area triggers on the Collision map (look up its map id in `data/editor/data/maps.data`) with `updraft_strength` 8. Put them next to the highest terrain point, or next to a tower of `Models/Prototyping/Cube.hmsh` props if the terrain is flat. Author them with mmo_edit or a scratchpad script. Commit in the data submodules.
- E2E (optional; only if `e2e` bots can learn spells via dev commands): `e2e/scenarios/traversal_dash.lua`. The bot learns Dash, casts it, its run speed rises by 35 %, attacking a dummy removes Dash, and its Vigor drained. Follow `e2e/scenarios/speed_change_movement_sync.lua`.

- [ ] **Step 1:** Author the updrafts (and the prop tower if needed).
- [ ] **Step 2:** Run the fast gate: `powershell -File tools/gate/verify.ps1 -Tier fast`. Expected: green, including the Linux step.
- [ ] **Step 3:** Check in the real client (memory note "client-visual-verification"): `.learn` Glider and Dash on a GM character, teleport to Collision, dash (bar drains, speed up), jump off a height, open the glider (slow sink, Vigor drains), fly into an updraft (rises), get to 0 (glider closes), and land without fall damage. Take screenshots.
- [ ] **Step 4: Commit** the data and E2E, then hand the branch to `/ship` only on the user's go.

## Out of scope (next steps from the spec)

Glider mesh and dedicated animations, glider quest line, vantage points and Vigor growth, Scout traversal passive, group slipstream, thermals from spells, mount system and categories.
