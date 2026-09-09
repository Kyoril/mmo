# Sitting Rest Buff Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A player who sits down gains a "Resting" buff (+50% health regen, +50% mana regen, +100% chance to be critically hit) that breaks on movement, damage, attacking or standing up — and eating/drinking now seats the character so their existing "must remain seated" rule finally applies.

**Architecture:** A new player trigger event fires on stand-state changes; a data-authored trigger reacts by casting a new spell whose three new aura types each accumulate into a cached scalar on `GameUnitS`, read by the regeneration ticks and the crit rolls. A new spell attribute seats the caster on client-initiated casts. No new machinery for the break conditions — the interrupt flags already exist and work.

**Tech Stack:** C++17, protobuf (proto2), Catch2 + CTest, Lua (e2e scenarios), Python 3 (data authoring scripts), CMake, MSVC on Windows.

## Global Constraints

- **Copyright header** on every source file touched or created: `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- **Code style:** Allman braces (every `{` and `}` on its own line); braces always, even for one-line bodies; **tabs** for indentation; `m_camelCase` members; `PascalCase` methods; `camelCase` locals; `snake_case.cpp/.h` filenames; `#pragma once`; Doxygen comments on public members; root namespace `mmo`.
- **No exceptions** (`SIMPLE_NO_EXCEPTIONS`). Use `ASSERT` / `VERIFY` / `UNREACHABLE` and `DLOG` / `WLOG` / `ELOG` from `src/shared/base/macros.h`.
- **`aura_type::Type` is append-only.** Values are serialized as raw integers in `spells.data`. Never reorder, never remove, always append before `Count_`.
- **`trigger_event::Type` is append-only** for the same reason (`triggers.data`). Append before `Invalid`.
- **No protocol version bump.** This work adds no opcode, changes no packet payload, and does not touch framing or the cipher. If you find yourself adding an opcode, stop — that requires bumping `mmo::auth::ProtocolVersion` / `mmo::game::ProtocolVersion`, running `python tools/protocol_version_check.py --update`, and recording it in `docs/protocol_versions.md`, and it is out of scope here.
- **Work happens in this worktree** (`H:\mmo\.claude\worktrees\sitting-player-buff-trigger-9163fa`) on branch `claude/sitting-player-buff-trigger-9163fa`. Never `cd` to `H:\mmo`. Never push to origin.
- **Never use bare `git stash` / `git stash pop`** — the stash stack is shared with other worktrees.
- **Spec:** `docs/superpowers/specs/2026-09-09-sitting-rest-buff-design.md`.
- **`<scratchpad>`** in this plan means the session scratchpad directory given in your
  environment prompt. Intermediate JSON drafts and one-off scripts go there, never into the
  repository.
- **`MMO_E2E_MYSQL_PASSWORD`** must be set before any e2e run. The value is recorded in the
  `e2e-test-harness` memory file; do not write it into any tracked file.

## Numbers fixed by this plan

Referenced by several tasks; they are decided here so no task has to invent one.

| Thing | Value |
|---|---|
| `trigger_event::OnPlayerStandStateChanged` | enum index **26** |
| `aura_type::ModHealthRegenPercent` | **38** |
| `aura_type::ModPowerRegenPercent` | **39** |
| `aura_type::ModCritChanceTaken` | **40** |
| `spell_attributes_b::SitsCaster` | **`1 << 10`** = 1024 |
| Resting spell id | **247** (next free; gaps at 117, 118, 151 are deliberately left alone) |
| Resting trigger id | **52** (next free; current max is 51) |
| Sit emote id | **4** (`standstate` 1, known by default) |
| Food / Drink spell ids | **143** / **59**, **60** |
| `unit_stand_state::Sit` | **1** |

## File Structure

**Task 1 — trigger event**
- Modify `src/shared/proto_data/trigger_helper.h` — append the enum value and its doc comment.
- Modify `src/shared/game_server/objects/game_unit_s.h` — `SetStandState` becomes a declaration.
- Modify `src/shared/game_server/objects/game_unit_s.cpp` — `SetStandState` definition, raising the event.
- Modify `src/mmo_edit/editor_windows/trigger_editor_window.cpp` — event name, filter-name table, summary text, data editor.
- Modify `src/tests/game_server_tests/trigger_event_filter_test.cpp` — stand-state filter coverage.

**Task 2 — aura types and cached scalars**
- Modify `src/shared/game/aura.h` — three appended aura types.
- Modify `src/shared/game_server/objects/game_unit_s.h` — three members, mutators, getters, two effective-value helpers.
- Modify `src/shared/game_server/objects/game_unit_s.cpp` — regen read sites, `CriticalHitChance` read site.
- Modify `src/shared/game_server/spells/aura_effect.h` / `.cpp` — three handlers plus dispatch entries.
- Modify `src/shared/game_server/spells/spell_effects.cpp` — weapon-damage spell crit read site.
- Modify `src/mmo_edit/editor_windows/spell_editor_window.cpp` — three names in `s_auraTypeNames`.
- Modify `.agents/skills/mmo-spell-designer/scripts/spell_catalog_lib.py` — three entries in `AURA_TYPE_NAMES` (the data-authoring validator rejects unknown aura ids, so Task 4 cannot run without this).
- Create `src/tests/game_server_tests/test_unit_factory.h` — one `MakeUnit` / `MakeSpell` shared by both aura suites.
- Modify `src/tests/game_server_tests/aura_effect_test.cpp` — use the shared factory instead of its own copies.
- Create `src/tests/game_server_tests/unit_aura_scalars_test.cpp` — the new coverage.

**Task 3 — sit on cast**
- Modify `src/shared/game/spell.h` — `spell_attributes_b::SitsCaster`.
- Modify `src/world_server/player.h` — declare `UpdateStandStateForCast`.
- Modify `src/world_server/player.cpp` — define it; call from `OnSpellCast`.
- Modify `src/world_server/player_inventory_handlers.cpp` — call from `OnUseItem`.
- Modify `src/mmo_edit/editor_windows/spell_editor_window.cpp` — attribute checkbox.
- Modify `.agents/skills/mmo-spell-designer/scripts/spell_catalog_lib.py` — `SPELL_ATTRIBUTE_FLAGS_1` entries.

**Task 4 — data (submodule `data/editor`)**
- Modify `data/editor/data/spells.data` — new spell 247; attribute edits on 143, 59, 60.
- Modify `data/editor/data/triggers.data` — new trigger 52.

**Task 5 — e2e and docs**
- Create `e2e/scenarios/sitting_rest_buff.lua`.
- Modify `e2e/README.md` — document the already-existing `DoEmote` / `GetStandState` / `CyclePose` / `GetSitPoseEmote` / `GM.LearnEmote` bindings.

**Task 6 — gate.**

---

### Task 0: Configure the worktree build

This worktree has no `build/` directory and no `data/editor` submodule checkout. Both are
needed by later tasks, and the quality gate assumes `build/` is already configured.

**Files:** none (environment only)

**Interfaces:**
- Consumes: nothing.
- Produces: a configured `build/` directory and a populated `data/editor/` submodule.

- [ ] **Step 1: Initialise the data submodule**

```bash
git submodule update --init data/editor
```

Expected: the submodule is checked out. Verify:

```bash
ls data/editor/data/spells.data
```

Expected: the file exists.

- [ ] **Step 2: Configure CMake**

```bash
cmake -S . -B build -DMMO_BUILD_CLIENT=ON -DMMO_BUILD_EDITOR=ON -DMMO_BUILD_TOOLS=ON -DMMO_WITH_DEV_COMMANDS=ON
```

Expected: ends with "Build files have been written to: .../build". `MMO_BUILD_EDITOR=ON` is
required because Tasks 1–3 modify editor sources that are otherwise not compiled;
`MMO_WITH_DEV_COMMANDS=ON` is required by the e2e harness in Task 5.

- [ ] **Step 3: Baseline build and test run**

```bash
cmake --build build --config Debug -t all_tests
```

Expected: succeeds. Then:

```bash
cd build && ctest -C Debug --output-on-failure
```

Expected: all suites pass. If anything is red *before* any change, stop and report it —
do not start implementing on a red baseline.

- [ ] **Step 4: No commit**

Nothing to commit; `build/` is not tracked and the submodule pointer is unchanged.

---

### Task 1: `OnPlayerStandStateChanged` trigger event

**Files:**
- Modify: `src/shared/proto_data/trigger_helper.h`
- Modify: `src/shared/game_server/objects/game_unit_s.h:1589-1600`
- Modify: `src/shared/game_server/objects/game_unit_s.cpp`
- Modify: `src/mmo_edit/editor_windows/trigger_editor_window.cpp`
- Test: `src/tests/game_server_tests/trigger_event_filter_test.cpp`

**Interfaces:**
- Consumes: `proto::TriggerEventDataMatches(const proto::TriggerEvent&, const std::vector<uint32>&)` from `shared/proto_data/trigger_event_filter.h`; `GamePlayerS::RaiseTrigger(trigger_event::Type, const std::vector<uint32>&, GameUnitS*)`.
- Produces: `trigger_event::OnPlayerStandStateChanged` (value 26), raised with `data = { <new stand state> }` and `triggeringUnit = the player itself`. Task 4 authors a trigger against it.

- [ ] **Step 1: Write the failing test**

Append to `src/tests/game_server_tests/trigger_event_filter_test.cpp`. Note that
`MakeEvent` in that file hardcodes `OnPlayerLevelUp` as the event type — the filter logic
does not read the type, so reuse it as-is rather than changing the helper.

```cpp
TEST_CASE("Stand state filter behaves as the sit trigger relies on", "[trigger_filter]")
{
	// Event data uses zero as a wildcard, so an unfiltered stand-state trigger fires on
	// every change and "Stand" (0) is not expressible as a filter. Sit is 1.
	const auto anyState = MakeEvent({});
	const auto onlySit = MakeEvent({ static_cast<uint32>(unit_stand_state::Sit) });

	for (uint32 state = 0; state < static_cast<uint32>(unit_stand_state::Count_); ++state)
	{
		CHECK(proto::TriggerEventDataMatches(anyState, { state }));
		CHECK(proto::TriggerEventDataMatches(onlySit, { state })
			== (state == static_cast<uint32>(unit_stand_state::Sit)));
	}
}

TEST_CASE("The stand state event has a value the data files can reference", "[trigger_filter]")
{
	// triggers.data stores the event type as a raw integer, so this value is frozen. Task 4
	// authors a trigger with event type 26; if this ever changes, that data breaks silently.
	CHECK(static_cast<uint32>(trigger_event::OnPlayerStandStateChanged) == 26);
	CHECK(trigger_event::OnPlayerStandStateChanged < trigger_event::Count_);
}
```

Add the include for the stand state enum at the top of the file, after the existing includes:

```cpp
#include "game/object_type_id.h"
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build --config Debug -t game_server_tests
```

Expected: FAILS to compile with an error naming `OnPlayerStandStateChanged` (it is not a
member of `mmo::trigger_event`).

- [ ] **Step 3: Append the enum value**

In `src/shared/proto_data/trigger_helper.h`, inside `namespace trigger_event`, insert
immediately **before** the `Invalid,` line (after the `OnPlayerLevelUp,` entry):

```cpp
			/// Executed on a player character right after its stand state changed. Requires the
			/// PlayerTrigger flag, since players carry no trigger list of their own.
			/// Data: [<STAND-STATE>]; When given, only fires on entering exactly that state.
			///
			/// Event data treats zero as a wildcard (see proto::TriggerEventDataMatches), so
			/// Stand (0) cannot be filtered on - an unfiltered trigger fires on every change,
			/// standing up included. Nothing needs to filter on standing up: auras flagged
			/// NotSeated are already removed by SetStandState itself.
			OnPlayerStandStateChanged,
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cmake --build build --config Debug -t game_server_tests && ./bin/Debug/game_server_tests "[trigger_filter]"
```

Expected: PASS, all `[trigger_filter]` assertions green.

- [ ] **Step 5: Move `SetStandState` out of the header**

In `src/shared/game_server/objects/game_unit_s.h`, replace the whole inline definition:

```cpp
		/// Sets the stand state of the unit.
		/// @param standState The new stand state.
		void SetStandState(const unit_stand_state::Type standState)
		{
			const unit_stand_state::Type previous = GetStandState();
			Set<uint32>(object_fields::StandState, standState);

			// Standing up interrupts auras flagged to break when the unit is no longer seated.
			if (previous != unit_stand_state::Stand && standState == unit_stand_state::Stand)
			{
				RemoveAurasByInterrupt(spell_aura_interrupt_flags::NotSeated);
			}
		}
```

with a declaration:

```cpp
		/// Sets the stand state of the unit.
		///
		/// Standing up removes auras flagged NotSeated, and a player whose stand state actually
		/// changed raises OnPlayerStandStateChanged.
		/// @param standState The new stand state.
		void SetStandState(unit_stand_state::Type standState);
```

- [ ] **Step 6: Define it in the .cpp, raising the event**

In `src/shared/game_server/objects/game_unit_s.cpp`, add the definition next to the other
`GameUnitS` members (put it immediately after `GameUnitS::RaiseTrigger`'s two definitions,
around line 375, so the raise and its only new caller sit together):

```cpp
	void GameUnitS::SetStandState(const unit_stand_state::Type standState)
	{
		const unit_stand_state::Type previous = GetStandState();
		Set<uint32>(object_fields::StandState, standState);

		// Standing up interrupts auras flagged to break when the unit is no longer seated.
		if (previous != unit_stand_state::Stand && standState == unit_stand_state::Stand)
		{
			RemoveAurasByInterrupt(spell_aura_interrupt_flags::NotSeated);
		}

		// Only players raise this: the base RaiseTrigger logs "not implemented", so raising it
		// for every creature spawned with a stand state would be pure log noise. The changed
		// check also keeps the constructor's SetStandState(Stand) - which writes the value the
		// field already holds - from raising anything during object creation.
		if (previous != standState && IsPlayer() && GetWorldInstance() != nullptr)
		{
			RaiseTrigger(trigger_event::OnPlayerStandStateChanged,
				{ static_cast<uint32>(standState) }, this);
		}
	}
```

- [ ] **Step 7: Build and run the whole suite**

```bash
cmake --build build --config Debug -t all_tests
```

Expected: succeeds. Then:

```bash
cd build && ctest -C Debug --output-on-failure
```

Expected: all suites pass.

- [ ] **Step 8: Add the editor event name**

In `src/mmo_edit/editor_windows/trigger_editor_window.cpp`, in `s_eventTypeNames`, change the
last entry from `"Player - On Level Up"` to `"Player - On Level Up",` and add after it:

```cpp
		"Player - On Stand State Changed"
```

The `static_assert` against `trigger_event::Count_` immediately below will now pass again.

- [ ] **Step 9: Add the stand-state filter name table**

In the same file, immediately after the existing `s_standStateNames` block and its
`static_assert`, add:

```cpp
	/// Stand states as an event *filter*. Trigger event data uses zero as a wildcard
	/// (see proto::TriggerEventDataMatches), so slot 0 reads "any state" here and Stand is
	/// not expressible as a filter.
	static const char* s_standStateFilterNames[] = {
		"Any", "Sit", "Sleep", "Dead", "Kneel"
	};

	static_assert(std::size(s_standStateFilterNames) == std::size(s_standStateNames),
		"stand state filter names must cover the same range as the stand state names");
```

- [ ] **Step 10: Add the event summary text**

In `DescribeEvent`, immediately after the `case trigger_event::OnPlayerLevelUp:` block (which
ends with `return "Player gained any level";`) and before `default:`, add:

```cpp
			case trigger_event::OnPlayerStandStateChanged:
				if (const int standState = GetEventDataValue(event, 0); standState > 0)
				{
					snprintf(buffer, sizeof(buffer), "Player entered stand state %s",
						standState < static_cast<int>(std::size(s_standStateNames))
							? s_standStateNames[standState] : "?");
					return buffer;
				}
				return "Player changed stand state";
```

- [ ] **Step 11: Add the event data editor**

In the event-data `switch`, immediately after the `case trigger_event::OnPlayerLevelUp:`
block (the one calling `DrawEventDataInt`) and before `default:`, add:

```cpp
				case trigger_event::OnPlayerStandStateChanged:
				{
					// Data: [<STAND-STATE>]; zero is the usual event-data wildcard, so "Stand"
					// cannot be filtered on and reads as "Any" here.
					DrawEventDataEnum(event, 0, "##StandStateChangedState", "Stand State",
						s_standStateFilterNames, static_cast<int>(std::size(s_standStateFilterNames)),
						"Requires the trigger's 'Player Trigger' flag, since players carry no trigger list of their own.");
					break;
				}
```

- [ ] **Step 12: Build the editor**

```bash
cmake --build build --config Debug -t mmo_edit
```

Expected: succeeds with no `static_assert` failures.

- [ ] **Step 13: Commit**

```bash
git add src/shared/proto_data/trigger_helper.h src/shared/game_server/objects/game_unit_s.h src/shared/game_server/objects/game_unit_s.cpp src/mmo_edit/editor_windows/trigger_editor_window.cpp src/tests/game_server_tests/trigger_event_filter_test.cpp
git commit -m "$(cat <<'MSG'
feat(triggers): raise OnPlayerStandStateChanged on a player's stand state change

Players carry no trigger list, so the event is found by the PlayerTrigger flag
the same way OnPlayerLevelUp is. Only players raise it: the base RaiseTrigger
logs "not implemented", so every creature spawned with a stand state would
otherwise be log noise.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
)"
```

---

### Task 2: Regeneration and crit-taken auras with cached scalars

**Files:**
- Modify: `src/shared/game/aura.h`
- Modify: `src/shared/game_server/objects/game_unit_s.h`
- Modify: `src/shared/game_server/objects/game_unit_s.cpp`
- Modify: `src/shared/game_server/spells/aura_effect.h`
- Modify: `src/shared/game_server/spells/aura_effect.cpp`
- Modify: `src/shared/game_server/spells/spell_effects.cpp`
- Modify: `src/mmo_edit/editor_windows/spell_editor_window.cpp`
- Modify: `.agents/skills/mmo-spell-designer/scripts/spell_catalog_lib.py`
- Test: `src/tests/game_server_tests/test_unit_factory.h` (create)
- Test: `src/tests/game_server_tests/aura_effect_test.cpp` (use the shared factory)
- Test: `src/tests/game_server_tests/unit_aura_scalars_test.cpp` (create)

**Interfaces:**
- Consumes: `aura_type::Type`; `AuraEffect::GetBasePoints()`, `AuraEffect::GetEffect()`, `AuraContainer::GetOwner()`; `GameUnitS::ModifyDodgeChanceBonus` as the shape to copy.
- Produces, all public on `GameUnitS`:
  - `void ModifyHealthRegenPercentBonus(float amount, bool apply)`
  - `void ModifyPowerRegenPercentBonus(PowerType powerType, float amount, bool apply)`
  - `void ModifyCritChanceTakenBonus(float amount, bool apply)`
  - `float GetCritChanceTakenBonus() const`
  - `float GetEffectiveHealthRegenPerTick() const`
  - `int32 GetEffectivePowerRegenPerTick(PowerType powerType, int32 amount) const`
  - and aura types `ModHealthRegenPercent` (38), `ModPowerRegenPercent` (39), `ModCritChanceTaken` (40), consumed by Task 4's spell data.

- [ ] **Step 1: Extract the shared test unit factory**

`aura_effect_test.cpp` already has file-local `MakeUnit` and `MakeSpell` helpers, and the new
suite needs the same two. Rather than a second copy that would immediately diverge (the new
suite needs non-zero regeneration values), lift them into a shared header.

Create `src/tests/game_server_tests/test_unit_factory.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game_server/objects/game_player_s.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/spells.pb.h"
#include "shared/proto_data/classes.pb.h"

#include <memory>

namespace mmo::test
{
	/// Builds a minimal GamePlayerS with a class entry set up so RefreshStats() and SetLevel()
	/// work without asserting.
	///
	/// GamePlayerS is final, so a test cannot reach its protected regeneration members through
	/// a subclass - suites that need to observe regeneration go through the public effective-
	/// value helpers on a unit built here.
	/// @param project The project the unit belongs to. Gains a class entry with id 1 if it has
	///	none yet.
	/// @param timers The timer queue the unit's countdowns run on.
	/// @param level The level to bring the unit to.
	/// @return The constructed player.
	inline std::shared_ptr<GamePlayerS> MakeUnit(proto::Project& project, TimerQueue& timers, const uint32 level = 1)
	{
		auto* cls = project.classes.getById(1);
		if (!cls)
		{
			cls = project.classes.add(1);
			if (cls)
			{
				cls->set_powertype(proto::ClassEntry_PowerType_MANA);

				// Non-zero flat regeneration, so percentage modifiers have something to scale.
				// With both at zero, every regeneration assertion would read 0 == 0 and pass
				// no matter what the modifier did.
				cls->set_healthregenpertick(10.0f);
				cls->set_basemanaregenpertick(20.0f);

				for (uint32 i = 0; i < level + 1; ++i)
				{
					auto* lbv = cls->add_levelbasevalues();
					lbv->set_health(100);
					lbv->set_mana(100);
					lbv->set_stamina(10);
					lbv->set_strength(10);
					lbv->set_agility(10);
					lbv->set_intellect(10);
					lbv->set_spirit(10);
				}
			}
		}

		auto unit = std::make_shared<GamePlayerS>(project, timers);
		unit->Initialize();
		if (cls)
		{
			unit->SetClass(*cls);
		}
		unit->SetLevel(level);
		return unit;
	}

	/// Builds a minimal spell with both attribute slots present, which the aura effect handlers
	/// read unguarded.
	/// @return The constructed spell entry.
	inline proto::SpellEntry MakeSpell()
	{
		proto::SpellEntry spell;
		spell.add_attributes(0);	// attributes_a
		spell.add_attributes(0);	// attributes_b
		return spell;
	}
}
```

Then in `src/tests/game_server_tests/aura_effect_test.cpp`, delete its whole anonymous
namespace containing `MakeUnit` and `MakeSpell`, add `#include "test_unit_factory.h"` to its
include block, and add `using namespace mmo::test;` next to the existing `using namespace mmo;`
so the call sites in that file need no other change.

- [ ] **Step 2: Verify the extraction changed no behaviour**

```bash
cmake --build build --config Debug -t game_server_tests && ./bin/Debug/game_server_tests "[aura_effect]"
```

Expected: PASS, exactly as before the extraction. `MakeUnit` now sets two regeneration fields
it did not set before; if any `[aura_effect]` assertion depends on those being zero, stop and
report it rather than weakening the new factory.

- [ ] **Step 3: Write the failing test**

Create `src/tests/game_server_tests/unit_aura_scalars_test.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "test_unit_factory.h"

#include "game_server/spells/aura_effect.h"
#include "game_server/spells/aura_container.h"
#include "game/aura.h"
#include "game/spell.h"
#include "game/item.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

using namespace mmo;
using namespace mmo::test;

TEST_CASE("ModHealthRegenPercent accumulates and unwinds exactly", "[aura_scalars]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers);

	// healthregenpertick is 10 and spiritperhealthregen is unset, so the base is exactly 10.
	CHECK(unit->GetEffectiveHealthRegenPerTick() == Approx(10.0f));

	unit->ModifyHealthRegenPercentBonus(50.0f, true);
	CHECK(unit->GetEffectiveHealthRegenPerTick() == Approx(15.0f));

	// A second aura of the same kind stacks additively.
	unit->ModifyHealthRegenPercentBonus(50.0f, true);
	CHECK(unit->GetEffectiveHealthRegenPerTick() == Approx(20.0f));

	// Misapplying both must land exactly back on the base, not merely near it: an aura that
	// leaks a fraction of its bonus on removal is a permanent stat gain per application.
	unit->ModifyHealthRegenPercentBonus(50.0f, false);
	unit->ModifyHealthRegenPercentBonus(50.0f, false);
	CHECK(unit->GetEffectiveHealthRegenPerTick() == Approx(10.0f));
}

TEST_CASE("ModPowerRegenPercent scales only the power type it names", "[aura_scalars]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers);

	unit->ModifyPowerRegenPercentBonus(power_type::Mana, 50.0f, true);

	CHECK(unit->GetEffectivePowerRegenPerTick(power_type::Mana, 20) == 30);
	CHECK(unit->GetEffectivePowerRegenPerTick(power_type::Energy, 20) == 20);

	unit->ModifyPowerRegenPercentBonus(power_type::Mana, 50.0f, false);
	CHECK(unit->GetEffectivePowerRegenPerTick(power_type::Mana, 20) == 20);
}

TEST_CASE("A regeneration bonus never accelerates rage decay", "[aura_scalars]")
{
	// Rage regenerates by *decaying* (amount -= 3). Scaling a negative amount would make a
	// "+50% regeneration" buff drain rage half again as fast.
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers);

	unit->ModifyPowerRegenPercentBonus(power_type::Rage, 50.0f, true);

	CHECK(unit->GetEffectivePowerRegenPerTick(power_type::Rage, -3) == -3);
	CHECK(unit->GetEffectivePowerRegenPerTick(power_type::Rage, 0) == 0);
	CHECK(unit->GetEffectivePowerRegenPerTick(power_type::Rage, 10) == 15);
}

TEST_CASE("ModCritChanceTaken raises every attacker's crit chance against the unit", "[aura_scalars]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto attacker = MakeUnit(project, timers);
	auto victim = MakeUnit(project, timers);

	const float before = attacker->CriticalHitChance(*victim, weapon_attack::BaseAttack);
	REQUIRE(before < 100.0f);

	victim->ModifyCritChanceTakenBonus(100.0f, true);
	CHECK(attacker->CriticalHitChance(*victim, weapon_attack::BaseAttack) == Approx(100.0f));

	// The bonus belongs to the victim, not the attacker: attacking the *attacker* is unchanged.
	CHECK(victim->CriticalHitChance(*attacker, weapon_attack::BaseAttack) == Approx(before));

	victim->ModifyCritChanceTakenBonus(100.0f, false);
	CHECK(attacker->CriticalHitChance(*victim, weapon_attack::BaseAttack) == Approx(before));
}

TEST_CASE("The aura effect handlers drive the cached scalars on apply and misapply", "[aura_scalars]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers);

	const proto::SpellEntry spell = MakeSpell();
	AuraContainer container(*unit, /*casterId=*/0, spell, /*duration=*/0, /*itemGuid=*/0);

	proto::SpellEffect critEffect;
	critEffect.set_aura(static_cast<uint32>(aura_type::ModCritChanceTaken));
	AuraEffect critAura(container, critEffect, timers, /*basePoints=*/100);

	proto::SpellEffect manaEffect;
	manaEffect.set_aura(static_cast<uint32>(aura_type::ModPowerRegenPercent));
	manaEffect.set_miscvaluea(static_cast<int32>(power_type::Mana));
	AuraEffect manaAura(container, manaEffect, timers, /*basePoints=*/50);

	critAura.HandleEffect(true);
	manaAura.HandleEffect(true);

	CHECK(unit->GetCritChanceTakenBonus() == Approx(100.0f));
	CHECK(unit->GetEffectivePowerRegenPerTick(power_type::Mana, 20) == 30);

	critAura.HandleEffect(false);
	manaAura.HandleEffect(false);

	CHECK(unit->GetCritChanceTakenBonus() == Approx(0.0f));
	CHECK(unit->GetEffectivePowerRegenPerTick(power_type::Mana, 20) == 20);
}

TEST_CASE("An out-of-range power type on a regen aura is rejected, not written past the array",
	"[aura_scalars]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers);

	const proto::SpellEntry spell = MakeSpell();
	AuraContainer container(*unit, /*casterId=*/0, spell, /*duration=*/0, /*itemGuid=*/0);

	proto::SpellEffect effect;
	effect.set_aura(static_cast<uint32>(aura_type::ModPowerRegenPercent));
	effect.set_miscvaluea(static_cast<int32>(power_type::Count_));
	AuraEffect aura(container, effect, timers, /*basePoints=*/50);

	aura.HandleEffect(true);

	// Bad data must be logged and dropped; every power type stays untouched.
	for (uint8 i = 0; i < static_cast<uint8>(power_type::Count_); ++i)
	{
		CHECK(unit->GetEffectivePowerRegenPerTick(static_cast<PowerType>(i), 20) == 20);
	}
}
```

- [ ] **Step 4: Run the test to verify it fails**

The `mmo_add_test` macro globs the directory, so no CMake edit is needed for the new file.

```bash
cmake --build build --config Debug -t game_server_tests
```

Expected: FAILS to compile — `ModHealthRegenPercent` is not a member of `mmo::aura_type`,
`GetEffectiveHealthRegenPerTick` is not a member of `GameUnitS`, and so on.

- [ ] **Step 5: Append the three aura types**

In `src/shared/game/aura.h`, replace the marker line and its surroundings:

```cpp
			ModStealth            = 37,

			// Add new aura types HERE (append only — never insert above an existing entry).

			Count_,
```

with:

```cpp
			ModStealth            = 37,

			/// Increases the owner's health regeneration per tick by a percentage. The aura's
			/// base points are the percentage (50 = +50%). Accumulated into a cached scalar on
			/// apply and misapply, so the regeneration tick costs one multiply and never walks
			/// the aura list.
			ModHealthRegenPercent = 38,

			/// Increases the owner's regeneration of one power type by a percentage. The
			/// effect's miscvaluea selects the power type (power_type::Type) and the base
			/// points are the percentage. Only positive regeneration is scaled: rage
			/// regenerates by decaying, and scaling that would drain it faster.
			ModPowerRegenPercent  = 39,

			/// Increases every attacker's chance to critically hit this unit. The aura's base
			/// points are flat percentage points (100 = every landed attack crits).
			ModCritChanceTaken    = 40,

			// Add new aura types HERE (append only — never insert above an existing entry).

			Count_,
```

- [ ] **Step 6: Add the cached scalars and their accessors**

In `src/shared/game_server/objects/game_unit_s.h`, immediately after the existing
`ModifyDodgeChanceBonus` declaration (around line 1301), add:

```cpp
		/// Adds or removes a percentage bonus to this unit's health regeneration per tick,
		///	driven by the ModHealthRegenPercent aura.
		///	@param amount The bonus in percent (e.g. 50.0f for +50%).
		///	@param apply true to add the bonus, false to remove it again.
		void ModifyHealthRegenPercentBonus(float amount, bool apply) { m_healthRegenPctBonus += apply ? amount : -amount; }

		/// Adds or removes a percentage bonus to this unit's regeneration of one power type,
		///	driven by the ModPowerRegenPercent aura.
		///	@param powerType The power type the bonus applies to.
		///	@param amount The bonus in percent (e.g. 50.0f for +50%).
		///	@param apply true to add the bonus, false to remove it again.
		void ModifyPowerRegenPercentBonus(const PowerType powerType, const float amount, const bool apply)
		{
			if (static_cast<uint8>(powerType) >= static_cast<uint8>(power_type::Count_))
			{
				return;
			}

			m_powerRegenPctBonus[static_cast<uint8>(powerType)] += apply ? amount : -amount;
		}

		/// Adds or removes a flat percentage-point bonus to every attacker's chance to
		///	critically hit this unit, driven by the ModCritChanceTaken aura.
		///	@param amount The bonus in percentage points (e.g. 100.0f to guarantee crits).
		///	@param apply true to add the bonus, false to remove it again.
		void ModifyCritChanceTakenBonus(float amount, bool apply) { m_critChanceTakenBonus += apply ? amount : -amount; }

		/// Gets the accumulated bonus in percentage points to any attacker's chance to
		///	critically hit this unit.
		[[nodiscard]] float GetCritChanceTakenBonus() const { return m_critChanceTakenBonus; }

		/// Gets the health regenerated per tick after aura percentage modifiers.
		///	@remark This exists as a public helper rather than as inline arithmetic in
		///	RegenerateHealth so it can be tested: the regeneration members are protected and
		///	GamePlayerS is final.
		[[nodiscard]] float GetEffectiveHealthRegenPerTick() const
		{
			return m_healthRegenPerTick * (1.0f + m_healthRegenPctBonus / 100.0f);
		}

		/// Applies the aura percentage modifier for a power type to a per-tick amount.
		///	@param powerType The power type being regenerated.
		///	@param amount The unmodified amount for this tick.
		///	@return The modified amount. Non-positive amounts are returned unchanged: rage
		///	regenerates by decaying, and scaling that would make a regeneration bonus drain it.
		[[nodiscard]] int32 GetEffectivePowerRegenPerTick(const PowerType powerType, const int32 amount) const
		{
			if (amount <= 0 || static_cast<uint8>(powerType) >= static_cast<uint8>(power_type::Count_))
			{
				return amount;
			}

			return static_cast<int32>(static_cast<float>(amount)
				* (1.0f + m_powerRegenPctBonus[static_cast<uint8>(powerType)] / 100.0f));
		}
```

And in the members section, immediately after `float m_dodgeChanceBonus = 0.0f;` (around
line 1760), add:

```cpp
		/// Accumulated health regeneration bonus in percent from auras (ModHealthRegenPercent).
		float m_healthRegenPctBonus = 0.0f;

		/// Accumulated regeneration bonus in percent per power type (ModPowerRegenPercent).
		std::array<float, power_type::Count_> m_powerRegenPctBonus{};

		/// Accumulated bonus in percentage points to any attacker's chance to crit this unit.
		float m_critChanceTakenBonus = 0.0f;
```

If `<array>` is not already included by `game_unit_s.h`, add `#include <array>` to its
include block.

- [ ] **Step 7: Use the helpers at the regeneration read sites**

In `src/shared/game_server/objects/game_unit_s.cpp`, in `RegenerateHealth()`, change:

```cpp
		health += m_healthRegenPerTick;
```

to:

```cpp
		health += GetEffectiveHealthRegenPerTick();
```

And in `RegeneratePower()`, change the tail of the function from:

```cpp
		AddPower(powerType, amount);
```

to:

```cpp
		AddPower(powerType, GetEffectivePowerRegenPerTick(powerType, amount));
```

- [ ] **Step 8: Use the crit-taken bonus in the melee crit read site**

In `src/shared/game_server/objects/game_unit_s.cpp`, in `CriticalHitChance`, change:

```cpp
		// Apply crit chance modifiers from talents/buffs
		critChance += GetTotalSpellMods(spell_mod_type::Flat, spell_mod_op::CritChance, 0);
		critChance *= (1.0f + GetTotalSpellMods(spell_mod_type::Pct, spell_mod_op::CritChance, 0) / 100.0f);

		return std::max(0.0f, std::min(critChance, 100.0f));
```

to:

```cpp
		// Apply crit chance modifiers from talents/buffs
		critChance += GetTotalSpellMods(spell_mod_type::Flat, spell_mod_op::CritChance, 0);
		critChance *= (1.0f + GetTotalSpellMods(spell_mod_type::Pct, spell_mod_op::CritChance, 0) / 100.0f);

		// The victim's own vulnerability (ModCritChanceTaken), added after the attacker's
		// percentage modifiers so a sitting target is not made *more* vulnerable by the
		// attacker's crit talents. This single site covers the whole melee attack table.
		critChance += victim.GetCritChanceTakenBonus();

		return std::max(0.0f, std::min(critChance, 100.0f));
```

- [ ] **Step 9: Use the crit-taken bonus in the weapon-damage spell crit read site**

In `src/shared/game_server/spells/spell_effects.cpp`, around line 1441, change:

```cpp
				// Get configurable crit chance and multiplier from combat settings
				float critChance = combatSettings.spell_weapon_default_crit_chance();
				std::uniform_real_distribution critDistribution(0.0f, 100.0f);

				executer.ApplySpellMod(spell_mod_op::CritChance, spell.id(), critChance);
```

to:

```cpp
				// Get configurable crit chance and multiplier from combat settings
				float critChance = combatSettings.spell_weapon_default_crit_chance();
				std::uniform_real_distribution critDistribution(0.0f, 100.0f);

				executer.ApplySpellMod(spell_mod_op::CritChance, spell.id(), critChance);

				// The target's own vulnerability (ModCritChanceTaken).
				//
				// The plain SchoolDamage effects do not roll for crit at all yet (they carry a
				// "TODO: Do real calculation including crit chance" instead), so this and the
				// melee attack table are the only two places a crit-taken bonus can apply. When
				// spell crit is implemented, it needs this line too.
				critChance += unitTarget.GetCritChanceTakenBonus();
```

- [ ] **Step 10: Declare the three aura handlers**

In `src/shared/game_server/spells/aura_effect.h`, immediately after
`void HandleModDodgeChance(bool apply) const;`, add:

```cpp
		void HandleModHealthRegenPercent(bool apply) const;

		void HandleModPowerRegenPercent(bool apply) const;

		void HandleModCritChanceTaken(bool apply) const;
```

- [ ] **Step 11: Define them and register them in the dispatch table**

In `src/shared/game_server/spells/aura_effect.cpp`, immediately after
`AuraEffect::HandleModDodgeChance`, add:

```cpp
	void AuraEffect::HandleModHealthRegenPercent(const bool apply) const
	{
		// Base points are the percentage bonus applied to health regenerated per tick.
		m_container.GetOwner().ModifyHealthRegenPercentBonus(static_cast<float>(GetBasePoints()), apply);
	}

	void AuraEffect::HandleModPowerRegenPercent(const bool apply) const
	{
		const int32 powerType = GetEffect().miscvaluea();

		if (powerType < 0 || powerType >= static_cast<int32>(power_type::Count_))
		{
			ELOG("AURA_TYPE_MOD_POWER_REGEN_PERCENT: Invalid power type " << powerType);
			return;
		}

		// Base points are the percentage bonus applied to that power's regeneration per tick.
		m_container.GetOwner().ModifyPowerRegenPercentBonus(
			static_cast<PowerType>(powerType), static_cast<float>(GetBasePoints()), apply);
	}

	void AuraEffect::HandleModCritChanceTaken(const bool apply) const
	{
		// Base points are flat percentage points added to any attacker's crit chance.
		m_container.GetOwner().ModifyCritChanceTakenBonus(static_cast<float>(GetBasePoints()), apply);
	}
```

And in the `kHandlers` table, immediately after the `ModDodgeChance` entry, add:

```cpp
			{ AuraType::ModHealthRegenPercent, [](AuraEffect& self, bool apply){ self.HandleModHealthRegenPercent(apply); } },
			{ AuraType::ModPowerRegenPercent,  [](AuraEffect& self, bool apply){ self.HandleModPowerRegenPercent(apply); } },
			{ AuraType::ModCritChanceTaken,    [](AuraEffect& self, bool apply){ self.HandleModCritChanceTaken(apply); } },
```

- [ ] **Step 12: Run the test to verify it passes**

```bash
cmake --build build --config Debug -t game_server_tests && ./bin/Debug/game_server_tests "[aura_scalars]"
```

Expected: PASS, all `[aura_scalars]` assertions green. If
`ModCritChanceTaken raises every attacker's crit chance` fails its `REQUIRE(before < 100.0f)`,
the level-1 test units are already at cap — raise their level in `MakeUnit` until they are
not, rather than weakening the assertion.

- [ ] **Step 13: Run the whole suite**

```bash
cmake --build build --config Debug -t all_tests
```

then

```bash
cd build && ctest -C Debug --output-on-failure
```

Expected: all suites pass.

- [ ] **Step 14: Add the editor aura names**

In `src/mmo_edit/editor_windows/spell_editor_window.cpp`, in `s_auraTypeNames`, change the
last entry `"ModStealth"` to `"ModStealth",` and add after it:

```cpp
		"ModHealthRegenPercent",
		"ModPowerRegenPercent",
		"ModCritChanceTaken"
```

The `static_assert` against `aura_type::Count_` below will pass again.

- [ ] **Step 15: Teach the data-authoring validator the new aura types**

In `.agents/skills/mmo-spell-designer/scripts/spell_catalog_lib.py`, in `AURA_TYPE_NAMES`,
after the `37: "ModStealth",` line add:

```python
    38: "ModHealthRegenPercent",
    39: "ModPowerRegenPercent",
    40: "ModCritChanceTaken",
```

Without this, `validate_spell_json.py` rejects the Resting spell in Task 4 with
"aura 38 is not a known aura enum", and `apply_spell_json.py` refuses to run.

- [ ] **Step 16: Build the editor**

```bash
cmake --build build --config Debug -t mmo_edit
```

Expected: succeeds.

- [ ] **Step 17: Commit**

```bash
git add src/shared/game/aura.h src/shared/game_server/objects/game_unit_s.h src/shared/game_server/objects/game_unit_s.cpp src/shared/game_server/spells/aura_effect.h src/shared/game_server/spells/aura_effect.cpp src/shared/game_server/spells/spell_effects.cpp src/mmo_edit/editor_windows/spell_editor_window.cpp .agents/skills/mmo-spell-designer/scripts/spell_catalog_lib.py src/tests/game_server_tests/test_unit_factory.h src/tests/game_server_tests/aura_effect_test.cpp src/tests/game_server_tests/unit_aura_scalars_test.cpp
git commit -m "$(cat <<'MSG'
feat(spells): regeneration-percent and crit-taken auras backed by cached scalars

Three appended aura types accumulate into scalars on the unit at apply and
misapply, the way ModDodgeChance already does, so the regeneration ticks and the
crit rolls read one value instead of walking the aura list. Rage decay is left
alone: scaling a negative amount would make a regeneration bonus drain it.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
)"
```

---

### Task 3: Seat the caster on eat and drink

**Files:**
- Modify: `src/shared/game/spell.h`
- Modify: `src/world_server/player.h`
- Modify: `src/world_server/player.cpp:2655-2660`
- Modify: `src/world_server/player_inventory_handlers.cpp:700-706`
- Modify: `src/mmo_edit/editor_windows/spell_editor_window.cpp`
- Modify: `.agents/skills/mmo-spell-designer/scripts/spell_catalog_lib.py`

**Interfaces:**
- Consumes: `GameUnitS::SetStandState` / `GetStandState` / `IsSitting` / `IsAlive`; `spell_attributes::CastableWhileSitting`.
- Produces: `spell_attributes_b::SitsCaster` (`1 << 10`), consumed by Task 4's data edits; `Player::UpdateStandStateForCast(const proto::SpellEntry&, bool standUpByDefault)`.

There is no unit test in this task: both call sites live in `world_server`, which has no test
suite and needs a live `Player` session. It is covered end to end in Task 5.

- [ ] **Step 1: Add the spell attribute**

In `src/shared/game/spell.h`, inside `namespace spell_attributes_b`, after the
`NotBreakCastInterruptAuras = 1 << 9,` entry, add:

```cpp
			/// A client-initiated cast of this spell seats the caster (stand state Sit) instead
			/// of standing them up. Used by food and drink, whose auras carry the NotSeated
			/// interrupt flag and so need a seated state to break out of.
			SitsCaster = 1 << 10,
```

- [ ] **Step 2: Declare the helper**

In `src/world_server/player.h`, next to the other private packet-handler helpers, add:

```cpp
		/// Updates the caster's stand state for a client-initiated cast of a spell.
		///
		/// This deliberately lives here and not in GameUnitS::CastSpell: only a cast the player
		/// asked for should move them. A server-side cast must not, or the trigger that casts
		/// the resting buff on a player who just sat down would stand them straight back up and
		/// cancel the aura it had just applied.
		///	@param spell The spell being cast.
		///	@param standUpByDefault When true, a spell that neither seats the caster nor is
		///	castable while seated stands the caster up. The spell-cast path passes true; the
		///	item-use path passes false, because using an item has never stood a character up.
		void UpdateStandStateForCast(const proto::SpellEntry& spell, bool standUpByDefault);
```

- [ ] **Step 3: Define the helper**

In `src/world_server/player.cpp`, immediately before `Player::OnSpellCast`, add:

```cpp
	void Player::UpdateStandStateForCast(const proto::SpellEntry& spell, const bool standUpByDefault)
	{
		if (!m_character->IsAlive())
		{
			return;
		}

		// attributes(1) is optional in the data, unlike attributes(0).
		const uint32 attributesB = spell.attributes_size() >= 2 ? spell.attributes(1) : 0;

		if ((attributesB & spell_attributes_b::SitsCaster) != 0)
		{
			if (m_character->GetStandState() != unit_stand_state::Sit)
			{
				m_character->SetStandState(unit_stand_state::Sit);
			}
			return;
		}

		if (!standUpByDefault)
		{
			return;
		}

		// A spell explicitly castable while seated leaves the caster where they are. Without
		// this check every cast stood the caster up, which made the attribute a no-op.
		if ((spell.attributes(0) & spell_attributes::CastableWhileSitting) != 0)
		{
			return;
		}

		if (m_character->GetStandState() != unit_stand_state::Stand)
		{
			m_character->SetStandState(unit_stand_state::Stand);
		}
	}
```

- [ ] **Step 4: Call it from the spell cast path**

In `src/world_server/player.cpp`, in `OnSpellCast`, replace:

```cpp
		// Casting a spell stands the character up.
		if (m_character->IsAlive() && m_character->GetStandState() != unit_stand_state::Stand)
		{
			m_character->SetStandState(unit_stand_state::Stand);
		}
```

with:

```cpp
		// Casting a spell normally stands the character up - unless the spell seats them, or
		// may be cast while seated.
		UpdateStandStateForCast(*spell, /*standUpByDefault=*/true);
```

- [ ] **Step 5: Call it from the item use path**

In `src/world_server/player_inventory_handlers.cpp`, in `OnUseItem`, inside the OnUse spell
loop, replace:

```cpp
			// Cast the spell
			uint64 time = spellEntry->casttime();
			SpellCastResult result = m_character->CastSpell(targetMap, *spellEntry, time, false, itemGuid);
```

with:

```cpp
			// Food and drink seat the character. Using an item has never stood a character up,
			// so this path only ever seats - it never stands anyone up.
			UpdateStandStateForCast(*spellEntry, /*standUpByDefault=*/false);

			// Cast the spell
			uint64 time = spellEntry->casttime();
			SpellCastResult result = m_character->CastSpell(targetMap, *spellEntry, time, false, itemGuid);
```

- [ ] **Step 6: Build the world server**

```bash
cmake --build build --config Debug -t world_server
```

Expected: succeeds. If `spell_attributes_b` is not visible in
`player_inventory_handlers.cpp`, it comes in through `player.h`; no new include should be
needed, but add `#include "game/spell.h"` if the compiler disagrees.

- [ ] **Step 7: Add the editor checkbox**

In `src/mmo_edit/editor_windows/spell_editor_window.cpp`, after the
`NotBreakCastInterruptAuras` checkbox line, add:

```cpp
				CHECKBOX_ATTR_PROP(1, "Seats The Caster", spell_attributes_b::SitsCaster);
```

Match the surrounding lines exactly — several of them are followed by a blank line and a
tooltip call; copy that shape from the `NotBreakCastInterruptAuras` line above it.

- [ ] **Step 8: Update the data-authoring flag table**

In `.agents/skills/mmo-spell-designer/scripts/spell_catalog_lib.py`, in
`SPELL_ATTRIBUTE_FLAGS_1`, after the `1 << 7: "IgnoreLineOfSight",` line add:

```python
    1 << 8: "CanOnlyTargetPlayers",
    1 << 9: "NotBreakCastInterruptAuras",
    1 << 10: "SitsCaster",
```

Bits 8 and 9 were already missing, which is why the inspector prints an incomplete attribute
list for spells that use them.

- [ ] **Step 9: Build the editor and run the suite**

```bash
cmake --build build --config Debug -t mmo_edit all_tests
```

then

```bash
cd build && ctest -C Debug --output-on-failure
```

Expected: build succeeds and all suites still pass.

- [ ] **Step 10: Commit**

```bash
git add src/shared/game/spell.h src/world_server/player.h src/world_server/player.cpp src/world_server/player_inventory_handlers.cpp src/mmo_edit/editor_windows/spell_editor_window.cpp .agents/skills/mmo-spell-designer/scripts/spell_catalog_lib.py
git commit -m "$(cat <<'MSG'
feat(spells): SitsCaster attribute so food and drink seat the character

Both client-initiated cast paths now route their stand-state decision through
one helper. This also honours CastableWhileSitting, which the spell-cast path
had been overriding on every cast, and leaves item use unable to stand a
character up - it never did.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
)"
```

---

### Task 4: Author the Resting spell, the trigger, and the Food/Drink flags

**Files:**
- Modify: `data/editor/data/spells.data` (submodule)
- Modify: `data/editor/data/triggers.data` (submodule)
- Create (scratch, not committed): a JSON draft and a Python script under the scratchpad directory.

**Interfaces:**
- Consumes: `aura_type::ModHealthRegenPercent` (38), `ModPowerRegenPercent` (39),
  `ModCritChanceTaken` (40), `spell_attributes_b::SitsCaster` (1024),
  `trigger_event::OnPlayerStandStateChanged` (26), and the updated
  `spell_catalog_lib.py` tables — all from Tasks 1–3.
- Produces: spell **247** ("Resting") and trigger **52**, consumed by Task 5's scenario.

The `data/editor` submodule is a separate git repository. Commit inside it first, then commit
the updated pointer in the main repo.

- [ ] **Step 1: Confirm the ids are still free**

```bash
python .agents/skills/mmo-spell-designer/scripts/inspect_spell_catalog.py --project-root . --section spell --spell-id 247 --pretty
```

Expected: reports that spell 247 does not exist (empty result or a "not found" message). If
it *does* exist, stop and pick the next free id, updating this task and Task 5 to match.

- [ ] **Step 2: Write the Resting spell JSON draft**

Write this to `<scratchpad>/resting_247.json` (use the session scratchpad directory, not the
repository):

```json
{
  "format": "mmo-spell",
  "version": 1,
  "spell": {
    "id": 247,
    "name": "Resting",
    "attributes": [
      0,
      512
    ],
    "effects": [
      {
        "index": 0,
        "type": 6,
        "basepoints": 50,
        "aura": 38
      },
      {
        "index": 1,
        "type": 6,
        "basepoints": 50,
        "aura": 39,
        "miscvaluea": 0
      },
      {
        "index": 2,
        "type": 6,
        "basepoints": 100,
        "aura": 40
      }
    ],
    "duration": 0,
    "aurainterruptflags": 17567755,
    "baseid": 247,
    "rank": 1,
    "description": "Resting. Health and mana regenerate 50% faster, but attackers always land critical hits. Lasts until you stand up, move, attack or take damage.",
    "icon": "Interface/Icons/Spells/Spell_Buff_01.htex",
    "auratext": "Health and mana regeneration increased by 50%. Attacks against you always critically hit.",
    "name_loc": [
      { "locale": 1, "value": "Ausruhen" },
      { "locale": 2, "value": "Resting" },
      { "locale": 3, "value": "Repos" },
      { "locale": 4, "value": "Отдых" }
    ],
    "description_loc": [
      { "locale": 1, "value": "Ausruhen. Gesundheit und Mana regenerieren 50% schneller, aber Angreifer landen immer kritische Treffer. Hält an, bis Ihr aufsteht, Euch bewegt, angreift oder Schaden nehmt." },
      { "locale": 2, "value": "Resting. Health and mana regenerate 50% faster, but attackers always land critical hits. Lasts until you stand up, move, attack or take damage." },
      { "locale": 3, "value": "Repos. La santé et le mana se régénèrent 50% plus vite, mais les attaquants portent toujours des coups critiques. Dure jusqu'à ce que vous vous leviez, bougiez, attaquiez ou subissiez des dégâts." },
      { "locale": 4, "value": "Отдых. Здоровье и мана восстанавливаются на 50% быстрее, но атакующие всегда наносят критические удары. Длится, пока вы не встанете, не сдвинетесь, не атакуете или не получите урон." }
    ],
    "auratext_loc": [
      { "locale": 1, "value": "Gesundheits- und Manaregeneration um 50% erhöht. Angriffe auf Euch treffen immer kritisch." },
      { "locale": 2, "value": "Health and mana regeneration increased by 50%. Attacks against you always critically hit." },
      { "locale": 3, "value": "Régénération de la santé et du mana augmentée de 50%. Les attaques contre vous sont toujours critiques." },
      { "locale": 4, "value": "Восстановление здоровья и маны увеличено на 50%. Атаки по вам всегда критические." }
    ]
  }
}
```

The two computed numbers:

- `attributes[1] = 512` = `NotBreakCastInterruptAuras` (`1 << 9`). Not optional: the trigger
  makes the *player* the caster, and `GameUnitS::CastSpell` runs
  `RemoveAurasByInterrupt(Cast)` on its caster. Without this, casting Resting would strip the
  food or drink aura that seated the player a moment earlier — both carry the `Cast` flag.
  `attributes[0] = 0`: in particular **not** `HiddenClientSide`, which
  `AuraContainer::IsVisible()` also reads and which would hide the buff icon.
- `aurainterruptflags = 17567755` = `0x010C100B`, the sum of
  `HitBySpell 0x00000001`, `Damage 0x00000002`, `Move 0x00000008`, `Attack 0x00001000`,
  `NotSeated 0x00040000`, `ChangeMap 0x00080000` and `DirectDamage 0x01000000`.
  Deliberately absent: `Cast (0x2000)`, so a player may cast while resting, and
  `Turning (0x10)`, so looking around does not break it.
- `duration = 0` means "never expires" (`AuraContainer::DoesExpire()` is `m_duration > 0`).
- `type: 6` on each effect is `ApplyAura`; `miscvaluea: 0` on effect 1 is `power_type::Mana`.

- [ ] **Step 3: Validate the draft**

```bash
python .agents/skills/mmo-spell-designer/scripts/validate_spell_json.py "<scratchpad>/resting_247.json" --project-root .
```

Expected: exits 0 with no errors. An "aura 38 is not a known aura enum" error means Task 2
Step 15 was skipped.

- [ ] **Step 4: Apply the draft**

```bash
python .agents/skills/mmo-spell-designer/scripts/apply_spell_json.py "<scratchpad>/resting_247.json" --project-root .
```

Expected: `OK: created spell 247 in .../spells.data`.

- [ ] **Step 5: Verify the applied spell**

```bash
python .agents/skills/mmo-spell-designer/scripts/inspect_spell_catalog.py --project-root . --section spell --spell-id 247 --pretty
```

Expected: three effects with `aura_name` `ModHealthRegenPercent`, `ModPowerRegenPercent`,
`ModCritChanceTaken`; `duration` absent or 0; `aurainterruptflags` 17567755.

- [ ] **Step 6: Add SitsCaster to Food and Drink**

Export, edit and re-apply each of the three spells. For **Food (143)**, whose current
`attributes` are `[0, 128]`:

```bash
python .agents/skills/mmo-spell-designer/scripts/export_spell_json.py --project-root . --spell-id 143 --output "<scratchpad>/food_143.json"
```

In that file set `"attributes": [134217728, 1152]`, where:
- `134217728` = `0x08000000` = `CastableWhileSitting`, which Food lacks today, so a seated
  player is not stood up by it;
- `1152` = `128 (IgnoreLineOfSight, already set) + 1024 (SitsCaster)`.

Then:

```bash
python .agents/skills/mmo-spell-designer/scripts/validate_spell_json.py "<scratchpad>/food_143.json" --project-root .
python .agents/skills/mmo-spell-designer/scripts/apply_spell_json.py "<scratchpad>/food_143.json" --project-root .
```

For **Drink (59)** and **Drink (60)**, both currently `[402653192, 128]`: export each to
`<scratchpad>/drink_59.json` / `<scratchpad>/drink_60.json`, set
`"attributes": [402653192, 1152]` (attributes[0] is unchanged — it already includes
`CastableWhileSitting`), then validate and apply each the same way.

- [ ] **Step 7: Verify the three spells**

```bash
python .agents/skills/mmo-spell-designer/scripts/inspect_spell_catalog.py --project-root . --section spell --spell-id 143 --pretty
python .agents/skills/mmo-spell-designer/scripts/inspect_spell_catalog.py --project-root . --section spell --spell-id 59 --pretty
python .agents/skills/mmo-spell-designer/scripts/inspect_spell_catalog.py --project-root . --section spell --spell-id 60 --pretty
```

Expected: each shows `attributes` ending in `1152`, and the effects/durations are otherwise
unchanged from before.

- [ ] **Step 8: Add the trigger**

There is no authoring script for `triggers.data`, so write a one-off. Save as
`<scratchpad>/add_resting_trigger.py` and run it once:

```python
"""One-off: add the 'Player - Sit Down Rest' trigger to data/editor/data/triggers.data."""

import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.abspath(".agents/skills/mmo-spell-designer/scripts"))
import proto_runtime

root = proto_runtime.find_project_root(".")
protoc = proto_runtime.find_protoc(root)
proto_dir = proto_runtime.find_proto_dir(root)

out = os.path.join(tempfile.gettempdir(), "mmo_pb_triggers")
os.makedirs(out, exist_ok=True)
subprocess.run(
    [str(protoc), f"-I{proto_dir}", f"--python_out={out}", "triggers.proto", "localization.proto"],
    check=True,
)
sys.path.insert(0, out)
import triggers_pb2

path = os.path.join(root, "data/editor/data/triggers.data")
triggers = triggers_pb2.Triggers()
with open(path, "rb") as handle:
    triggers.ParseFromString(handle.read())

assert all(entry.id != 52 for entry in triggers.entry), "trigger 52 already exists"

entry = triggers.entry.add()
entry.id = 52
entry.name = "Player - Sit Down Rest"
entry.flags = 0x0008          # trigger_flags::PlayerTrigger
entry.probability = 100

event = entry.newevents.add()
event.type = 26               # trigger_event::OnPlayerStandStateChanged
event.data.append(1)          # unit_stand_state::Sit

action = entry.actions.add()
action.action = 6             # trigger_actions::CastSpell
action.target = 1             # trigger_action_target::OwningObject (the player)
action.data.append(247)       # the Resting spell
action.data.append(0)         # trigger_spell_cast_target::Caster

with open(path, "wb") as handle:
    handle.write(triggers.SerializeToString())

print("OK: added trigger 52 to", path)
```

Run it from the worktree root:

```bash
python "<scratchpad>/add_resting_trigger.py"
```

Expected: `OK: added trigger 52 to .../triggers.data`.

- [ ] **Step 9: Verify the trigger**

```bash
python - <<'PY'
import os, subprocess, sys, tempfile
sys.path.insert(0, os.path.abspath(".agents/skills/mmo-spell-designer/scripts"))
import proto_runtime
root = proto_runtime.find_project_root(".")
protoc = proto_runtime.find_protoc(root)
proto_dir = proto_runtime.find_proto_dir(root)
out = os.path.join(tempfile.gettempdir(), "mmo_pb_triggers")
os.makedirs(out, exist_ok=True)
subprocess.run([str(protoc), f"-I{proto_dir}", f"--python_out={out}", "triggers.proto", "localization.proto"], check=True)
sys.path.insert(0, out)
import triggers_pb2
t = triggers_pb2.Triggers()
with open(os.path.join(root, "data/editor/data/triggers.data"), "rb") as f:
    t.ParseFromString(f.read())
for e in t.entry:
    if e.flags & 0x8:
        print(e.id, repr(e.name), "flags", e.flags)
        for ev in e.newevents:
            print("   event", ev.type, list(ev.data))
        for a in e.actions:
            print("   action", a.action, "target", a.target, list(a.data))
PY
```

Expected output includes:

```
52 'Player - Sit Down Rest' flags 8
   event 26 [1]
   action 6 target 1 [247, 0]
```

- [ ] **Step 10: Commit inside the submodule, then the pointer**

```bash
cd data/editor
git add data/spells.data data/triggers.data
git commit -m "$(cat <<'MSG'
data: Resting buff on sitting, and food/drink now seat the character

Spell 247 grants +50% health and mana regeneration and +100% chance to be
critically hit, breaking on movement, damage, attacking or standing up. Trigger
52 casts it when a player's stand state becomes Sit. Food and both Drink ranks
gain SitsCaster so their existing "must remain seated" rule finally applies.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
)"
cd ../..
git add data/editor
git commit -m "$(cat <<'MSG'
data: bump data/editor for the Resting buff and the eat/drink seated rule

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
)"
```

---

### Task 5: End-to-end scenario and harness docs

**Files:**
- Create: `e2e/scenarios/sitting_rest_buff.lua`
- Modify: `e2e/README.md`

**Interfaces:**
- Consumes: spell **247** and trigger **52** from Task 4; sit emote **4**; the existing
  harness bindings `DoEmote(emoteId)`, `GetStandState(guid)`, `HasAura(guid, spellId)`,
  `MoveTo`, `GM.LearnSpell`, `CastSpell`, `WaitUntil`, `Assert`, `Log`, `Me()`.
- Produces: nothing consumed by later tasks.

Scenarios are discovered by glob, so no registration is needed. Do **not** add the scenario
to `$expectedFailures` in `tools/e2e/e2e_run.ps1` — it is expected to pass.

- [ ] **Step 1: Write the scenario**

Create `e2e/scenarios/sitting_rest_buff.lua`:

```lua
-- Verifies the sitting rest buff end-to-end: the stand-state trigger casts it, and every
-- documented break condition removes it. Also covers the eat/drink seated rule through the
-- spell path.
--
-- Uses: emote 4 Sit (pose, known by default), spell 247 Resting (trigger-granted, never
-- learned), spell 59 Drink (SitsCaster + CastableWhileSitting).

local SIT = 4
local RESTING = 247
local DRINK = 59

local STAND_STATE_STAND = 0
local STAND_STATE_SIT = 1

local me = Me()

Assert(GetStandState(me) == STAND_STATE_STAND, "player should spawn standing")
Assert(not HasAura(me, RESTING), "player should not be resting while standing")

-- 1. Sitting down grants the buff. The player never learns spell 247 - the trigger casts it
--    server-side, which is why the trigger CastSpell action bypasses the known-spell check.
Assert(not HasSpell(RESTING), "the resting spell must not be a learnable player spell")

DoEmote(SIT)
Assert(WaitUntil(function() return GetStandState(me) == STAND_STATE_SIT end, 10000, "sitting"),
	"stand state should become Sit after the /sit emote")
Assert(WaitUntil(function() return HasAura(me, RESTING) end, 10000, "resting aura applied"),
	"sitting down should grant the resting buff")

-- 2. Moving breaks it (both by the Move interrupt flag and by standing the character up).
Assert(MoveTo(GetPosX(me) + 5, GetPosY(me), GetPosZ(me), 30000), "player should be able to walk away")
Assert(WaitUntil(function() return GetStandState(me) == STAND_STATE_STAND end, 10000, "stood up by moving"),
	"starting to move should stand the character up")
Assert(WaitUntil(function() return not HasAura(me, RESTING) end, 10000, "resting aura removed"),
	"moving should remove the resting buff")

-- 3. Sitting again grants it again: the trigger is not a one-shot.
DoEmote(SIT)
Assert(WaitUntil(function() return HasAura(me, RESTING) end, 10000, "resting aura re-applied"),
	"sitting down a second time should grant the resting buff again")

-- 4. Standing up voluntarily (the pose emote toggles) removes it via the NotSeated interrupt.
DoEmote(SIT)
Assert(WaitUntil(function() return GetStandState(me) == STAND_STATE_STAND end, 10000, "stood up"),
	"performing /sit again should stand the character up")
Assert(WaitUntil(function() return not HasAura(me, RESTING) end, 10000, "resting aura removed on stand"),
	"standing up should remove the resting buff")

-- 5. Drinking seats the character, so it both applies its own aura and trips the sit trigger.
--    This goes through the spell path rather than item use: the harness has no UseItem
--    binding, and the item path calls the same stand-state helper one line later.
GM.LearnSpell(DRINK)
Assert(WaitUntil(function() return HasSpell(DRINK) end, 10000, "Drink learned"),
	"player should know Drink after GM.LearnSpell")

Assert(CastSpell(DRINK, me), "Drink cast request should be accepted")
Assert(WaitUntil(function() return GetStandState(me) == STAND_STATE_SIT end, 10000, "seated by drinking"),
	"casting Drink should seat the character")
Assert(WaitUntil(function() return HasAura(me, DRINK) end, 10000, "drink aura applied"),
	"casting Drink should apply the drink aura")
Assert(WaitUntil(function() return HasAura(me, RESTING) end, 10000, "resting aura from drinking"),
	"being seated by Drink should also grant the resting buff")

-- 6. Moving breaks both: the drink aura by its own NotSeated/Move flags, the resting buff by
--    the same rule. This is the seated requirement that was dead data before.
Assert(MoveTo(GetPosX(me) + 5, GetPosY(me), GetPosZ(me), 30000), "player should be able to walk away again")
Assert(WaitUntil(function() return not HasAura(me, DRINK) end, 10000, "drink aura removed"),
	"moving while drinking should remove the drink aura")
Assert(WaitUntil(function() return not HasAura(me, RESTING) end, 10000, "resting aura removed again"),
	"moving should remove the resting buff")

Log("Sitting rest buff passed")
```

- [ ] **Step 2: Build the e2e stack**

```bash
cmake --build build --config Debug -t e2e_client login_server realm_server world_server
```

Expected: succeeds.

- [ ] **Step 3: Run just this scenario to verify it passes**

In PowerShell, with the password from the `e2e-test-harness` memory file:

```bash
$env:MMO_E2E_MYSQL_PASSWORD = "<the recorded password>"
powershell -File tools/e2e/e2e_run.ps1 -Scenario sitting_rest_buff
```

Expected: exit code 0 and a summary reporting 1/1 scenarios as expected. On failure, read
the transcript under `e2e/runtime/logs/` — it records every action and assertion — before
changing anything.

- [ ] **Step 4: Document the harness bindings the scenario relies on**

In `e2e/README.md`, in the Queries paragraph, after
`` `GetObjectState(g)` (the object's State field; doors: 0 = closed, 1 = open) `` add:

```markdown
`GetStandState(g)` (0 = Stand, 1 = Sit, 2 = Sleep, 3 = Dead, 4 = Kneel),
`GetSitPoseEmote(g)`, `GetMoodEmote(g)`
```

In the Actions paragraph, after `` `SendChat(msg)` `` add:

```markdown
`DoEmote(emoteId)` (emote 4 = Sit, a pose emote that toggles: performing it again
stands the character up), `CyclePose()`
```

In the GM commands paragraph, after `` `GM.LearnSpell(spellId)` `` add
`` `GM.LearnEmote(emoteId)` ``.

These bindings already existed and were used by `emote_smoke.lua`; only the documentation was
missing.

- [ ] **Step 5: Commit**

```bash
git add e2e/scenarios/sitting_rest_buff.lua e2e/README.md
git commit -m "$(cat <<'MSG'
test(e2e): cover the sitting rest buff and the eat/drink seated rule

Also documents the emote and stand-state harness bindings, which existed and
were already used by emote_smoke.lua but were missing from the API reference.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
)"
```

---

### Task 6: Quality gate

**Files:** none (verification only)

**Interfaces:**
- Consumes: everything from Tasks 1–5.
- Produces: a green `tools/gate/last_report.json` matching HEAD, which `/ship` requires.

- [ ] **Step 1: Confirm the tree is clean**

```bash
git status --porcelain
```

Expected: empty. The gate report must match HEAD, so nothing may be uncommitted.

- [ ] **Step 2: Run the gate**

```bash
$env:MMO_E2E_MYSQL_PASSWORD = "<the recorded password>"
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1
```

Expected: exit code 0. The protocol version check must pass without a bump — this work adds
no opcode and changes no payload. If it reports a wire change, something in the
implementation drifted from the plan; investigate rather than bumping the version.

- [ ] **Step 3: Report**

Summarise the gate result to the user, including the E2E scenario count, and hand over. Do
not merge to `develop` and do not push — `/ship` is the user's call.

---

## Behaviour accepted by this design

Worth stating so a reviewer does not file them as bugs:

- **A sitting player can still be missed, dodge and parry.** The melee attack table is a
  cumulative roll, so +100 crit-taken means every attack that *lands* crits. Making seated
  units unable to react is a separate combat-feel change and is out of scope.
- **Switching pose from Sit to Kneel keeps the buff.** `NotSeated` fires only on a transition
  into `Stand`, and the trigger fires only on entering `Sit`. Staying seated in a different
  pose is a reasonable reading of "remain seated".
- **Eating and drinking now also grant the resting buff**, crit vulnerability included. That
  is the point: one rule, no special-casing, and eating somewhere dangerous is a real
  decision.
- **Plain school-damage spells still never crit.** They carry a pre-existing
  `TODO: Do real calculation including crit chance` and are untouched here.
