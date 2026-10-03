# Human Racial Abilities Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every human character knows three racial spells: the passives *Versatility* and *Used to Hard Work*, and the active party buff *Call of the Watch*. The active gets a new visualization and a generated sound.

**Architecture:**
- **Granting.** A new class-independent `RaceEntry.racialSpells` list is merged into the
  character's spell list in two places in the realm server: at character creation and in the
  login backfill. The merge goes through a small, unit-tested helper in `game_server`.
- **Persistence.** Racials have `classmask = 0` and no Proficiency effect, so the existing
  multi-class logic keeps them active across class switches.
- **Area-aura fix.** Party copies of area auras start with the caster aura's remaining time
  instead of the full duration.
- **Data.** All spell, race, visualization and sound data is written by one idempotent
  Python authoring script into the editor data and mirrored into ClientDB.

**Tech Stack:**
- C++17 (realm_server, game_server, mmo_edit), Catch2 (`game_server_tests`)
- protobuf data with Python authoring scripts
- `tools/particle_gen` for `.hpar` effects, `tools/sfx_gen` with the ElevenLabs connector
  for sound
- Lua E2E harness

**Spec:** [docs/superpowers/specs/2026-10-03-human-racial-abilities-design.md](../specs/2026-10-03-human-racial-abilities-design.md)

## Global Constraints

**Git workflow**
- Work happens on branch `feature/channel-end-notify`; do not create another branch.
- Data commits go into the submodules `data/client` and `data/editor`, directly on their
  `master` branch (both are on `master` and clean today).
- Never push to origin.
- Never stage the parent repo's `data/client` / `data/editor` gitlinks with `git commit -a`.
  Add paths explicitly. The gitlinks are bumped once, in Task 10.

**Code style**
- C++: Allman braces, braces on every `if`, tabs, `m_camelCase` members, PascalCase methods.
- Every source file starts with `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
  (Python: `# Copyright ...`).
- Doxygen on public declarations.
- No exceptions.

**Tooling**
- Python: use `py` (not `python`, which is the WindowsApps stub). The gate itself calls
  `python`; that is fine in its own environment.

**Ids**

| Kind | Id | Name |
|---|---|---|
| spell | 248 | Versatility |
| spell | 249 | Used to Hard Work |
| spell | 250 | Call of the Watch |
| visualization | 78 | "Human - Call of the Watch" |
| SoundEntry | 135 | "Human - Call of the Watch" |
| race | 0 | Human |

Current maxima: spell 247, visualization 77, sound 134.

**Spell data**
- Racemask Human = `0x80000000` (2147483648), from the `1 << (id - 1)` encoding with race id 0.
- Racials use `classmask` 0.
- Locales on every spell row: `name_loc` / `description_loc` / `auratext_loc` with
  1 = deDE, 2 = English, 3 = frFR, 4 = ruRU (as on the existing spells).
- Icons:
  - Versatility: `Interface/Icons/Spells/T_Icon_Gold_17.htex`
  - Used to Hard Work: `Interface/Icons/Spells/T_Icon_Gold_07.htex`
  - Call of the Watch: `Interface/Icons/Spells/T_Icon_Gold_84.htex`

**Visualization rules** (from earlier spell-visual rounds)
- No `duration_ms` on animation kits.
- Kits use `sound_ids`, never `sounds`.
- No sound on aura events.
- No looping particles outside the Casting (2) / AuraIdle (8) events.
- Particle materials only from `Particles/Particle_*.hmi`.

**Network protocol:** no wire change, so no `ProtocolVersion` bump.

---

### Task 1: `racialSpells` field and the racial-spell helper

**Files:**
- Modify: `src/shared/proto_data/races.proto:73-76`
- Create: `src/shared/game_server/racial_spells.h`
- Create: `src/shared/game_server/racial_spells.cpp`
- Test: `src/tests/game_server_tests/racial_spells_test.cpp`

**Interfaces:**
- Produces:
  - `std::vector<uint32> mmo::AppendRacialSpells(const proto::RaceEntry& race, const proto::SpellManager& spells, std::vector<uint32>& spellIds)` — appends every racial spell that exists and is not yet listed, in list order. Returns the ids it appended.
  - `bool mmo::IsActionBarAbility(const proto::SpellEntry& spell)` — true for a non-passive, non-`HiddenClientSide` spell with the `Ability` attribute (the rule `realm_server/player.cpp` applies today).
  - Proto: `RaceEntry::racialspells()` (field `racialSpells = 22`).

- [ ] **Step 1: Add the proto field**

In `src/shared/proto_data/races.proto`, after `optional VoiceLineSet female_voice = 21;`:

```proto
	// Spells every character of this race knows regardless of class (racial abilities).
	// Granted at character creation and backfilled on login. Racial spells should carry
	// classmask 0 so they stay active across class switches.
	repeated uint32 racialSpells = 22;
```

Do not touch `src/shared/client_data/races.proto`. The client does not need the list, and its
parser ignores the unknown field when ClientDB is a copy of the editor blob.

- [ ] **Step 2: Write the failing test**

Create `src/tests/game_server_tests/racial_spells_test.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/racial_spells.h"

#include "game/spell.h"
#include "shared/proto_data/project.h"

#include "catch.hpp"

using namespace mmo;

namespace
{
	proto::SpellEntry* AddSpell(proto::Project& project, const uint32 id, const uint32 attributesA)
	{
		proto::SpellEntry* spell = project.spells.add(id);
		spell->set_name("Spell " + std::to_string(id));
		spell->add_attributes(attributesA);
		spell->add_attributes(0);
		return spell;
	}
}

TEST_CASE("AppendRacialSpells appends known racial spells in list order", "[racial_spells]")
{
	proto::Project project;
	AddSpell(project, 248, spell_attributes::Passive);
	AddSpell(project, 250, spell_attributes::Ability);

	proto::RaceEntry race;
	race.add_racialspells(248);
	race.add_racialspells(250);

	std::vector<uint32> spellIds{ 37, 153 };
	const std::vector<uint32> added = AppendRacialSpells(race, project.spells, spellIds);

	CHECK(added == std::vector<uint32>{ 248, 250 });
	CHECK(spellIds == std::vector<uint32>{ 37, 153, 248, 250 });
}

TEST_CASE("AppendRacialSpells never duplicates a spell the character already knows", "[racial_spells]")
{
	proto::Project project;
	AddSpell(project, 248, spell_attributes::Passive);

	proto::RaceEntry race;
	race.add_racialspells(248);
	race.add_racialspells(248);

	std::vector<uint32> spellIds{ 248 };
	const std::vector<uint32> added = AppendRacialSpells(race, project.spells, spellIds);

	CHECK(added.empty());
	CHECK(spellIds == std::vector<uint32>{ 248 });
}

TEST_CASE("AppendRacialSpells skips spell ids missing from the project", "[racial_spells]")
{
	proto::Project project;
	AddSpell(project, 249, spell_attributes::Passive);

	proto::RaceEntry race;
	race.add_racialspells(9999);
	race.add_racialspells(249);

	std::vector<uint32> spellIds;
	const std::vector<uint32> added = AppendRacialSpells(race, project.spells, spellIds);

	CHECK(added == std::vector<uint32>{ 249 });
	CHECK(spellIds == std::vector<uint32>{ 249 });
}

TEST_CASE("IsActionBarAbility accepts only visible non-passive abilities", "[racial_spells]")
{
	proto::Project project;
	CHECK(IsActionBarAbility(*AddSpell(project, 1, spell_attributes::Ability)));
	CHECK_FALSE(IsActionBarAbility(*AddSpell(project, 2, spell_attributes::Passive)));
	CHECK_FALSE(IsActionBarAbility(*AddSpell(project, 3, spell_attributes::Ability | spell_attributes::Passive)));
	CHECK_FALSE(IsActionBarAbility(*AddSpell(project, 4, spell_attributes::Ability | spell_attributes::HiddenClientSide)));
	CHECK_FALSE(IsActionBarAbility(*AddSpell(project, 5, 0)));

	proto::SpellEntry noAttributes;
	CHECK_FALSE(IsActionBarAbility(noAttributes));
}
```

`mmo_add_test` globs the directory, so no CMake edit is needed. Check that `project.spells.add(id)` is the `TemplateManager` API: it is used the same way in `test_unit_factory.h` (`project.classes.add(1)`).

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake --build build --config Debug -t game_server_tests`
Expected: compile error, `game_server/racial_spells.h` not found.

- [ ] **Step 4: Write the header**

Create `src/shared/game_server/racial_spells.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "shared/proto_data/project.h"

#include <vector>

namespace mmo
{
	/// Appends the racial spells of a race to a character's spell list.
	///
	/// Racial spells are class-independent: every character of the race knows them. Spells
	/// already in the list are not added twice, and ids missing from the spell manager are
	/// skipped so stale race data cannot hand out spells that do not exist.
	///
	/// @param race The race whose racialSpells list is merged.
	/// @param spells The spell manager used to validate the ids.
	/// @param spellIds The character's spell list; receives the missing racial spells in list order.
	/// @return The ids that were appended.
	std::vector<uint32> AppendRacialSpells(const proto::RaceEntry& race, const proto::SpellManager& spells, std::vector<uint32>& spellIds);

	/// Determines whether a spell is placed on a fresh action bar automatically: a visible,
	/// non-passive spell with the Ability attribute.
	///
	/// @param spell The spell to check.
	/// @return true if the spell belongs on a default action bar.
	bool IsActionBarAbility(const proto::SpellEntry& spell);
}
```

- [ ] **Step 5: Write the implementation**

Create `src/shared/game_server/racial_spells.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "racial_spells.h"

#include "game/spell.h"

#include <algorithm>

namespace mmo
{
	std::vector<uint32> AppendRacialSpells(const proto::RaceEntry& race, const proto::SpellManager& spells, std::vector<uint32>& spellIds)
	{
		std::vector<uint32> added;

		for (const uint32 spellId : race.racialspells())
		{
			if (spells.getById(spellId) == nullptr)
			{
				continue;
			}

			if (std::find(spellIds.begin(), spellIds.end(), spellId) != spellIds.end())
			{
				continue;
			}

			spellIds.push_back(spellId);
			added.push_back(spellId);
		}

		return added;
	}

	bool IsActionBarAbility(const proto::SpellEntry& spell)
	{
		if (spell.attributes_size() == 0)
		{
			return false;
		}

		const uint32 attributes = spell.attributes(0);
		return (attributes & spell_attributes::Passive) == 0 &&
			(attributes & spell_attributes::HiddenClientSide) == 0 &&
			(attributes & spell_attributes::Ability) != 0;
	}
}
```

If `spells.getById` is not `const`-callable, check `TemplateManager` in
`src/shared/proto_data/` and use its const lookup. `realm_server/player.cpp` calls
`m_project.spells.getById(...)` from const methods (`BuildDefaultActionButtons` is const),
so a const overload exists.

- [ ] **Step 6: Re-run CMake (new .cpp files are globbed), build and run the tests**

Run:
```bash
cmake -S . -B build
cmake --build build --config Debug -t game_server_tests
./bin/Debug/game_server_tests "[racial_spells]"
```
Expected: `All tests passed (… assertions in 4 test cases)`.

- [ ] **Step 7: Commit**

```bash
git add src/shared/proto_data/races.proto src/shared/game_server/racial_spells.h src/shared/game_server/racial_spells.cpp src/tests/game_server_tests/racial_spells_test.cpp
git commit -m "feat(races): racialSpells list and racial-spell merge helper"
```

---

### Task 2: Grant racial spells in the realm server

**Files:**
- Modify: `src/realm_server/player.cpp` (creation ~668-702, login backfill ~3542-3564, `BuildDefaultActionButtons` ~3995-4040)

**Interfaces:**
- Consumes: `AppendRacialSpells`, `IsActionBarAbility` (Task 1).

- [ ] **Step 1: Include the helper**

Add `#include "game_server/racial_spells.h"` to the include block of `src/realm_server/player.cpp`, next to the other `game_server/` includes.

- [ ] **Step 2: Character creation**

Replace the class-spell loop's inline action-bar predicate and add the racials after the class-change spell block. The section from `std::map<uint8, ActionButton> actionButtons;` to just before the `DLOG("Creating new character named ...` becomes:

```cpp
		std::map<uint8, ActionButton> actionButtons;

		std::vector<uint32> spellIds;
		spellIds.reserve(classInstance->spells().size() + raceEntry->racialspells().size());
		for (const auto &spell : classInstance->spells())
		{
			const proto::SpellEntry *spellEntry = m_project.spells.getById(spell.spell());
			if (spellEntry && actionButtons.size() < 12 && IsActionBarAbility(*spellEntry))
			{
				actionButtons[actionButtons.size()] = ActionButton{static_cast<uint16>(spell.spell()), action_button_type::Spell};
			}

			if (spell.level() <= level)
			{
				spellIds.push_back(spell.spell());
			}
		}

		// Grant the initial class's class-change spell so the player can switch back to this class
		// after switching away. Additional classes are unlocked in-game by learning their spells.
		if (classInstance->has_class_change_spell() && classInstance->class_change_spell() != 0)
		{
			const uint32 classChangeSpell = classInstance->class_change_spell();
			if (std::find(spellIds.begin(), spellIds.end(), classChangeSpell) == spellIds.end())
			{
				spellIds.push_back(classChangeSpell);
			}
		}

		// Racial spells are known by every character of the race, regardless of class. Racial
		// abilities follow the class abilities on the action bar.
		for (const uint32 racialSpellId : AppendRacialSpells(*raceEntry, m_project.spells, spellIds))
		{
			const proto::SpellEntry *spellEntry = m_project.spells.getById(racialSpellId);
			if (spellEntry && actionButtons.size() < 12 && IsActionBarAbility(*spellEntry))
			{
				actionButtons[actionButtons.size()] = ActionButton{static_cast<uint16>(racialSpellId), action_button_type::Spell};
			}
		}
```

The existing comment line `// Each spell which isn't a passive should (for now) be placed on the action bar` above the `DLOG` stays.

- [ ] **Step 3: Login backfill**

Directly after the class-spell backfill block (after its closing `}` and before `// TODO: Persist added spells back in database`), insert:

```cpp
		// Add potentially missing racial spells, so existing characters learn racials added later
		if (const proto::RaceEntry *raceEntry = m_project.races.getById(m_characterData->raceId))
		{
			for (const uint32 racialSpellId : AppendRacialSpells(*raceEntry, m_project.spells, m_characterData->spellIds))
			{
				spellsAdded.insert(racialSpellId);
				DLOG("Added racial spell " << racialSpellId << " to character because its race grants it");
			}
		}
```

- [ ] **Step 4: Default action bar for a newly activated class**

`BuildDefaultActionButtons` fills a fresh bar when a class is activated. Reuse the predicate and append racial abilities after the class abilities. Replace the attribute block inside the loop:

```cpp
			const proto::SpellEntry* spellEntry = m_project.spells.getById(classSpell.spell());
			if (!spellEntry || !IsActionBarAbility(*spellEntry))
			{
				continue;
			}

			buttons[slot++] = ActionButton{ static_cast<uint16>(classSpell.spell()), action_button_type::Spell };
		}
```

Then, between the end of the `for` loop and `return buttons;`, add:

```cpp
		// Racial abilities follow the class abilities, on every class's bar.
		if (m_characterData)
		{
			if (const proto::RaceEntry* raceEntry = m_project.races.getById(m_characterData->raceId))
			{
				for (const uint32 racialSpellId : raceEntry->racialspells())
				{
					if (slot >= MaxActionButtons)
					{
						break;
					}

					const proto::SpellEntry* spellEntry = m_project.spells.getById(racialSpellId);
					if (spellEntry && IsActionBarAbility(*spellEntry))
					{
						buttons[slot++] = ActionButton{ static_cast<uint16>(racialSpellId), action_button_type::Spell };
					}
				}
			}
		}
```

Read the full function before editing. If its early `return buttons;` for a missing class entry or its loop shape differs from the excerpt above, adapt the insertion point but keep the behaviour: class abilities first, then racial abilities, capped at `MaxActionButtons`.

- [ ] **Step 5: Build realm_server and the tests**

Run: `cmake --build build --config Debug -t realm_server game_server_tests`
Expected: both build with no warnings in the touched code.

- [ ] **Step 6: Commit**

```bash
git add src/realm_server/player.cpp
git commit -m "feat(races): grant racial spells at creation, on login and on fresh class bars"
```

---

### Task 3: Area-aura copies inherit the remaining time

**Files:**
- Modify: `src/shared/game_server/spells/aura_container.h` (free function declaration after the class)
- Modify: `src/shared/game_server/spells/aura_container.cpp:381-391`
- Test: `src/tests/game_server_tests/area_aura_propagation_test.cpp`

**Interfaces:**
- Produces: `std::optional<GameTime> mmo::GetAreaAuraPropagationTime(GameTime duration, GameTime remaining)`. It returns:
  - `std::nullopt`: do not propagate.
  - `0`: a permanent aura.
  - `> 0`: the initial remaining time for the copy.

- [ ] **Step 1: Write the failing test**

Create `src/tests/game_server_tests/area_aura_propagation_test.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/spells/aura_container.h"

#include "catch.hpp"

using namespace mmo;

TEST_CASE("Area aura copies of a timed aura start with the caster's remaining time", "[area_aura]")
{
	// A party member who walks back into range 8 s into a 10 s buff must not get a fresh 10 s.
	const auto time = GetAreaAuraPropagationTime(10000, 2000);
	REQUIRE(time.has_value());
	CHECK(*time == 2000);
}

TEST_CASE("Area aura copies of a permanent aura stay permanent", "[area_aura]")
{
	const auto time = GetAreaAuraPropagationTime(0, 0);
	REQUIRE(time.has_value());
	CHECK(*time == 0);
}

TEST_CASE("An expiring timed area aura is not propagated", "[area_aura]")
{
	// SetInitialRemainingTime(0) falls back to the full duration, so a copy made in the last
	// tick would outlive its source by a whole duration.
	CHECK_FALSE(GetAreaAuraPropagationTime(10000, 0).has_value());
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --config Debug -t game_server_tests`
Expected: compile error, `GetAreaAuraPropagationTime` is not declared.

- [ ] **Step 3: Declare the function**

In `aura_container.h`, add `#include <optional>` to the includes. After the `AuraContainer` class, inside `namespace mmo`, add:

```cpp
	/// Computes how long a party member's copy of an area aura lasts.
	///
	/// Copies must expire together with the caster's aura. Otherwise leaving and re-entering
	/// range would hand out a fresh full duration.
	///
	/// @param duration The aura spell's full duration in milliseconds; 0 means permanent.
	/// @param remaining The caster aura's remaining time in milliseconds.
	/// @return std::nullopt if the aura must not be propagated (a timed aura with no time left),
	///	0 for a permanent copy, otherwise the copy's initial remaining time.
	std::optional<GameTime> GetAreaAuraPropagationTime(GameTime duration, GameTime remaining);
```

- [ ] **Step 4: Implement it and use it in the tick**

In `aura_container.cpp`, inside `namespace mmo` (outside the class methods):

```cpp
	std::optional<GameTime> GetAreaAuraPropagationTime(const GameTime duration, const GameTime remaining)
	{
		if (duration == 0)
		{
			return GameTime{ 0 };
		}

		if (remaining == 0)
		{
			return std::nullopt;
		}

		return remaining;
	}
```

In `HandleAreaAuraTick`, replace the block under `// Aura already active from same caster?`:

```cpp
							// Aura already active from same caster?
							if (!player.HasAuraSpellFromCaster(m_spell.id(), m_casterId))
							{
								// Copies expire together with our aura instead of restarting its duration
								const std::optional<GameTime> propagationTime = GetAreaAuraPropagationTime(m_duration, GetRemainingTime());
								if (!propagationTime)
								{
									return true;
								}

								// Apply the aura
								auto container = std::make_shared<AuraContainer>(player, m_casterId, m_spell, m_duration, m_itemGuid);
								if (*propagationTime > 0)
								{
									container->SetInitialRemainingTime(*propagationTime);
								}

								for (const auto& effect : m_auras)
								{
									container->AddAuraEffect(effect->GetEffect(), effect->GetBasePoints());
								}
								player.ApplyAura(std::move(container));
							}
```

- [ ] **Step 5: Run the tests**

Run:
```bash
cmake --build build --config Debug -t game_server_tests
./bin/Debug/game_server_tests "[area_aura]"
./bin/Debug/game_server_tests
```
Expected: the three new cases pass, and the whole suite stays green.

- [ ] **Step 6: Commit**

```bash
git add src/shared/game_server/spells/aura_container.h src/shared/game_server/spells/aura_container.cpp src/tests/game_server_tests/area_aura_propagation_test.cpp
git commit -m "fix(auras): party copies of an area aura inherit the caster's remaining time"
```

---

### Task 4: Race editor — Racial Spells list

**Files:**
- Modify: `src/mmo_edit/editor_windows/race_editor_window.cpp` (new section before `"Allowed Classes"`, ~line 368)

- [ ] **Step 1: Add the section**

Insert before `if (const auto section = ScopedEditorSection("Allowed Classes", ...`:

```cpp
		if (const auto section = ScopedEditorSection("Racial Spells", ImGuiTreeNodeFlags_DefaultOpen))
		{
			static const char* s_spellNone = "<None>";
			DrawSectionHeader("Racial Abilities");
			ImGui::TextDisabled("Spells every character of this race knows regardless of class. Granted at character creation and backfilled on login. Racial spells should have classmask 0.");
			ImGui::Spacing();

			auto& racialSpells = *currentEntry.mutable_racialspells();
			for (int index = 0; index < racialSpells.size(); ++index)
			{
				ImGui::PushID(index);

				const uint32 spellId = racialSpells[index];
				const auto* spellEntry = m_project.spells.getById(spellId);

				if (ImGui::BeginCombo("##racialSpell", spellEntry != nullptr ? spellEntry->name().c_str() : s_spellNone, ImGuiComboFlags_HeightLargest))
				{
					for (int i = 0; i < m_project.spells.count(); ++i)
					{
						const auto& candidate = m_project.spells.getTemplates().entry(i);
						ImGui::PushID(i);
						const bool selected = candidate.id() == spellId;
						const std::string label = "#" + std::to_string(candidate.id()) + " " + candidate.name();
						if (ImGui::Selectable(label.c_str(), selected))
						{
							racialSpells[index] = candidate.id();
						}
						if (selected)
						{
							ImGui::SetItemDefaultFocus();
						}
						ImGui::PopID();
					}
					ImGui::EndCombo();
				}

				ImGui::SameLine();
				if (DrawDangerButton("Remove"))
				{
					racialSpells.erase(racialSpells.begin() + index);
					ImGui::PopID();
					break;
				}

				ImGui::PopID();
			}

			if (DrawSuccessButton("Add Racial Spell"))
			{
				racialSpells.Add(0);
			}
		}
```

Confirm that `<string>` is available. Other editor windows in this file already build `std::string` labels; if not, add `#include <string>`.

- [ ] **Step 2: Build the editor**

Run: `cmake --build build --config Debug -t mmo_edit`
Expected: builds cleanly.

- [ ] **Step 3: Commit**

```bash
git add src/mmo_edit/editor_windows/race_editor_window.cpp
git commit -m "feat(editor): edit a race's racial spells"
```

---

### Task 5: Author the three spells and the Human racial list

**Files:**
- Create: `tools/racial_abilities/author_human_racials.py`
- Test: `tools/tests/test_human_racial_data.py`
- Data (submodules): `data/editor/data/spells.data`, `data/editor/data/races.data`, `data/client/ClientDB/spells.data`, `data/client/ClientDB/races.data`

**Interfaces:**
- Produces:
  - Spells 248, 249 and 250 in both trees.
  - Race 0 `racialSpells == [248, 249, 250]`.
  - The module constants `SPELL_IDS`, `HUMAN_RACEMASK`, `CALL_OF_THE_WATCH`, `VIS_ID`, `SOUND_ID`, which Tasks 8 and 9 import.
  - The script's `--apply` mode.

ClientDB rule: `MainWindow::ExportToClient()` binary-copies `spells.data` and `races.data`. The
script does the same, writing the editor bytes into ClientDB, so the two trees cannot drift.

- [ ] **Step 1: Write the failing data test**

Create `tools/tests/test_human_racial_data.py`:

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Guards the human racial spell data.

* Racials are Human-only (racemask 0x80000000 = race id 0 under the ``1 << (id - 1)``
  encoding) and class-independent (classmask 0), so class switches keep them.
* Passives are hidden auras; the active is an Ability so new characters get it on the bar.
* Every row carries all four locales, like the rest of the spell data.
* Race 0 lists exactly the three racials, and ClientDB mirrors the editor blobs.

Skips rather than fails when protoc or the data submodules are unavailable.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/racial_abilities"))


class HumanRacialDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            import author_human_racials as ahr
            cls.ahr = ahr
            cls.spells = ahr.load_editor("spells")
            cls.races = ahr.load_editor("races")
        except (FileNotFoundError, OSError, SystemExit) as exc:
            raise unittest.SkipTest(f"data or protoc unavailable: {exc}")

    def spell(self, spell_id):
        for entry in self.spells.entry:
            if entry.id == spell_id:
                return entry
        self.fail(f"spell {spell_id} missing")

    def test_racials_are_human_only_and_class_independent(self):
        for spell_id in self.ahr.SPELL_IDS:
            spell = self.spell(spell_id)
            self.assertEqual(spell.racemask, self.ahr.HUMAN_RACEMASK, spell.name)
            self.assertEqual(spell.classmask, 0, spell.name)

    def test_passives_are_hidden_passive_auras(self):
        for spell_id in (248, 249):
            spell = self.spell(spell_id)
            self.assertTrue(spell.attributes[0] & 0x40, f"{spell.name} must be Passive")
            self.assertFalse(spell.attributes[0] & 0x10, f"{spell.name} must not be an Ability")
            self.assertTrue(spell.attributes[1] & 0x2, f"{spell.name} must be a HiddenAura")

    def test_versatility_raises_all_five_stats_by_two_percent(self):
        effects = self.spell(248).effects
        self.assertEqual(sorted(e.miscvaluea for e in effects), [0, 1, 2, 3, 4])
        for effect in effects:
            self.assertEqual((effect.type, effect.aura, effect.basepoints), (6, 31, 2))

    def test_call_of_the_watch_is_a_timed_party_buff(self):
        spell = self.spell(self.ahr.CALL_OF_THE_WATCH)
        self.assertTrue(spell.attributes[0] & 0x10, "must be an Ability")
        self.assertEqual(spell.duration, 10000)
        self.assertEqual(spell.cooldown, 120000)
        self.assertEqual(spell.rangetype, 5)
        self.assertEqual(sorted((e.type, e.aura, e.basepoints) for e in spell.effects),
                         [(49, 23, 5), (49, 24, -5)])

    def test_every_racial_has_all_four_locales(self):
        for spell_id in self.ahr.SPELL_IDS:
            spell = self.spell(spell_id)
            for field in ("name_loc", "description_loc", "auratext_loc"):
                locales = sorted(entry.locale for entry in getattr(spell, field))
                self.assertEqual(locales, [1, 2, 3, 4], f"{spell.name}.{field}")

    def test_human_race_lists_the_racials(self):
        human = next(race for race in self.races.entry if race.id == 0)
        self.assertEqual(list(human.racialSpells), list(self.ahr.SPELL_IDS))

    def test_client_db_mirrors_the_editor_blobs(self):
        for name in ("spells", "races"):
            editor = (ROOT / f"data/editor/data/{name}.data").read_bytes()
            client = (ROOT / f"data/client/ClientDB/{name}.data").read_bytes()
            self.assertEqual(editor, client, f"ClientDB/{name}.data drifted from the editor")


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test to verify it fails or skips**

Run: `py -m unittest tools/tests/test_human_racial_data.py -v`
Expected: SKIP or ERROR, with `No module named 'author_human_racials'` surfacing as an import failure.

- [ ] **Step 3: Write the authoring script**

Create `tools/racial_abilities/author_human_racials.py`:

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the human racial spells (248-250) and the Human race's racialSpells list.

    py tools/racial_abilities/author_human_racials.py            # validate only
    py tools/racial_abilities/author_human_racials.py --apply    # write editor + ClientDB

Idempotent: re-running replaces the same ids. ClientDB receives a byte copy of the editor
blobs, exactly what mmo_edit's ExportToClient does, so the two trees cannot drift.

Design notes that are easy to undo by accident:

* racemask 0x80000000 is Human (race id 0): masks are ``1 << (id - 1)`` and the data already
  relies on id 0 wrapping to bit 31 (see the Mage classmask).
* classmask stays 0. With no class bit and no Proficiency effect, GamePlayerS keeps the spells
  active across class switches.
* Passives carry HiddenAura: two permanent buffs would only clutter the buff bar.
* Call of the Watch uses ApplyAreaAura. AuraContainer::HandleAreaAuraTick propagates it to
  party members within the *rangetype* radius (5 = 30 yd), not the effect radius.
"""

import argparse
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path

from google.protobuf import descriptor_pb2, descriptor_pool, json_format, message_factory

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-spell-designer/scripts"))
from proto_runtime import find_protoc  # noqa: E402

OUT = ROOT / "generated/racial_abilities"

HUMAN_RACE_ID = 0
HUMAN_RACEMASK = 0x80000000
VERSATILITY, HARD_WORK, CALL_OF_THE_WATCH = 248, 249, 250
SPELL_IDS = (VERSATILITY, HARD_WORK, CALL_OF_THE_WATCH)

# Ids used by the visualization and sound authored in later steps (see author_visuals.py).
VIS_ID = 78
SOUND_ID = 135

# Effect / aura type values (src/shared/game/spell.h, src/shared/game/aura.h).
APPLY_AURA, APPLY_AREA_AURA = 6, 49
MOD_DAMAGE_DONE_PCT, MOD_DAMAGE_TAKEN_PCT, MOD_STAT_PCT, MOD_HEALTH_REGEN_PCT = 23, 24, 31, 38

# Attribute words (spell_attributes / spell_attributes_b).
ONLY_ONE_STACK, ABILITY, PASSIVE = 0x08, 0x10, 0x40
HIDDEN_AURA, IGNORE_LOS = 0x02, 0x80

DE, EN, FR, RU = 1, 2, 3, 4

ICON_DIR = "Interface/Icons/Spells/"


def loc(de, en, fr, ru):
    return [{"locale": DE, "value": de}, {"locale": EN, "value": en},
            {"locale": FR, "value": fr}, {"locale": RU, "value": ru}]


def texts(name, description, auratext):
    """Base strings are English; every field carries all four locales."""
    return {
        "name": name[1], "description": description[1], "auratext": auratext[1],
        "name_loc": loc(*name), "description_loc": loc(*description),
        "auratext_loc": loc(*auratext),
    }


def common(spell_id, icon):
    return {"id": spell_id, "baseid": spell_id, "rank": 1, "baselevel": 1, "spelllevel": 1,
            "racemask": HUMAN_RACEMASK, "classmask": 0, "positive": 1,
            "icon": ICON_DIR + icon}


def spell_definitions():
    versatility = common(VERSATILITY, "T_Icon_Gold_17.htex")
    versatility.update({
        "attributes": [PASSIVE | ONLY_ONE_STACK, IGNORE_LOS | HIDDEN_AURA],
        "effects": [{"index": i, "type": APPLY_AURA, "aura": MOD_STAT_PCT, "basepoints": 2,
                     "miscvaluea": i} for i in range(5)],
    })
    versatility.update(texts(
        ("Vielseitigkeit", "Versatility", "Polyvalence", "Разносторонность"),
        ("Erhöht Stärke, Beweglichkeit, Ausdauer, Intelligenz und Willenskraft um $s0%.",
         "Increases Strength, Agility, Stamina, Intellect and Spirit by $s0%.",
         "Augmente la Force, l'Agilité, l'Endurance, l'Intelligence et l'Esprit de $s0%.",
         "Увеличивает силу, ловкость, выносливость, интеллект и дух на $s0%."),
        ("Alle Werte um $s0% erhöht.", "All stats increased by $s0%.",
         "Toutes les caractéristiques augmentées de $s0%.",
         "Все характеристики увеличены на $s0%.")))

    hard_work = common(HARD_WORK, "T_Icon_Gold_07.htex")
    hard_work.update({
        "attributes": [PASSIVE | ONLY_ONE_STACK, IGNORE_LOS | HIDDEN_AURA],
        "effects": [{"index": 0, "type": APPLY_AURA, "aura": MOD_HEALTH_REGEN_PCT,
                     "basepoints": 10}],
    })
    hard_work.update(texts(
        ("Harte Arbeit gewohnt", "Used to Hard Work", "Habitué au dur labeur",
         "Привычка к тяжёлому труду"),
        ("Erhöht die Lebensregeneration um $s0%.", "Increases health regeneration by $s0%.",
         "Augmente la régénération de la vie de $s0%.",
         "Увеличивает скорость восстановления здоровья на $s0%."),
        ("Lebensregeneration um $s0% erhöht.", "Health regeneration increased by $s0%.",
         "Régénération de la vie augmentée de $s0%.",
         "Восстановление здоровья увеличено на $s0%.")))

    call = common(CALL_OF_THE_WATCH, "T_Icon_Gold_84.htex")
    call.update({
        "attributes": [ABILITY | ONLY_ONE_STACK, IGNORE_LOS],
        "effects": [
            {"index": 0, "type": APPLY_AREA_AURA, "aura": MOD_DAMAGE_DONE_PCT,
             "basepoints": 5, "radius": 30.0},
            {"index": 1, "type": APPLY_AREA_AURA, "aura": MOD_DAMAGE_TAKEN_PCT,
             "basepoints": -5, "radius": 30.0},
        ],
        "duration": 10000,
        "cooldown": 120000,
        "rangetype": 5,
    })
    call.update(texts(
        ("Ruf der Wache", "Call of the Watch", "Appel de la Garde", "Зов Стражи"),
        ("Ruft die Pflicht der Wache wach: Du und Gruppenmitglieder im Umkreis von 30 Metern "
         "verursacht $D lang $s0% mehr Schaden und erleidet 5% weniger Schaden.",
         "Calls upon the duty of the Watch: you and party members within 30 yards deal $s0% "
         "more damage and take 5% less damage for $D.",
         "Fait appel au devoir de la Garde : vous et les membres du groupe à moins de 30 mètres "
         "infligez $s0% de dégâts supplémentaires et subissez 5% de dégâts en moins pendant $D.",
         "Взывает к долгу Стражи: вы и члены группы в радиусе 30 метров наносите на $s0% "
         "больше урона и получаете на 5% меньше урона в течение $D."),
        ("Verursachter Schaden um $s0% erhöht, erlittener Schaden um 5% verringert.",
         "Damage dealt increased by $s0%, damage taken reduced by 5%.",
         "Dégâts infligés augmentés de $s0%, dégâts subis réduits de 5%.",
         "Наносимый урон увеличен на $s0%, получаемый урон уменьшен на 5%.")))

    return [versatility, hard_work, call]


def _require(condition, message):
    """Like assert, but survives ``python -O`` -- these guards gate writing game data."""
    if not condition:
        raise SystemExit(message)


def load_type(schema_dir, protos, message_name):
    with tempfile.TemporaryDirectory(prefix="racials_") as tmp:
        desc = Path(tmp) / "d.pb"
        subprocess.run([str(find_protoc(ROOT)), f"-I{schema_dir}",
                        f"--descriptor_set_out={desc}", "--include_imports", *protos],
                       cwd=schema_dir, check=True)
        file_set = descriptor_pb2.FileDescriptorSet.FromString(desc.read_bytes())
    pool = descriptor_pool.DescriptorPool()
    for entry in file_set.file:
        pool.Add(entry)
    return message_factory.GetMessageClass(pool.FindMessageTypeByName(message_name))


EDITOR_TYPES = {
    "spells": ("spells.proto", "mmo.proto.Spells"),
    "races": ("races.proto", "mmo.proto.Races"),
}


def editor_path(name):
    return ROOT / f"data/editor/data/{name}.data"


def client_path(name):
    return ROOT / f"data/client/ClientDB/{name}.data"


def load_editor(name):
    proto, message = EDITOR_TYPES[name]
    message_type = load_type(ROOT / "src/shared/proto_data", [proto], message)
    return message_type.FromString(editor_path(name).read_bytes())


def upsert_spells(dataset):
    by_id = {spell.id: spell for spell in dataset.entry}
    for draft in spell_definitions():
        visualization_id = 0
        target = by_id.get(draft["id"])
        if target is None:
            target = dataset.entry.add()
        else:
            _require(target.name == draft["name"],
                     f"spell {draft['id']} is {target.name!r}, refusing to overwrite it "
                     f"with {draft['name']!r}")
            visualization_id = target.visualization_id
        target.Clear()
        json_format.ParseDict(draft, target)
        # The spell -> visualization link is owned by author_visuals.py; keep it on re-runs.
        if visualization_id:
            target.visualization_id = visualization_id


def set_racial_spells(races):
    human = next((race for race in races.entry if race.id == HUMAN_RACE_ID), None)
    _require(human is not None, "race 0 (Human) is missing")
    _require(human.name == "Human", f"race 0 is {human.name!r}, expected 'Human'")
    del human.racialSpells[:]
    human.racialSpells.extend(SPELL_IDS)


def validate(spells, races):
    ids = [spell.id for spell in spells.entry]
    _require(len(ids) == len(set(ids)), "duplicate spell ids")
    for spell_id in SPELL_IDS:
        _require(spell_id in ids, f"spell {spell_id} missing after upsert")
    for draft in spell_definitions():
        _require((ROOT / "data/client" / draft["icon"]).is_file(),
                 f"missing icon {draft['icon']}")
    _require(spells.IsInitialized(), "spells dataset is missing required fields")
    _require(races.IsInitialized(), "races dataset is missing required fields")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    spells = load_editor("spells")
    races = load_editor("races")
    upsert_spells(spells)
    set_racial_spells(races)
    validate(spells, races)

    if not args.apply:
        print(f"validated racial spells {SPELL_IDS} and Human racialSpells")
        return

    OUT.mkdir(parents=True, exist_ok=True)
    backup = OUT / ("backup_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    backup.mkdir()
    for name in EDITOR_TYPES:
        shutil.copy2(editor_path(name), backup / f"editor_{name}.data")
        shutil.copy2(client_path(name), backup / f"client_{name}.data")

    for name, dataset in (("spells", spells), ("races", races)):
        blob = dataset.SerializeToString()
        editor_path(name).write_bytes(blob)
        client_path(name).write_bytes(blob)

    print(f"wrote racial spells {SPELL_IDS} and Human racialSpells to editor + ClientDB. "
          f"Backup: {backup}")


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Validate, then apply**

Run:
```bash
py tools/racial_abilities/author_human_racials.py
py tools/racial_abilities/author_human_racials.py --apply
```
Expected:
- Validation prints `validated racial spells (248, 249, 250) and Human racialSpells`.
- Apply prints `wrote … Backup: …`.

- [ ] **Step 5: Run the data tests**

Run: `py -m unittest tools/tests/test_human_racial_data.py -v`
Expected: 7 tests OK.

Also: `py .claude/skills/mmo-spell-designer/scripts/inspect_spell_catalog.py --section spell --spell-id 250 --pretty`. The output shows `racemask: 2147483648` and the four `name_loc` entries.

- [ ] **Step 6: Commit data on the submodules' master, then the tooling in the parent**

```bash
git -C data/editor add data/spells.data data/races.data
git -C data/editor commit -m "Human racial spells 248-250 and racialSpells list"
git -C data/client add ClientDB/spells.data ClientDB/races.data
git -C data/client commit -m "Human racial spells 248-250 (ClientDB)"
git add tools/racial_abilities/author_human_racials.py tools/tests/test_human_racial_data.py
git commit -m "feat(races): author the human racial spells"
```

`generated/` is a scratch folder. Check `git status` shows nothing from it. If it does, it is
ignored in the other authoring rounds; mirror their `.gitignore` handling rather than
committing backups.

---

### Task 6: Particles for *Call of the Watch*

**Files:**
- Create: `tools/particle_gen/recipes/human_racials.py`
- Create (data/client): `Particles/Human/CallOfTheWatchActivate.hpar`, `Particles/Human/CallOfTheWatchApply.hpar`

**Interfaces:**
- Produces:
  - Particle paths `Particles/Human/CallOfTheWatchActivate.hpar` (caster root, one-shot) and `Particles/Human/CallOfTheWatchApply.hpar` (recipient root, one-shot).
  - `human_racials.LOOPING_EFFECTS = set()`.

Use the `particle-author` skill for this task: invoke it before writing the recipe.

- [ ] **Step 1: Write the recipe**

Create `tools/particle_gen/recipes/human_racials.py`:

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Human racial effects. Run from the repo root::

    py -3 tools/particle_gen/recipes/human_racials.py

Writes into ``data/client/Particles/Human/``.

Art direction
-------------
*Call of the Watch* is a call to duty, not a miracle. Warm gold with a steel-blue accent
(the Watch and the Crown); no sigils or holy rays, which belong to the cleric. The activation
is a horn-blast shockwave: a bright onset flash at chest height, a gold ring and a steel
ring racing outward over the 30 yd party radius, and banner-like gold sparks rising around
the caster. The apply effect plays on every party member in range: a small gold ring at the
feet and a few rising motes, low alpha.

Both effects are one-shot; nothing here loops (``LOOPING_EFFECTS`` is empty).
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import cleric_common as cc  # noqa: E402
from cleric_auras import column_flash, fast_ring, ground_motes  # noqa: E402
from cleric_common import ParticleSystem, hpar  # noqa: E402

WATCH_GOLD = (1.00, 0.78, 0.30)
BANNER_GOLD = (1.00, 0.88, 0.52)
STEEL_BLUE = (0.55, 0.70, 0.95)
HOT = (1.00, 0.96, 0.82)

OUT_DIR = os.path.join(cc.ROOT, "data", "client", "Particles", "Human")
LOOPING_EFFECTS = set()


def write(system, filename):
    path = os.path.join(OUT_DIR, filename)
    os.makedirs(OUT_DIR, exist_ok=True)
    hpar.save(system, path)
    print("wrote %s (%d emitters, %d bytes)"
          % (path, len(system.emitters), os.path.getsize(path)))


def call_of_the_watch_activate():
    """Caster ROOT. Horn-blast shockwave plus rising banner sparks."""
    return ParticleSystem(emitters=[
        cc.soft_flash("Onset Flash", size=1.6, colour=HOT, alpha=0.85, lifetime=0.22,
                      height=1.3),
        fast_ring("Gold Shockwave", start_size=0.8, end_size=12.0, colour=WATCH_GOLD,
                  alpha=0.6, lifetime=0.9),
        fast_ring("Steel Shockwave", start_size=0.6, end_size=8.0, colour=STEEL_BLUE,
                  alpha=0.45, lifetime=0.8, delay=0.12),
        column_flash("Rally Column", colour=BANNER_GOLD, hot=HOT, count=22, alpha=0.30),
        ground_motes("Banner Sparks", colour=WATCH_GOLD, hot=HOT, count=40, radius=2.2,
                     rise=2.6, alpha=0.75),
        cc.spark_burst("Steel Sparks", count=18, speed=3.5, colour=STEEL_BLUE, size=0.08,
                       lifetime=0.5, gravity=-3.0, drag=2.0),
    ])


def call_of_the_watch_apply():
    """Recipient ROOT, every party member in range. Small, low alpha."""
    return ParticleSystem(emitters=[
        fast_ring("Apply Ring", start_size=0.4, end_size=1.9, colour=WATCH_GOLD,
                  alpha=0.5, lifetime=0.5),
        cc.rising_motes("Apply Motes", colour=BANNER_GOLD, hot=HOT, count=12, radius=0.4,
                        rise=1.7, size=0.20, lifetime=0.7, alpha=0.65, orbital=1.0,
                        duration=0.15),
    ])


if __name__ == "__main__":
    write(call_of_the_watch_activate(), "CallOfTheWatchActivate.hpar")
    write(call_of_the_watch_apply(), "CallOfTheWatchApply.hpar")
```

Check the helper signatures before running. `cc.soft_flash` and `cc.spark_burst` are
re-exported from `warrior_common`, and `cc.rising_motes` lives in `cleric_common`. Open those
definitions and match the keyword names exactly. If `soft_flash` has no `height` parameter,
drop it and attach the flash through the kit's `attach_bone` in Task 8 instead.

- [ ] **Step 2: Build and check the effects**

Run:
```bash
py -3 tools/particle_gen/recipes/human_racials.py
py tools/particle_gen/inspect_hpar.py data/client/Particles/Human/CallOfTheWatchActivate.hpar --check --one-shot
py tools/particle_gen/inspect_hpar.py data/client/Particles/Human/CallOfTheWatchApply.hpar --check --one-shot
```
Expected:
- Both files are written.
- `--check` reports no errors: translucent materials, one-shot, particle budget OK.

- [ ] **Step 3: Render previews and look at them**

Run:
```bash
py tools/particle_gen/preview.py data/client/Particles/Human/CallOfTheWatchActivate.hpar --figure --exposure 1.6
py tools/particle_gen/preview.py data/client/Particles/Human/CallOfTheWatchApply.hpar --figure --exposure 1.6
```
Open both PNGs with the Read tool and judge:
1. Does the gold ring read clearly as wide and bright *while* expanding?
2. Is the steel accent visible but secondary?
3. Do the sparks rise around the caster rather than clumping at the centre?
4. Is the apply effect small and low-key?

Tune numbers in the recipe (alpha, counts, sizes), rebuild, and re-render until all four hold.
Err brighter: preview sheets are on black, the game scene is lit.

- [ ] **Step 4: Send the final contact sheets to the user** with SendUserFile (`status: proactive`).

- [ ] **Step 5: Commit**

```bash
git -C data/client add Particles/Human
git -C data/client commit -m "Call of the Watch particle effects"
git add tools/particle_gen/recipes/human_racials.py
git commit -m "feat(vfx): Call of the Watch particle recipe"
```

---

### Task 7: Sound for *Call of the Watch* (ElevenLabs)

**Files:**
- Create: `tools/sfx_gen/recipes/human.py`
- Create (data/client): `Sound/Spells/Human/CallOfTheWatch.wav`

**Interfaces:**
- Produces: the sound file `Sound/Spells/Human/CallOfTheWatch.wav` (one-shot, mono 44.1 kHz).

- [ ] **Step 1: Write the prompt table**

Create `tools/sfx_gen/recipes/human.py`:

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Prompt table for the human racial sound effects.

Generated through the ElevenLabs MCP connector (``eleven_text_to_sound_v2``); this file is
the checked-in record of what was asked for. Every prompt names its layers and asks for a
tail -- "dry, isolated, no reverb" prompting produces thin foley (see warrior.py).

Call of the Watch is a rallying horn call: martial and grounded, no voice (a baked-in shout
would clash with every race and gender), no choir (that is the cleric's register).
"""

from dataclasses import dataclass

PROMPT_INFLUENCE = 0.75

# eleven_text_to_sound_v2 silently fails anything longer than this; see warrior.py.
MAX_PROMPT_CHARS = 450


@dataclass(frozen=True)
class SoundSpec:
    prompt: str
    duration: float
    target_dbfs: float
    loop: bool = False


_STYLE = ("Epic AAA fantasy MMO ability sound effect, richly layered and produced, "
          "with a designed tail.")

SOUNDS = {
    "CallOfTheWatch": SoundSpec(
        f"A rallying call to arms for a town watch. Layers: a short bold brass war horn "
        f"blast, a bright ringing strike of steel on a shield, a deep war drum hit for "
        f"weight, a low sub-bass swell, then a short airy tail. Martial and heroic, no voice, "
        f"no choir, no music. {_STYLE}",
        duration=3.0, target_dbfs=-3.0),
}
```

- [ ] **Step 2: Guard the recipe with the existing sfx recipe test**

Open `tools/tests/test_sfx_recipes.py` and check how it discovers recipe modules. If it lists
modules explicitly, add `human` to that list. Then run:

Run: `py -m unittest tools/tests/test_sfx_recipes.py -v`
Expected: OK. The cap and banned-phrase checks pass for `human.CallOfTheWatch`.

- [ ] **Step 3: Generate four takes**

Load the connector tools with ToolSearch (`creative_generate_in_flow`, `creative_get_flow_run_status`, `creative_create_flow` and related). Use the same node type the earlier rounds used: the `sfx` node, model `eleven_text_to_sound_v2`, `prompt_influence` 0.75, duration 3.0 s, 4 variations, prompt verbatim from `human.SOUNDS["CallOfTheWatch"]`.

Hand the poll-and-download loop to a subagent if it involves many signed URLs, so they stay
out of the controller's context.

- [ ] **Step 4: Fetch and postprocess each take**

For each take `n` in 1..4:
```bash
py tools/sfx_gen/fetch.py generated/racial_abilities/sfx/CallOfTheWatch_take<n>.wav -3.0 "<signed-url>"
```
Expected: each WAV peaks near 0.708 (-3 dBFS). Reject any take whose peak sits past ~60% of
the clip.

- [ ] **Step 5: Audition (user checkpoint)**

```bash
py tools/sfx_gen/fetch.py --audition generated/racial_abilities/sfx/audition_call_of_the_watch.wav generated/racial_abilities/sfx/CallOfTheWatch_take1.wav generated/racial_abilities/sfx/CallOfTheWatch_take2.wav generated/racial_abilities/sfx/CallOfTheWatch_take3.wav generated/racial_abilities/sfx/CallOfTheWatch_take4.wav
```
Send the reel with SendUserFile and state your measurement-based pre-pick. **Stop and wait for
the user's choice.** If no take is acceptable, revise the prompt with the user's feedback and
go back to Step 3.

- [ ] **Step 6: Install the chosen take and commit**

```bash
mkdir -p data/client/Sound/Spells/Human
cp generated/racial_abilities/sfx/CallOfTheWatch_take<chosen>.wav data/client/Sound/Spells/Human/CallOfTheWatch.wav
git -C data/client add Sound/Spells/Human/CallOfTheWatch.wav
git -C data/client commit -m "Call of the Watch sound"
git add tools/sfx_gen/recipes/human.py tools/tests/test_sfx_recipes.py
git commit -m "feat(sfx): Call of the Watch prompt table"
```
(Leave `test_sfx_recipes.py` out of the `git add` if Step 2 did not change it.)

---

### Task 8: Sound entry, visualization and spell link

**Files:**
- Create: `tools/racial_abilities/author_visuals.py`
- Modify: `tools/tests/test_human_racial_data.py` (visualization tests)
- Data:
  - `data/editor/data/{sounds,spell_visualizations,spells}.data`
  - `data/client/ClientDB/{sounds,spell_visualizations,spells}.data`

**Interfaces:**
- Consumes: `author_human_racials` (`CALL_OF_THE_WATCH`, `VIS_ID`, `SOUND_ID`, `load_type`, `editor_path`, `client_path`); particle paths (Task 6); WAV (Task 7).
- Produces: SoundEntry 135, visualization 78, and spell 250 with `visualization_id = 78`.

Event ids (proto `SpellVisualEvent`): 3 = CAST_SUCCEEDED, 5 = AURA_APPLIED.

*Call of the Watch* is instant, so only CAST_SUCCEEDED fires on the caster (see the Open-spell
notes). `EmoteRoar` exists on both human rigs; `EmoteShout` is male-only.

- [ ] **Step 1: Add the failing visualization tests**

Append to `HumanRacialDataTest` in `tools/tests/test_human_racial_data.py`:

```python
    def test_call_of_the_watch_points_at_its_visualization(self):
        self.assertEqual(self.spell(self.ahr.CALL_OF_THE_WATCH).visualization_id,
                         self.ahr.VIS_ID)

    def test_visualization_follows_the_kit_rules(self):
        import author_visuals as av
        vis = next(v for v in av.load_editor_visuals().entry if v.id == self.ahr.VIS_ID)
        self.assertEqual(vis.name, "Human - Call of the Watch")
        for event, kit_list in vis.kits_by_event.items():
            for kit in kit_list.kits:
                self.assertFalse(kit.HasField("duration_ms"), "duration_ms time-warps the clip")
                self.assertFalse(kit.sounds, "kits use sound_ids, never sounds")
                self.assertFalse(kit.loop, "nothing in this visualization loops")
                if event == 5:
                    self.assertFalse(kit.sound_ids, "aura events replay on view-in: no sound")
                for path in kit.particles:
                    self.assertTrue((ROOT / "data/client" / path).is_file(), path)
        cast_sounds = [sid for kit in vis.kits_by_event[3].kits for sid in kit.sound_ids]
        self.assertEqual(cast_sounds, [self.ahr.SOUND_ID])

    def test_sound_entry_exists_and_mirrors(self):
        import author_visuals as av
        sounds = av.load_editor_sounds()
        entry = next(e for e in sounds.entry if e.id == self.ahr.SOUND_ID)
        self.assertEqual(list(entry.files), ["Sound/Spells/Human/CallOfTheWatch.wav"])
        for name in ("sounds", "spell_visualizations", "spells"):
            editor = (ROOT / f"data/editor/data/{name}.data").read_bytes()
            client = (ROOT / f"data/client/ClientDB/{name}.data").read_bytes()
            self.assertEqual(editor, client, f"ClientDB/{name}.data drifted")
```

Run: `py -m unittest tools/tests/test_human_racial_data.py -v`
Expected: the three new tests ERROR or FAIL, because `author_visuals` is missing.

- [ ] **Step 2: Write the authoring script**

Create `tools/racial_abilities/author_visuals.py`:

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the Call of the Watch sound entry (135) and visualization (78), and link spell 250.

    py tools/racial_abilities/author_visuals.py            # validate only
    py tools/racial_abilities/author_visuals.py --apply    # write editor + ClientDB

ClientDB receives byte copies of the editor blobs (what mmo_edit's ExportToClient does).

Design notes that are easy to undo by accident:

* The spell is instant: only CAST_SUCCEEDED (3) fires on the caster, so it carries the
  animation, the activation effect, the light and the sound.
* AURA_APPLIED (5) plays on every buffed party member and replays whenever a unit with the
  aura comes into view -- so it carries particles only, never a sound.
* EmoteRoar exists on both human rigs; no kit sets duration_ms (it time-warps the clip).
"""

import argparse
import shutil
import sys
from datetime import datetime
from pathlib import Path

from google.protobuf import json_format

sys.path.insert(0, str(Path(__file__).resolve().parent))
import author_human_racials as ahr  # noqa: E402

ROOT = ahr.ROOT
OUT = ahr.OUT
CAST_SUCCEEDED, AURA_APPLIED = "3", "5"

SOUND_FILE = "Sound/Spells/Human/CallOfTheWatch.wav"
ACTIVATE = "Particles/Human/CallOfTheWatchActivate.hpar"
APPLY = "Particles/Human/CallOfTheWatchApply.hpar"
WATCH_GOLD = (1.00, 0.78, 0.30)

VIS_NAME = "Human - Call of the Watch"
SOUND_NAME = "Human - Call of the Watch"


def light(colour, intensity, rng, fade_in, fade_out):
    return {"r": colour[0], "g": colour[1], "b": colour[2], "intensity": intensity,
            "range": rng, "fade_in_time": fade_in, "fade_out_time": fade_out}


def visualization():
    return {
        "id": ahr.VIS_ID,
        "name": VIS_NAME,
        "kits_by_event": {
            CAST_SUCCEEDED: {"kits": [
                {"scope": "CASTER", "loop": False, "animation_name": "EmoteRoar"},
                {"scope": "CASTER", "loop": False, "particles": [ACTIVATE],
                 "sound_ids": [ahr.SOUND_ID]},
                {"scope": "CASTER", "loop": False, "attach_bone": "spine_03",
                 "light": light(WATCH_GOLD, 2.6, 9.0, 0.05, 1.0),
                 "tint": {"r": WATCH_GOLD[0], "g": WATCH_GOLD[1], "b": WATCH_GOLD[2],
                          "a": 0.45, "duration_ms": 700}},
            ]},
            AURA_APPLIED: {"kits": [
                {"scope": "TARGET", "loop": False, "particles": [APPLY]},
            ]},
        },
    }


def populate_sound(entry):
    entry.Clear()
    entry.id = ahr.SOUND_ID
    entry.name = SOUND_NAME
    entry.files.append(SOUND_FILE)
    entry.category = 0          # SOUND_EFFECTS
    entry.is_3d = True
    entry.looped = False
    entry.stream = False
    entry.volume = 0.95
    entry.pitch_min = 0.98
    entry.pitch_max = 1.02
    # A rallying call is heard further than a single strike.
    entry.min_distance = 8.0
    entry.max_distance = 40.0


def load_editor_sounds():
    message = ahr.load_type(ROOT / "src/shared/proto_data", ["sounds.proto"], "mmo.proto.Sounds")
    return message.FromString(ahr.editor_path("sounds").read_bytes())


def load_editor_visuals():
    message = ahr.load_type(ROOT / "src/shared/proto_data", ["spell_visualizations.proto"],
                            "mmo.proto.SpellVisualizations")
    return message.FromString(ahr.editor_path("spell_visualizations").read_bytes())


def upsert_sound(sounds):
    by_id = {e.id: e for e in sounds.entry}
    target = by_id.get(ahr.SOUND_ID)
    if target is not None:
        ahr._require(target.name == SOUND_NAME,
                     f"sound id {ahr.SOUND_ID} is already taken by {target.name!r}")
    else:
        target = sounds.entry.add()
    populate_sound(target)


def upsert_visual(visuals):
    by_id = {v.id: v for v in visuals.entry}
    target = by_id.get(ahr.VIS_ID)
    if target is not None:
        ahr._require(target.name == VIS_NAME,
                     f"visualization {ahr.VIS_ID} is already taken by {target.name!r}")
    else:
        target = visuals.entry.add()
    target.Clear()
    json_format.ParseDict(visualization(), target)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    for path in (SOUND_FILE, ACTIVATE, APPLY):
        ahr._require((ROOT / "data/client" / path).is_file(), f"missing {path}")

    sounds = load_editor_sounds()
    visuals = load_editor_visuals()
    spells = ahr.load_editor("spells")

    upsert_sound(sounds)
    upsert_visual(visuals)
    spell = next((s for s in spells.entry if s.id == ahr.CALL_OF_THE_WATCH), None)
    ahr._require(spell is not None, "spell 250 missing -- run author_human_racials.py first")
    spell.visualization_id = ahr.VIS_ID

    for name, dataset in (("sounds", sounds), ("spell_visualizations", visuals),
                          ("spells", spells)):
        ids = [e.id for e in dataset.entry]
        ahr._require(len(ids) == len(set(ids)), f"duplicate ids in {name}")
        ahr._require(dataset.IsInitialized(), f"{name} dataset is missing required fields")

    if not args.apply:
        print(f"validated sound {ahr.SOUND_ID}, visualization {ahr.VIS_ID}, spell link")
        return

    OUT.mkdir(parents=True, exist_ok=True)
    backup = OUT / ("backup_vis_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    backup.mkdir()
    datasets = (("sounds", sounds), ("spell_visualizations", visuals), ("spells", spells))
    for name, _ in datasets:
        shutil.copy2(ahr.editor_path(name), backup / f"editor_{name}.data")
        shutil.copy2(ahr.client_path(name), backup / f"client_{name}.data")
    for name, dataset in datasets:
        blob = dataset.SerializeToString()
        ahr.editor_path(name).write_bytes(blob)
        ahr.client_path(name).write_bytes(blob)

    print(f"wrote sound {ahr.SOUND_ID}, visualization {ahr.VIS_ID} and the spell 250 link "
          f"to editor + ClientDB. Backup: {backup}")


if __name__ == "__main__":
    main()
```

Before running, check the field names against `src/shared/proto_data/spell_visualizations.proto`:
- `scope`, `animation_name`, `particles`, `sound_ids`, `attach_bone`
- `light` and its sub-fields
- `tint.duration_ms`
- the `kits_by_event` map key type

Also check the `SoundEntry` fields in `sounds.proto`. The cleric scripts use exactly these
names, so they should match. If `ParseDict` rejects a name, fix it to the proto's name.

- [ ] **Step 3: Validate, apply, test**

Run:
```bash
py tools/racial_abilities/author_visuals.py
py tools/racial_abilities/author_visuals.py --apply
py -m unittest tools/tests/test_human_racial_data.py -v
```
Expected: 10 tests OK.

- [ ] **Step 4: Commit**

```bash
git -C data/editor add data/sounds.data data/spell_visualizations.data data/spells.data
git -C data/editor commit -m "Call of the Watch sound entry, visualization and spell link"
git -C data/client add ClientDB/sounds.data ClientDB/spell_visualizations.data ClientDB/spells.data
git -C data/client commit -m "Call of the Watch sound entry, visualization and spell link (ClientDB)"
git add tools/racial_abilities/author_visuals.py tools/tests/test_human_racial_data.py
git commit -m "feat(vfx): Call of the Watch visualization and sound entry"
```

---

### Task 9: E2E scenario

**Files:**
- Create: `e2e/scenarios/human_racials.lua`

**Interfaces:**
- Consumes: spells 248-250 granted at creation (Task 2 + Task 5); class-change spell 230 (Mage); Frost Armor 6 (Mage level-1 class spell, used as the "now a mage" signal).

- [ ] **Step 1: Write the scenario**

Create `e2e/scenarios/human_racials.lua`:

```lua
-- e2e-class: 1
-- Human racial abilities: a fresh human character knows its three racials, Call of the
-- Watch applies its buff and starts its cooldown, and the racials survive a class switch.
--
-- Uses: Versatility (248), Used to Hard Work (249), Call of the Watch (250),
-- the Mage class-change spell (230) and Frost Armor (6, a Mage level-1 class spell).
-- The passives carry HiddenAura, so their auras are never sent to the client; only the
-- spells themselves are observable here.

local VERSATILITY = 248
local HARD_WORK = 249
local CALL_OF_THE_WATCH = 250
local BECOME_MAGE = 230
local FROST_ARMOR = 6

local me = Me()

for _, spellId in ipairs({ VERSATILITY, HARD_WORK, CALL_OF_THE_WATCH }) do
	Assert(WaitUntil(function() return HasSpell(spellId) end, 10000, "racial " .. spellId .. " known"),
		"a fresh human character should know racial spell " .. spellId)
end

Assert(CastSpell(CALL_OF_THE_WATCH), "Call of the Watch request should be accepted")
Assert(WaitUntil(function() return LastCastResult() == "ok" end, 10000, "cast succeeds"),
	"Call of the Watch should succeed, got: " .. LastCastResult())
Assert(WaitUntil(function() return HasAura(me, CALL_OF_THE_WATCH) end, 10000, "buff applied"),
	"Call of the Watch should buff the caster")
Assert(IsSpellOnCooldown(CALL_OF_THE_WATCH), "Call of the Watch should be on cooldown")

-- Switch class: racials have classmask 0 and must stay known and castable.
GM.LearnSpell(BECOME_MAGE)
Assert(WaitUntil(function() return HasSpell(BECOME_MAGE) end, 10000, "class change learned"),
	"the Mage class-change spell should be learned")
Assert(CastSpell(BECOME_MAGE), "class change request should be accepted")
Assert(WaitUntil(function() return HasSpell(FROST_ARMOR) end, 15000, "now a mage"),
	"switching to Mage should grant Frost Armor")

for _, spellId in ipairs({ VERSATILITY, HARD_WORK, CALL_OF_THE_WATCH }) do
	Assert(HasSpell(spellId), "racial " .. spellId .. " should survive the class switch")
end

GM.ResetCooldowns()
Assert(CastSpell(CALL_OF_THE_WATCH), "Call of the Watch should still be castable as a mage")
Assert(WaitUntil(function() return LastCastResult() == "ok" end, 10000, "cast as mage succeeds"),
	"Call of the Watch as a mage should succeed, got: " .. LastCastResult())

Log("racials known, Call of the Watch buffs and survives a class switch")
```

Things to check while writing it:
- **`GM.ResetCooldowns()` with a target.** If a target is selected, it resets the target. The
  scenario never targets anything, so it resets the GM.
- **The class-change spell.** If 230 has a cast time or needs its class unlocked first, adapt
  by reading `e2e/scenarios/gm_progression.lua` or the class-change spell data
  (`inspect_spell_catalog.py --section spell --spell-id 230`).
- **Switching back.** Does a class switch persist into later scenarios? Each scenario gets a
  fresh character, so no restore is needed.

- [ ] **Step 2: Build and run the scenario**

Run:
```powershell
cmake --build build -t e2e_client login_server realm_server world_server --config Debug
powershell -File tools/e2e/e2e_run.ps1 -Scenario human_racials
```
Expected: exit 0. On failure, read `e2e/runtime/logs/human_racials.jsonl` and `human_racials.out.log`.

- [ ] **Step 3: Commit**

```bash
git add e2e/scenarios/human_racials.lua
git commit -m "test(e2e): human racials known, Call of the Watch buffs, survive class switch"
```

---

### Task 10: Land the data, run the gate, take in-game screenshots

**Files:**
- Modify: parent gitlinks `data/client`, `data/editor`
- Modify: `C:\Users\RobinKlimonow\.claude\projects\H--mmo\memory\` (new memory + index line)

- [ ] **Step 1: Check the submodules**

Run:
```bash
git -C data/client status --short
git -C data/editor status --short
git -C data/client branch --show-current
git -C data/editor branch --show-current
```
Expected: both are clean and on `master`.

- [ ] **Step 2: Bump the gitlinks in the parent**

```bash
git add data/client data/editor
git commit -m "Updated data ref: human racial abilities"
```

- [ ] **Step 3: Run the gate**

Invoke the `/gate` skill. It runs a Debug build, unit tests (`ctest`), tool tests (`python -m unittest discover -s tools/tests`) and E2E, then reviews the branch diff.

Expected: green. If red, fix forward on this branch and re-run.

- [ ] **Step 4: Look at it in game**

Follow the client-visual-verification memory: auto-login, cast *Call of the Watch* (action
bar), and take a screenshot mid-effect. Check the bar layout, the spellbook General tab (both
passives with icon and description) and the tooltip texts in German. Send the screenshots to
the user.

- [ ] **Step 5: Save a memory**

Write `human-racial-abilities.md` (type project):
- the `racialSpells` mechanism and where it is honoured;
- ids 248-250 / vis 78 / sound 135;
- the area-aura remaining-time fix;
- the balance framework pointer to the spec.

Add one index line to `MEMORY.md`.

- [ ] **Step 6: Report**

Summarize to the user:
- what shipped, with the commit list;
- the gate result;
- the screenshots.

Do not push and do not `/ship` unless the user asks.
