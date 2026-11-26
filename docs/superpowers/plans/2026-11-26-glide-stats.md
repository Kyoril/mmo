# Glide Stats Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Gliders carry four glide stats (glide speed, agility, lift, endurance). Server, own client and remote clients glide with the effective settings of the equipped glider.

**Architecture:**
- One shared pure function builds a player's effective `TraversalSettings` from the base settings and the glide stats of the equipped glider, with caps.
- On the server, `GamePlayerS` sets it on its `VigorTracker` whenever a glider is equipped or removed. The glide validator and the fall model already read their settings from there.
- On the client, `GamePlayerC` builds the settings from the glider's `ItemInfo`, which it already fetches for the glider's look. It hands them to `GameUnitC`; own-unit physics, remote extrapolation and the Vigor predictor then use them in place of default-constructed settings.
- Agility is a new turn rate in `StepGlideVelocity`: the horizontal glide direction turns toward the input at a bounded rate instead of instantly.

**Tech Stack:** C++17, Catch2 suites (`game_tests`, `game_server_tests`, `game_client_tests`, `movement_tests`), protobuf item data, frame_ui Lua tooltip, `ui_test`.

**Spec:** `docs/superpowers/specs/2026-11-25-skywright-glider-questline-design.md` (Part 2)

## Global Constraints

- No wire change: item stats already travel inside the item data. No protocol bump.
- The new item stat ids are 32–35. Ids 2–31 are retired and must not be reused; 64+ are attribute stats.
- Glide stats are percentages. Caps:

  | Stat | Range |
  |---|---|
  | Glide speed | 0..50 |
  | Agility | 0..100 |
  | Lift | 0..60 |
  | Endurance | 0..75 |

  Negative values count as 0. This keeps the sink speed above 0, so a glider never climbs without an updraft. It also keeps the horizontal speed inside what the validator allows for that player.
- Only the glide stats of the item in the glider slot count. Glide stats on other items do nothing.
- Glide stats apply regardless of durability, like the glider's look, so remote clients (which cannot see durability) agree with the server.
- The prototype (item 181) keeps no glide stats.
- Texts in all four locales (enUS, deDE, frFR, ruRU); deDE uses Ihr/Euch/Euer.
- Code style:
  - Allman braces, tabs, `m_camelCase` members, PascalCase methods.
  - Doxygen on public members.
  - Copyright header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`

## Review Focus

1. **Swapping one glider for another in mid-air:** the removal force-stops the glide. The next glide must use the new glider's settings, on server and client alike.
2. **A remote player's glider entry arrives before its item data:** the unit glides with the base settings until the data arrives, then switches. There must be no crash and no stale settings from the previous glider.
3. **Removing the glider:** settings fall back to the base values on server, own client and the Vigor predictor. A later prototype glide must not keep the old speed.
4. **Changing settings while the Vigor tracker runs:** drain up to that moment is settled at the old rate before the new rate applies.
5. **Data with glide stats above the caps or negative:** the effective settings are clamped. The tooltip still shows the authored value, and nothing breaks.

---

### Task 1: Glide stat types, modifiers and turn rate (shared `game` library)

**Files:**
- Modify: `src/shared/game/item.h`: `item_stat` gains `GlideSpeed = 32, GlideAgility = 33, GlideLift = 34, GlideEndurance = 35`, with a comment that 32–35 are glide stats.
- Modify: `src/shared/game/item.cpp`: `ItemInfo::GetStatType` returns `"GLIDE_SPEED"`, `"GLIDE_AGILITY"`, `"GLIDE_LIFT"`, `"GLIDE_ENDURANCE"` for 32–35. Use a switch instead of the index table, because the ids are not contiguous with 0–1.
- Create: `src/shared/game/glide_stats.h/.cpp`
- Modify: `src/shared/game/traversal.h/.cpp`: new field `float glideTurnRate = 3.0f;` (radians per second); `StepGlideVelocity` turns with it.
- Test: `src/tests/game_tests/glide_stats_test.cpp` (new), `src/tests/game_tests/traversal_test.cpp`

**Interfaces:**
- Produces, in `glide_stats.h` (namespace `mmo`):
  - `struct GlideModifiers { int32 speedPercent = 0; int32 agilityPercent = 0; int32 liftPercent = 0; int32 endurancePercent = 0; };`
  - `void AddGlideStat(GlideModifiers& modifiers, uint32 statType, int32 value);`: adds `value` to the matching field and ignores every other stat type.
  - `GlideModifiers GetGlideModifiers(const ItemInfo& item);`: sums all stats of the item through `AddGlideStat`.
  - `TraversalSettings ApplyGlideModifiers(const TraversalSettings& base, const GlideModifiers& modifiers);`: clamps each percentage to its range (see Global Constraints), then:
    - `glideForwardSpeed *= 1 + speed/100`
    - `glideTurnRate *= 1 + agility/100`
    - `glideSinkSpeed *= 1 - lift/100`
    - `glideDrainPerSecond` and `glideDrainInUpdraftPerSecond` each `*= 1 - endurance/100`
    - All other fields are copied unchanged.
- Produces: `TraversalSettings::glideTurnRate`, which Tasks 2 and 3 pass through unchanged.

- [ ] **Step 1: Write the failing tests**

`glide_stats_test.cpp`:
- `TEST_CASE("Glide stats scale the base glide settings", "[glide_stats]")`
  - modifiers {20, 50, 40, 25}
  - Expect `glideForwardSpeed` 12.0, `glideTurnRate` 4.5, `glideSinkSpeed` 1.5, `glideDrainPerSecond` 6.0, `glideDrainInUpdraftPerSecond` 3.0.
  - Expect `maxVigor` 100 and `glideStartCost` 5 unchanged (use `Approx`).
- `TEST_CASE("Glide stats are clamped to their caps", "[glide_stats]")`
  - {999, 999, 999, 999} gives forward 15.0, turn 6.0, sink 1.0, drain 2.0.
  - {-50, -50, -50, -50} equals the base values.
- `TEST_CASE("Only glide stat types add to the glide modifiers", "[glide_stats]")`
  - `AddGlideStat` with `item_stat::Mana`, `item_stat::FromAttribute(0)` and 31 leaves all fields 0.
  - 32..35 with value 10 set each field to 10.
- `TEST_CASE("Glider item data yields its glide modifiers", "[glide_stats]")`
  - An `ItemInfo` with `stats[0] = {GlideSpeed, 15}` and `stats[1] = {Health, 30}` gives speedPercent 15 and the other fields 0.
- `TEST_CASE("Glide stat types have localization keys", "[glide_stats]")`
  - `GetStatType` returns `"GLIDE_SPEED"` for index 0 of an `ItemInfo` whose `stats[0].type` is 32, and `"GLIDE_ENDURANCE"` for 35.

`traversal_test.cpp`:
- `TEST_CASE("Glide direction turns toward the input at the turn rate", "[traversal]")`
  - Velocity (10, -2.5, 0), input (0, 0, 1), default settings, 0.1 s.
  - The horizontal speed stays 10. The angle between the new horizontal velocity and +X is 0.3 rad (±0.001).
- `TEST_CASE("Glide direction takes the input direction when nearly standing still", "[traversal]")`
  - Velocity (0.2, -2.5, 0), input (0, 0, 1), 0.1 s, gives horizontal (0, _, 10).
- `TEST_CASE("Glide direction reaches the input when the turn is smaller than one step", "[traversal]")`
  - Velocity (10, _, 0), input (1, 0, 0.1), 0.1 s, gives exactly the input direction × 10.

- [ ] **Step 2: Run, expect FAIL**

Run: `cmake --build build --config Debug -t game_tests && bin/Debug/game_tests "[glide_stats],[traversal]"`
Expected: the build fails (`glide_stats.h` missing, `glideTurnRate` unknown).

- [ ] **Step 3: Implement**

Implement the stat ids, the `GetStatType` switch, `glide_stats.h/.cpp` and `glideTurnRate`.

`StepGlideVelocity` with input:
- If the current horizontal speed is below 1.0, use the input direction directly.
- Otherwise, rotate the current horizontal direction toward the input direction about the up axis by at most `glideTurnRate * seconds`. Use the shorter way; if the remaining angle fits in one step, land exactly on it.
- The magnitude is always `glideForwardSpeed`.

The no-input branch and the vertical part stay unchanged.

- [ ] **Step 4: Run, expect PASS**

Run: `bin/Debug/game_tests` (whole suite).
Expected: all pass. The existing "Glide velocity converges" case still passes.

- [ ] **Step 5: Run movement_tests**

Run: `cmake --build build --config Debug -t movement_tests && bin/Debug/movement_tests "[glide]"`
Expected: all pass. A glide starting from a straight fall snaps to the input.

- [ ] **Step 6: Commit**

`feat(game): glide stats and a glide turn rate`

### Task 2: The server glides with the equipped glider's settings

**Files:**
- Modify: `src/shared/game_server/objects/vigor_tracker.h/.cpp`
- Modify: `src/shared/game_server/objects/game_player_s.cpp`: the existing glider branch of `ApplyItemStats`, where `UpdateMaxVigor` / `gliderUnequipped` already happen. Do this outside the durability check.
- Create or modify: `glide_stats` gets a proto overload in `game_server`, `src/shared/game_server/glide_item_stats.h/.cpp`.
- Test: `src/tests/game_server_tests/glider_slot_test.cpp`, `vigor_tracker_test.cpp`, `glide_validation_test.cpp`

**Interfaces:**
- Consumes: `GlideModifiers`, `AddGlideStat`, `ApplyGlideModifiers` (Task 1).
- Produces:
  - `void VigorTracker::SetSettings(GameTime now, const TraversalSettings& settings);`: settles to `now` at the old rates, then replaces the settings. Current and max Vigor are unchanged.
  - `GlideModifiers GetGlideModifiers(const proto::ItemEntry& entry);` in `glide_item_stats.h`.
  - Behaviour: equipping a glider sets `ApplyGlideModifiers(TraversalSettings(), GetGlideModifiers(entry))`; removing it sets `TraversalSettings()`.

- [ ] **Step 1: Write the failing tests**

- `glider_slot_test.cpp`: `TEST_CASE("An equipped glider's glide stats set the player's glide settings", "[glider_slot]")`
  - Equip a test glider entry with stat `{GlideSpeed, 20}` and `{GlideLift, 40}`.
  - Expect `GetVigor().GetSettings().glideForwardSpeed` ≈ 12 and `glideSinkSpeed` ≈ 1.5.
  - Unequip it: both are back to 10 and 2.5.
- `vigor_tracker_test.cpp`: `TEST_CASE("Changing the settings settles the drain at the old rate first", "[vigor]")`
  - Glide for 1 s at drain 8, `SetSettings` with drain 4, glide 1 s more: current is 100 − 5 (start cost) − 8 − 4 = 83.
- `glide_validation_test.cpp`: `TEST_CASE("A faster glider is validated against its own forward speed", "[glide]")`
  - A validator begun with settings `glideForwardSpeed` 15 accepts 14 units in 1 s horizontally.
  - One begun with the defaults rejects it.

- [ ] **Step 2: Run, expect FAIL**

Run: `cmake --build build --config Debug -t game_server_tests && bin/Debug/game_server_tests "[glider_slot],[vigor],[glide]"`
Expected: `SetSettings` is not declared. The validator case already passes: the validator takes its settings at `Begin`, so this case pins existing behaviour. Note it in the ledger as a guard test.

- [ ] **Step 3: Implement**

Implement `SetSettings`, the proto overload and the glider branch in `ApplyItemStats`. Use `GetAsyncTimeMs()` for `now`.

- [ ] **Step 4: Run the whole suite, expect PASS**

Run: `bin/Debug/game_server_tests`
Expected: all pass.

- [ ] **Step 5: Commit**

`feat(server): the equipped glider's glide stats set the glide settings`

### Task 3: Clients glide with the glider's settings

**Files:**
- Modify: `src/shared/game_client/game_unit_c.h/.cpp`
- Modify: `src/shared/game_client/unit_movement.cpp`: lines 360 and 1678 replace `TraversalSettings()` with `m_movedUnit.GetTraversalSettings()`.
- Modify: `src/shared/game_client/remote_movement_renderer.h/.cpp`
- Modify: `src/shared/game_client/vigor_predictor.h/.cpp`
- Modify: `src/shared/game_client/game_player_c.cpp`: the glider branch of `NotifyItemData`, and `OnGliderChanged` when the entry is 0.
- Test: `src/tests/movement_tests/glide_movement_test.cpp`, `src/tests/game_client_tests/test_vigor_predictor.cpp`

**Interfaces:**
- Consumes: `GetGlideModifiers(const ItemInfo&)`, `ApplyGlideModifiers` (Task 1).
- Produces:
  - `void GameUnitC::SetTraversalSettings(const TraversalSettings& settings);`: stores the settings and forwards them to the remote movement renderer.
  - `[[nodiscard]] const TraversalSettings& GameUnitC::GetTraversalSettings() const;`
  - `void RemoteMovementRenderer::SetTraversalSettings(const TraversalSettings& settings);`
  - `void VigorPredictor::SetSettings(const TraversalSettings& settings);`
  - `GamePlayerC` behaviour:
    - When the glider's item data arrives for the current `VisibleGlider_0`, call `SetTraversalSettings(ApplyGlideModifiers(TraversalSettings(), GetGlideModifiers(data)))` and `m_vigorPredictor.SetSettings` with the same settings.
    - When `VisibleGlider_0` becomes 0, or a new entry is requested, both go back to `TraversalSettings()`, so no previous glider's settings linger while data is pending.

- [ ] **Step 1: Write the failing tests**

- `glide_movement_test.cpp`: `TEST_CASE("Own gliding unit moves at its glider's forward speed", "[glide]")`
  - Like "Own gliding unit moves at the glide forward speed with input", but call `unit->SetTraversalSettings` with `glideForwardSpeed` 15 first.
  - Expect a horizontal speed of 15.
- `glide_movement_test.cpp`: `TEST_CASE("Own gliding unit sinks at its glider's sink speed", "[glide]")`
  - `glideSinkSpeed` 1.0 gives a vertical speed of −1.0 after it settles.
- `glide_movement_test.cpp`: `TEST_CASE("Remote gliding unit sinks at its glider's sink speed", "[glide]")`
  - As above, through the remote path.
- `test_vigor_predictor.cpp`: `TEST_CASE("The predictor drains at the glider's rate after SetSettings", "[vigor_predictor]")`
  - Settings with `glideDrainPerSecond` 2.
  - Anchor 100, glide 1 s: 100 − 5 − 2 = 93.

- [ ] **Step 2: Run, expect FAIL**

Run: `cmake --build build --config Debug -t movement_tests game_client_tests && bin/Debug/movement_tests "[glide]" && bin/Debug/game_client_tests "[vigor_predictor]"`
Expected: the build fails (`SetTraversalSettings` and `SetSettings` are not declared).

- [ ] **Step 3: Implement**

Implement the client side as described in Interfaces.

- [ ] **Step 4: Run, expect PASS**

Run: `bin/Debug/movement_tests && bin/Debug/game_client_tests`
Expected: all pass.

- [ ] **Step 5: Build the client**

Run: `cmake --build build --config Debug -t mmo_client`
Expected: it builds.

- [ ] **Step 6: Commit**

`feat(client): own and remote gliding follow the glider's glide stats`

### Task 4: Tooltip, localization and editor

**Files:**
- Modify: `data/client/Locales/Locale_{enUS,deDE,frFR,ruRU}/Localization.txt`: keys `GLIDE_SPEED`, `GLIDE_AGILITY`, `GLIDE_LIFT`, `GLIDE_ENDURANCE`. The tooltip prints `"+%d %s"`, so each name begins with `% `:

  | Key | enUS | deDE | frFR | ruRU |
  |---|---|---|---|---|
  | `GLIDE_SPEED` | `% Glide Speed` | `% Gleitgeschwindigkeit` | `% vitesse de vol plané` | `% скорость планирования` |
  | `GLIDE_AGILITY` | `% Agility in Flight` | `% Wendigkeit` | `% agilité en vol` | `% манёвренность в полёте` |
  | `GLIDE_LIFT` | `% Lift` | `% Tragkraft` | `% portance` | `% подъёмная сила` |
  | `GLIDE_ENDURANCE` | `% Flight Endurance` | `% Flugausdauer` | `% endurance en vol` | `% выносливость в полёте` |
- Modify: `src/mmo_edit/editor_windows/item_editor_window.cpp`: replace the indexed `s_statTypeStrings` with a list of `{type, name}` pairs (Mana 0, Health 1, Glide Speed 32, Agility 33, Lift 34, Endurance 35). Use it for `GetItemStatTypeLabel` and the combo.
- Modify: `tools/ui_test/` — a scenario or mock that shows an item tooltip with a glide stat, if the existing tooltip scenarios allow it; otherwise extend the item mock of an existing tooltip scenario.

**Interfaces:**
- Consumes: the `GetStatType` keys (Task 1).

- [ ] **Step 1: Add the locale keys and the editor list**
- [ ] **Step 2: Build `mmo_edit` and `ui_test`**

Run: `cmake --build build --config Debug -t mmo_edit && cmake --build build --config Release -t ui_test`
Expected: both build.

- [ ] **Step 3: Check the tooltip in ui_test**

Show a tooltip of an item with `{GlideSpeed, 20}` in deDE.
Expected: a green line "+20 % Gleitgeschwindigkeit". `status.json` has no errors. Check enUS as well.

- [ ] **Step 4: Commit**

Commit `data/client` first, then the superproject: `feat(ui): glide stats in item tooltips and the item editor`

- [ ] **Step 5: Fast gate**

Run: `tools/gate/verify.ps1 -Tier fast` with `MMO_GATE_PYTHON` set.
Expected: GREEN.
