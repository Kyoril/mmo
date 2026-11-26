# Glider Ungating Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the gaps that keep the glider GM-only: glide anti-cheat (rise tolerance, client timestamps), creatures against airborne players, and an E2E scenario that glides through the real server.

**Architecture:**
- The glide validator (`GlideMoveValidator`, pure) gets two changes:
  - The rise tolerance becomes a pool that only descent refills.
  - Its clock is clamped so client timestamps cannot run ahead of the server's receive clock.
- Creatures get one pure height rule (`IsAboveMeleeHeight`). The swing check and the chase use it, and the existing unreachable tracker then evades.
- The headless bot gets air physics: falling and gliding through the shared `StepGlideVelocity`, with updrafts from its project data and landing at a known height. It also gets Lua bindings and two scenarios.

**Tech Stack:** C++17, Catch2 (`game_server_tests`, a bot_core test suite), e2e_client Lua scenarios, `tools/e2e/e2e_run.ps1`.

**Spec:** `docs/superpowers/specs/2026-11-25-skywright-glider-questline-design.md` (Part 3)

## Global Constraints

- No wire change: the bot uses the existing opcodes `MoveJump`, `MoveStartGlide`, `MoveStopGlide`, `MoveHeartBeat`, `MoveFallLand` and `ForceStopGlide`. No protocol bump.
- Server tiers stay single-threaded. Every asio timer or work guard that is added gets a `Stop()`.
- A violation is still only counted (`RecordMovementViolation`); the kick policy is unchanged.
- E2E runs on the test stack (`tools/e2e/e2e_run.ps1`, needs `MMO_WITH_DEV_COMMANDS=ON` and `MMO_E2E_MYSQL_PASSWORD`).
- Code style:
  - Allman braces, tabs, `m_camelCase` members, PascalCase methods.
  - Doxygen on public members.
  - Copyright header on new files.
- New Lua API is documented in `e2e/README.md`.

## Decided by the builder (rulings on the spec's wording)

- **Rise tolerance as a pool.** The spec says to scale the rise allowance with elapsed time. A time-scaled allowance would still allow a steady climb at that rate. Instead, a tolerance pool of `RiseToleranceMeters` (0.5 m) is used up by rising and refilled only by sinking. A glider can then never end up more than 0.5 m above its lowest point plus carried and updraft lift, however many packets it sends. *(Cost if wrong: a legitimate client with height jitter of more than 0.5 m upward in a row would be flagged. The glide sinks at ≥ 1 m/s, so packet rounding stays far below that.)*
- **The clock clamp is cumulative, not per interval.** The validator's effective client time may not run more than `TimestampLeadMs` (1000 ms) ahead of the server's receive clock since `Begin`. Bunched packets (small receive gap, normal client gap) stay valid. Forged future timestamps can no longer buy horizontal distance or updraft rise.
- **Melee height applies only to airborne victims** (`Falling`, which includes gliding). Ground combat on slopes is unchanged.

## Review Focus

1. **Packets that arrive bunched after a lag spike:** the cumulative clamp must not flag them (client times are legitimate; receive times are close together).
2. **A glide that opens with jump carry, then climbs in an updraft, then leaves it:** the pool must not double-count. Carry and updraft are separate allowances; the pool only absorbs rounding.
3. **A creature fighting a player who jumps (a short fall) in melee:** a normal jump (about 1.3 m up) stays within melee height, so no evade and no missed swing.
4. **A bot whose glide is refused** (no glider) must answer `ForceStopGlide` with `MoveStopGlide` and fall. Otherwise the server keeps counting "still gliding after stop" violations.
5. **Landing at the given height while the nav-mesh walker is idle:** the movement controller must not send a second landing packet.

---

### Task 1: Glide validator — rise tolerance pool and clock clamp

**Files:**
- Modify: `src/shared/game_server/movement/glide_rules.h/.cpp`
- Modify: `src/world_server/player.cpp`: the `m_glideValidator.Begin` / `Check` calls in `ApplyGlideState` (around 4519 and 4542) pass `GetAsyncTimeMs()` as the receive time.
- Test: `src/tests/game_server_tests/glide_validation_test.cpp`

**Interfaces:**
- Produces:
  - `glide_rules::TimestampLeadMs = 1000`
  - `RiseToleranceMeters` is unchanged at 0.5 and is now the pool size.
  - `void GlideMoveValidator::Begin(const Vector3& position, GameTime time, GameTime receiveTime, float upwardSpeed, const TraversalSettings& settings);`
  - `Result GlideMoveValidator::Check(const Vector3& position, GameTime time, GameTime receiveTime, const std::vector<UpdraftVolume>& updrafts);`
  - `[[nodiscard]] GameTime ClampGlideClientTime(GameTime clientTime, GameTime beginClientTime, GameTime receiveTime, GameTime beginReceiveTime);`: returns `min(clientTime, beginClientTime + (receiveTime - beginReceiveTime) + TimestampLeadMs)`.
  - `IsGlideSpeedValid` changes its last parameter to the remaining tolerance: `float riseAllowance` replaces the fixed `RiseToleranceMeters + carriedRise`. The caller passes `pool + carriedRise`.
  - Update every existing call site and test of `IsGlideSpeedValid`; existing expectations must keep holding.

- [ ] **Step 1: Write the failing tests** (`glide_validation_test.cpp`)
  - `TEST_CASE("Small rises cannot add up to a climb without an updraft", "[glide]")`
    - Begin at y 50 with upward speed 0.
    - 20 checks 300 ms apart, each 0.4 m higher than the last, each moving 2 m horizontally.
    - The first check is Ok. One of the next two is a Violation; at the latest the check that brings the total rise above 0.5 m.
  - `TEST_CASE("Sinking refills the rise tolerance", "[glide]")`
    - Rise 0.4 (Ok), sink 1.0, rise 0.4 (Ok), rise 0.4 (Violation).
  - `TEST_CASE("Client timestamps cannot run ahead of the server clock", "[glide]")`
    - Begin with client time 1000 and receive time 1000.
    - A check with client time 5000 but receive time 1300, 30 m horizontally: Violation. The effective dt is 1300, which allows 17.55 m.
  - `TEST_CASE("Packets that arrive bunched after a lag spike stay valid", "[glide]")`
    - Begin with client 1000 and receive 1000.
    - Checks with client times 1500, 2000, 2500, all received at 2600, each 5 m further and 1.25 m lower: all Ok.
  - `TEST_CASE("The client time clamp", "[glide]")`
    - `ClampGlideClientTime(5000, 1000, 1300, 1000) == 2300`
    - `ClampGlideClientTime(1200, 1000, 1300, 1000) == 1200`

- [ ] **Step 2: Run, expect FAIL**

Run: `cmake --build build --config Debug -t game_server_tests && bin/Debug/game_server_tests "[glide]"`
Expected: the build fails (new signatures).

- [ ] **Step 3: Implement**

The validator keeps:
- `m_beginTime` and `m_beginReceiveTime`;
- `m_risePool`, which starts at `RiseToleranceMeters`;
- the effective last time, after clamping.

In `Check`:
- `time` is first replaced by `ClampGlideClientTime(...)`.
- Validation uses `riseAllowance = m_risePool + m_carriedRise`, plus the updraft term that already exists.
- After validating, outside an updraft:
  - a rise uses up carry first, then the pool;
  - a descent refills the pool by the descended height, up to `RiseToleranceMeters`.
- Inside an updraft the pool stays as it is.

- [ ] **Step 4: Run, expect PASS**

Run: `bin/Debug/game_server_tests`
Expected: the whole suite passes.

- [ ] **Step 5: Build world_server**

Run: `cmake --build build --config Debug -t world_server`
Expected: it builds.

- [ ] **Step 6: Commit**

`fix(server): glide rise tolerance is a pool and client glide timestamps are clamped`

### Task 2: Creatures against airborne players

**Files:**
- Modify: `src/shared/game_server/ai/unreachable_target.h/.cpp`: new static `IsAboveMeleeHeight`.
- Modify: `src/shared/game_server/objects/game_unit_s.cpp`: in the attack swing range check (around 4525), an airborne victim above melee height gives `AttackSwingEvent::OutOfRange`.
- Modify: `src/shared/game_server/ai/creature_ai_combat_state.cpp`: in `ChaseTarget`'s in-range branch (around 788-818), an airborne victim above melee height counts as unreachable (`m_unreachable.Update(true, now)`, evade through `ShouldEvade` as for the no-LOS case).
- Test: `src/tests/game_server_tests/unreachable_target_test.cpp`

**Interfaces:**
- Produces: `static bool UnreachableTargetTracker::IsAboveMeleeHeight(const GameUnitS& attacker, const GameUnitS& victim, float attackRange);`. It is true when `victim.GetMovementInfo().IsFalling()` and `victim.y - attacker.y > attackRange`. `attackRange` is the reach sum the caller already uses.
- For testing without units, a position overload: `static bool IsAboveMeleeHeight(const Vector3& attacker, const Vector3& victim, bool victimAirborne, float attackRange);`. The unit overload forwards to it.

- [ ] **Step 1: Write the failing tests** (`unreachable_target_test.cpp`, tag `[unreachable_target]`)
  - `TEST_CASE("An airborne victim high above is out of melee height", "[unreachable_target]")`
    - Attacker (0,0,0), victim (1,8,0), airborne, range 5: true.
  - `TEST_CASE("A jumping victim stays within melee height", "[unreachable_target]")`
    - Victim (1,1.3,0), airborne, range 5: false.
  - `TEST_CASE("A victim on the ground is never out of melee height", "[unreachable_target]")`
    - Victim (1,8,0), not airborne: false.
  - `TEST_CASE("A victim below the attacker is not out of melee height", "[unreachable_target]")`
    - Victim (1,-8,0), airborne: false.
  - If an existing fixture supports driving a creature's combat state in time (`cc_ai_test.cpp` `CCAIFixture` or `pet_ai_test.cpp` `PetAIFixture`), add `TEST_CASE("A creature evades a victim gliding above it", "[creature_ai]")`: victim flags `Falling | Gliding` via `ApplyMovementInfo`, 8 units above, creature in combat; after `UnreachableTargetTracker::EvadeAfter` plus one action interval the creature is no longer in combat state.
  - If neither fixture can advance time, ledger a ruling and rely on the pure tests plus the swing wiring.

- [ ] **Step 2: Run, expect FAIL**

Run: `cmake --build build --config Debug -t game_server_tests && bin/Debug/game_server_tests "[unreachable_target]"`
Expected: the build fails (`IsAboveMeleeHeight` is not declared).

- [ ] **Step 3: Implement**

Implement the helper and wire it into the swing check and `ChaseTarget`.

- [ ] **Step 4: Run, expect PASS**

Run: `bin/Debug/game_server_tests`
Expected: the whole suite passes.

- [ ] **Step 5: Commit**

`feat(server): creatures cannot melee airborne players high above and evade them`

### Task 3: Bot air physics (falling, gliding, landing, updrafts)

**Files:**
- Create: `src/shared/bot_core/bot_air_movement.h/.cpp`: pure air state and step.
- Modify: `src/shared/bot_core/bot_movement_controller.cpp`: around 117-121, no automatic landing while air movement is active.
- Modify: `src/shared/bot_core/bot_context.h/.cpp`:
  - owns a `BotAirMovement`;
  - steps it every tick;
  - sends heartbeats while airborne, every 250 ms, with `Falling` (and `Gliding`) flags and `jumpVelocity` = velocity;
  - sends `MoveFallLand` on landing.
- Modify: `src/shared/bot_core/bot_realm_connector.cpp`: handle `ForceStopGlide` for the own guid. Clear gliding, send `MoveStopGlide`, keep falling.
- Modify: `src/shared/bot_core/bot_nav_service.*` or a new `bot_updrafts.*`: updraft volumes from `proto::Project::areaTriggers` for the current map, with the same shape rules as `BuildUpdraftVolumes` (radius wins; `box_x/y/z` are full sizes, `box_o` facing; strength must be > 0).
- Test: the existing bot_core unit test suite (find it under `src/tests/`, e.g. `bot_ai_tests`; add `bot_air_movement_test.cpp`).

**Interfaces:**
- Produces:
  - `class BotAirMovement`:
    - `void Jump(const Vector3& position, float jumpSpeed, float landingHeight);`: velocity (0, jumpSpeed, 0); airborne.
    - `void OpenGlider();` / `void CloseGlider();`
    - `void SetGlideDirection(const Vector3& horizontalDirection);`
    - `struct Step { Vector3 position; Vector3 velocity; bool landed; };`
    - `Step Advance(float seconds, const std::vector<UpdraftVolume>& updrafts, const TraversalSettings& settings);`
      - Falling: gravity `glide_rules`-equal 19.6 down, capped at 55.
      - Gliding: `StepGlideVelocity` with `GetUpdraftStrength(updrafts, position)`.
      - Landing: when `position.y <= landingHeight` while descending; position is clamped to it, the unit is no longer airborne, and `landed = true`.
    - `bool IsAirborne() const; bool IsGliding() const; Vector3 GetVelocity() const;`
  - `std::vector<UpdraftVolume> BuildBotUpdraftVolumes(const proto::AreaTriggers& triggers, uint32 mapId);`
  - `bot_core` constants: `BotJumpSpeed = 7.0f`; `BotAirHeartbeatMs = 250`.

- [ ] **Step 1: Write the failing tests** (`bot_air_movement_test.cpp`)
  - `TEST_CASE("A bot jump rises, falls and lands at the landing height", "[bot_air]")`
    - Jump at y 10, speed 7, landing height 10.
    - Stepping 1/30 s: the peak is ≈ 10 + 7²/(2·19.6) = 11.25 (±0.1). It lands within 1 s with `landed` true and y exactly 10.
  - `TEST_CASE("A gliding bot sinks at the glide sink speed", "[bot_air]")`
    - Jump, `OpenGlider` at once, no direction, 3 s, landing height −1000: velocity.y ≈ −2.5 (±0.05).
  - `TEST_CASE("A gliding bot moves at the glide forward speed toward its direction", "[bot_air]")`
    - Direction (1,0,0): horizontal speed 10 after 0.5 s.
  - `TEST_CASE("An updraft lifts a gliding bot", "[bot_air]")`
    - Sphere updraft at the start position, radius 20, strength 8. After 2 s the bot is higher than at the start.
  - `TEST_CASE("Bot updraft volumes follow the server's shape rules", "[bot_air]")`
    - Triggers: one sphere with strength, one box (full size 4/6/8, o 1.0), one without strength, one on another map.
    - Expect two volumes: radius kept, half extents 2/3/4, orientation 1.0.

- [ ] **Step 2: Run, expect FAIL**

Run: build the bot test suite and run `"[bot_air]"`.
Expected: the build fails (`bot_air_movement.h` missing).

- [ ] **Step 3: Implement**

Implement `BotAirMovement`, `BuildBotUpdraftVolumes`, the context wiring, the controller guard and the `ForceStopGlide` handler.

- [ ] **Step 4: Run, expect PASS**

Run: the bot test suite (whole).
Expected: all pass.

- [ ] **Step 5: Build e2e_client**

Run: `cmake --build build --config Debug -t e2e_client`
Expected: it builds.

- [ ] **Step 6: Commit**

`feat(bot): air physics for jumping, gliding and updrafts`

### Task 4: Lua API, scenarios, E2E run

**Files:**
- Modify: `src/e2e_client/scenario_engine.cpp`:
  - bindings `Jump()`, `OpenGlider()`, `CloseGlider()`, `IsGliding()`, `IsFalling()`, `SetGlideDirection(x, z)`, `SetLandingHeight(y)`, `WaitUntilLanded(timeoutSeconds)` (blocking, returns true once landed, pumps like `luaMoveTo`);
  - `EquipBackpackItemTo` also accepts the glider slot (`player_glider_slots::Slot`).
- Modify: `e2e/README.md`: document the new functions and the glider slot.
- Create: `e2e/scenarios/traversal_glide.lua`
- Create: `e2e/scenarios/traversal_glide_refused.lua`

**Interfaces:**
- Consumes: `BotAirMovement` through the context (Task 3).

**Scenarios:**
- `traversal_glide.lua` (header `-- e2e-own-character: trg`):
  1. `GM.AddItem(181, 1)`, equip the glider to the glider slot, `GM.Heal`.
  2. Worldport to map 2 next to updraft trigger 11 (read its position from `data/editor` area triggers when writing the scenario; record the ground height there first by standing on it).
  3. Worldport 30 m above that ground, then `SetLandingHeight(ground)`, `Jump()`, `OpenGlider()`, and `SetGlideDirection` toward the updraft.
  4. Check that inside the updraft `GetPosY` rises over 1 s.
  5. `WaitUntilLanded(30)`.
  6. Expect: landed; health equals max health (no fall damage); the bot is still connected (no kick); Vigor below max (it drained).
- `traversal_glide_refused.lua` (header `-- e2e-own-character: trr`):
  1. No glider. Worldport 30 m up, `Jump()`, `OpenGlider()`.
  2. Expect `IsGliding()` false within 2 s (the server sent `ForceStopGlide` and the bot answered).
  3. `WaitUntilLanded(10)`: health below max (fall damage from 30 m).

- [ ] **Step 1: Add the bindings, README and scenarios**
- [ ] **Step 2: Build the E2E targets**

Run: `cmake --build build -t e2e_client login_server realm_server world_server --config Debug`
Expected: they build.

- [ ] **Step 3: Run both scenarios**

Run: `powershell -File tools/e2e/e2e_run.ps1 -Scenario traversal_glide`, then the same with `traversal_glide_refused`.
Expected: both green (exit 0). On red, read `e2e/runtime/logs/` (JSONL transcript and server logs) and debug under superpowers:systematic-debugging.

- [ ] **Step 4: Run the whole E2E suite once**

Run: `powershell -File tools/e2e/e2e_run.ps1`
Expected: exit 0. Expected failures are listed in the script; nothing new is red.

- [ ] **Step 5: Commit**

`test(e2e): glide through an updraft and a refused glide`

- [ ] **Step 6: Fast gate**

Run: `tools/gate/verify.ps1 -Tier fast` with `MMO_GATE_PYTHON` set.
Expected: GREEN.
