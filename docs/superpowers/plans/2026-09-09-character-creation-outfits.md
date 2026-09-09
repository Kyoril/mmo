# Character Creation Outfits Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Dress the character creation preview in a class-appropriate, race/gender-aware cosmetic outfit with a combat-ready stance, and give the player a checkbox to strip it off again.

**Architecture:** A new `CharacterOutfit` message on `ClassEntry` carries item *display* ids (client-owned data, unlike item ids which the world server owns). The editor's `ExportToClient` already copies `classes.data` verbatim, so mirroring the field numbers into the client's subset schema is the whole transport story. The client's `CharCreateInfo` applies those displays to its `ModelFrame` entity with the same `ApplyItemDisplay` helper the character *select* screen already uses.

**Tech Stack:** C++17, protobuf 2 syntax, Dear ImGui (editor), luabind (client scripting), the project's XML/Lua frame UI, Catch2 + CTest, Python 3 with `protobuf` for the one-off data authoring script.

## Global Constraints

- Braces: Allman style — every `{` and `}` on its own line. Braces always, even for single-line bodies.
- Indentation: **tabs**, in C++, `.proto`, XML and Lua alike.
- Member variables `m_camelCase`; methods `PascalCase`; locals and anonymous-namespace free functions `camelCase`; files `snake_case`.
- Every new source file starts with `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` and headers use `#pragma once` plus Doxygen comments on public members.
- Root namespace is `mmo`. Client data types live in `mmo::proto_client`, written as nested `namespace mmo { namespace proto_client { ... } }` to match the surrounding files.
- No exceptions (`SIMPLE_NO_EXCEPTIONS`). Use `ASSERT` / `VERIFY` / `DLOG` / `WLOG` / `ELOG` from `base/macros.h`.
- `src/shared/proto_data/classes.proto` and `src/shared/client_data/classes.proto` share field numbers. Field 27 is `classlevels`; the new field is **28** in both.
- Nothing in this plan goes on the wire. Do **not** bump `mmo::auth::ProtocolVersion` or `mmo::game::ProtocolVersion` and do not run `tools/protocol_version_check.py --update`.
- UI strings must not be hardcoded. Add the key to `Localization.txt` in **all four** locales: `Locale_enUS`, `Locale_deDE`, `Locale_frFR`, `Locale_ruRU`.
- `data/client` and `data/editor` are git submodules. Changes there are committed inside the submodule first, then the new submodule SHA is committed in the superproject.
- Never push to origin.
- The build directory is `build/`; binaries land in `bin/Debug/` on Windows.

**Race ids:** Human = 0, Orc = 1, Undead Human = 2. **Class ids:** Mage = 0, Warrior = 1, Cleric = 2, Acolyte = 3, Scout = 4. **Genders:** male = 0, female = 1. Both id spaces are 0-based, which is why `-1` is the "any" sentinel.

**One-time setup** (run once before Task 1, from the worktree root):

```bash
git submodule update --init data/client data/editor
```

```bash
cmake -S . -B build -DMMO_BUILD_CLIENT=ON -DMMO_BUILD_EDITOR=ON -DMMO_BUILD_TOOLS=ON -DMMO_WITH_DEV_COMMANDS=ON
```

---

## File Structure

**Created:**
- `src/shared/client_data/character_outfit.h` — declaration of `SelectCharacterOutfit`.
- `src/shared/client_data/character_outfit.cpp` — the race/gender matching rule. Pure protobuf, no rendering.
- `src/tests/client_data_tests/CMakeLists.txt` — one `mmo_add_test` line.
- `src/tests/client_data_tests/test_character_outfit.cpp` — Catch2 tests for the matching rule.
- `tools/apply_character_outfits.py` — one-off, re-runnable script that writes the authored outfits into both `classes.data` files.

**Modified:**
- `src/shared/proto_data/classes.proto` — `CharacterOutfit` message + `outfits = 28` on `ClassEntry`.
- `src/shared/client_data/classes.proto` — the same, in `mmo.proto_client`.
- `src/tests/CMakeLists.txt` — register the new suite.
- `src/mmo_client/char_creation/char_create_info.h` / `.cpp` — outfit application, ready animation, visibility toggle, Lua bindings.
- `src/mmo_edit/editor_windows/class_editor_window.cpp` — "Character Creation Outfits" authoring section.
- `data/client/Interface/GlueUI/CharCreate.xml` / `CharCreate.lua` — the show/hide checkbox.
- `data/client/Locales/*/Localization.txt` — the `SHOW_OUTFIT` key.
- `data/editor/data/classes.data`, `data/client/ClientDB/classes.data` — the authored outfit content.

---

### Task 1: Outfit schema and selection rule

The data shape plus the pure function that picks an outfit, with tests. Nothing renders yet.

**Files:**
- Modify: `src/shared/proto_data/classes.proto`
- Modify: `src/shared/client_data/classes.proto`
- Create: `src/shared/client_data/character_outfit.h`
- Create: `src/shared/client_data/character_outfit.cpp`
- Create: `src/tests/client_data_tests/CMakeLists.txt`
- Test: `src/tests/client_data_tests/test_character_outfit.cpp`
- Modify: `src/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `mmo::proto_client::CharacterOutfit` with accessors `race()` (`int32`, default `-1`), `gender()` (`int32`, default `-1`), `item_displays()` (repeated `uint32`) and `animation()` (`string`).
  - `mmo::proto_client::ClassEntry::outfits()` / `outfits_size()` / `mutable_outfits(int)` / `add_outfits()`.
  - `const mmo::proto_client::CharacterOutfit* mmo::proto_client::SelectCharacterOutfit(const ClassEntry& classEntry, int32 race, int32 gender);` declared in `client_data/character_outfit.h`.
  - The server-side twin `mmo::proto::CharacterOutfit` with identical accessors, used by the editor in Task 6.

- [ ] **Step 1: Add the message to the server schema**

In `src/shared/proto_data/classes.proto`, insert this immediately before `message ClassEntry {`:

```proto
// A cosmetic outfit shown on the character creation preview so a freshly created character
// is not displayed naked. This is purely client-side eye candy: it grants nothing and is
// unrelated to the actual starting equipment (see races.proto: initialItems). Item display
// ids are used rather than item ids because the client knows display data from ClientDB,
// while item data is owned by the world server and unavailable during character creation.
// NOTE: field numbers of this message and of the outfits field below are shared with the
// client subset schema in src/shared/client_data/classes.proto - keep them in sync.
message CharacterOutfit {
	// Race id this outfit applies to, or -1 for any race. Race ids are 0-based, so 0 is a
	// real race and cannot serve as the wildcard.
	optional int32 race = 1 [default = -1];

	// Gender this outfit applies to (0 = male, 1 = female), or -1 for any gender.
	optional int32 gender = 2 [default = -1];

	// Item display ids applied to the preview model, in order. Armor and weapons alike.
	repeated uint32 item_displays = 3;

	// Skeleton animation state played while the outfit is shown, e.g. "1HReady", "2HReady"
	// or "2HLReady". Empty keeps the model frame's configured idle animation. This has to be
	// named explicitly because in-world the ready clip comes from the equipped weapon's item
	// subclass, which the character creation screen cannot resolve.
	optional string animation = 4;
}
```

Then, inside `message ClassEntry`, add after the `classlevels = 27` field:

```proto
	// Cosmetic character creation preview outfits (see CharacterOutfit).
	repeated CharacterOutfit outfits = 28;
```

- [ ] **Step 2: Mirror the message into the client schema**

In `src/shared/client_data/classes.proto`, insert this immediately before `message ClassEntry {`:

```proto
// Client subset of the server schema. NOTE: Keep this message and the outfits field number
// in sync with src/shared/proto_data/classes.proto!
message CharacterOutfit {
	// Race id this outfit applies to, or -1 for any race.
	optional int32 race = 1 [default = -1];

	// Gender this outfit applies to (0 = male, 1 = female), or -1 for any gender.
	optional int32 gender = 2 [default = -1];

	// Item display ids applied to the preview model, in order. Armor and weapons alike.
	repeated uint32 item_displays = 3;

	// Skeleton animation state played while the outfit is shown, e.g. "1HReady".
	// Empty keeps the model frame's configured idle animation.
	optional string animation = 4;
}
```

Then replace the trailing comment block inside `message ClassEntry` (the one starting `// NOTE: field 27 is used by the server-side schema`) so the new field sits below it:

```proto
	// NOTE: field 27 is used by the server-side schema (proto_data/classes.proto: classlevels).
	// Do not reuse it here for a different field; class-level progression reaches the client via
	// the KnownClasses packet instead of ClientDB.

	// Cosmetic character creation preview outfits (see CharacterOutfit).
	repeated CharacterOutfit outfits = 28;
```

- [ ] **Step 3: Write the failing test**

Create `src/tests/client_data_tests/test_character_outfit.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "client_data/character_outfit.h"
#include "shared/client_data/proto_client/classes.pb.h"

using namespace mmo;
using namespace mmo::proto_client;

namespace
{
	/// Appends an outfit to a class entry. Pass -1 for race or gender to leave them wildcards.
	CharacterOutfit& AddOutfit(ClassEntry& classEntry, const int32 race, const int32 gender, const uint32 displayId)
	{
		CharacterOutfit* outfit = classEntry.add_outfits();
		outfit->set_race(race);
		outfit->set_gender(gender);
		outfit->add_item_displays(displayId);
		return *outfit;
	}
}

TEST_CASE("A class without outfits selects nothing", "[character_outfit]")
{
	ClassEntry classEntry;

	REQUIRE(SelectCharacterOutfit(classEntry, 0, 0) == nullptr);
}

TEST_CASE("A wildcard outfit matches every race and gender", "[character_outfit]")
{
	ClassEntry classEntry;
	AddOutfit(classEntry, -1, -1, 100);

	const CharacterOutfit* human = SelectCharacterOutfit(classEntry, 0, 0);
	const CharacterOutfit* orcFemale = SelectCharacterOutfit(classEntry, 1, 1);

	REQUIRE(human != nullptr);
	REQUIRE(orcFemale != nullptr);
	REQUIRE(human->item_displays(0) == 100);
	REQUIRE(orcFemale->item_displays(0) == 100);
}

TEST_CASE("An outfit for another race is skipped", "[character_outfit]")
{
	ClassEntry classEntry;
	AddOutfit(classEntry, 1, -1, 200);

	REQUIRE(SelectCharacterOutfit(classEntry, 0, 0) == nullptr);
}

TEST_CASE("An outfit for another gender is skipped", "[character_outfit]")
{
	ClassEntry classEntry;
	AddOutfit(classEntry, -1, 1, 200);

	REQUIRE(SelectCharacterOutfit(classEntry, 0, 0) == nullptr);
}

TEST_CASE("A race match beats the wildcard fallback", "[character_outfit]")
{
	ClassEntry classEntry;
	AddOutfit(classEntry, -1, -1, 100);
	AddOutfit(classEntry, 2, -1, 300);

	const CharacterOutfit* selected = SelectCharacterOutfit(classEntry, 2, 0);

	REQUIRE(selected != nullptr);
	REQUIRE(selected->item_displays(0) == 300);
}

TEST_CASE("An exact race and gender match beats a race only match", "[character_outfit]")
{
	ClassEntry classEntry;
	AddOutfit(classEntry, -1, -1, 100);
	AddOutfit(classEntry, 2, -1, 300);
	AddOutfit(classEntry, 2, 1, 400);

	const CharacterOutfit* female = SelectCharacterOutfit(classEntry, 2, 1);
	const CharacterOutfit* male = SelectCharacterOutfit(classEntry, 2, 0);

	REQUIRE(female != nullptr);
	REQUIRE(male != nullptr);
	REQUIRE(female->item_displays(0) == 400);
	REQUIRE(male->item_displays(0) == 300);
}

TEST_CASE("A gender only match beats the wildcard fallback", "[character_outfit]")
{
	ClassEntry classEntry;
	AddOutfit(classEntry, -1, -1, 100);
	AddOutfit(classEntry, -1, 0, 500);

	const CharacterOutfit* selected = SelectCharacterOutfit(classEntry, 1, 0);

	REQUIRE(selected != nullptr);
	REQUIRE(selected->item_displays(0) == 500);
}

TEST_CASE("The first entry wins when two candidates score the same", "[character_outfit]")
{
	ClassEntry classEntry;
	AddOutfit(classEntry, 0, -1, 600);
	AddOutfit(classEntry, 0, -1, 700);

	const CharacterOutfit* selected = SelectCharacterOutfit(classEntry, 0, 0);

	REQUIRE(selected != nullptr);
	REQUIRE(selected->item_displays(0) == 600);
}

TEST_CASE("An unset race field defaults to the wildcard", "[character_outfit]")
{
	ClassEntry classEntry;
	CharacterOutfit* outfit = classEntry.add_outfits();
	outfit->add_item_displays(800);

	const CharacterOutfit* selected = SelectCharacterOutfit(classEntry, 2, 1);

	REQUIRE(selected != nullptr);
	REQUIRE(selected->item_displays(0) == 800);
}
```

Create `src/tests/client_data_tests/CMakeLists.txt`:

```cmake
mmo_add_test(client_data_tests client_data base log)
```

Register the suite in `src/tests/CMakeLists.txt`. `client_data` is only built when the
client or the editor is built (see the guard in `src/shared/CMakeLists.txt`), so the suite
goes inside the existing graphics guard at the bottom of the file:

```cmake
# Suites that need a graphics device and cannot run on the headless server build.
if (MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR)
	add_subdirectory(client_data_tests)
	add_subdirectory(movement_tests)
	add_subdirectory(scene_graph_tests)
endif()
```

Also update the comment above that guard so it stays truthful:

```cmake
# Suites which need a graphics device, or a library that is only built alongside the client
# or the editor, and so cannot run on the headless server build.
```

- [ ] **Step 4: Run the test to verify it fails**

```bash
cmake -S . -B build -DMMO_BUILD_CLIENT=ON -DMMO_BUILD_EDITOR=ON -DMMO_BUILD_TOOLS=ON -DMMO_WITH_DEV_COMMANDS=ON
```

```bash
cmake --build build --config Debug -t client_data_tests
```

Expected: the build FAILS with a compiler error along the lines of
`cannot open include file: 'client_data/character_outfit.h'`.

- [ ] **Step 5: Write the implementation**

Create `src/shared/client_data/character_outfit.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	namespace proto_client
	{
		class ClassEntry;
		class CharacterOutfit;

		/// Picks the character creation outfit of a class which best matches a race and gender.
		///	Entries whose race or gender is set to something other than the wildcard -1 and does
		///	not match are skipped. Of the remaining entries the most specific one wins: an
		///	explicit race counts double an explicit gender, and the first entry in list order
		///	breaks ties.
		/// @param classEntry The class whose outfit list is searched.
		/// @param race The selected race id.
		/// @param gender The selected gender (0 = male, 1 = female).
		/// @return The best matching outfit, or nullptr when the class defines none that apply.
		const CharacterOutfit* SelectCharacterOutfit(const ClassEntry& classEntry, int32 race, int32 gender);
	}
}
```

Create `src/shared/client_data/character_outfit.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "character_outfit.h"

#include "shared/client_data/proto_client/classes.pb.h"

namespace mmo
{
	namespace proto_client
	{
		namespace
		{
			/// The wildcard value of the race and gender fields.
			constexpr int32 anyValue = -1;
		}

		const CharacterOutfit* SelectCharacterOutfit(const ClassEntry& classEntry, const int32 race, const int32 gender)
		{
			const CharacterOutfit* best = nullptr;
			int32 bestScore = -1;

			for (const CharacterOutfit& outfit : classEntry.outfits())
			{
				if (outfit.race() != anyValue && outfit.race() != race)
				{
					continue;
				}

				if (outfit.gender() != anyValue && outfit.gender() != gender)
				{
					continue;
				}

				// The more specific entry wins. A race match is worth more than a gender match so
				// that a race specific outfit beats a gender specific one.
				int32 score = 0;
				if (outfit.race() != anyValue)
				{
					score += 2;
				}

				if (outfit.gender() != anyValue)
				{
					score += 1;
				}

				// Strictly greater keeps the first entry of equally specific candidates.
				if (score > bestScore)
				{
					bestScore = score;
					best = &outfit;
				}
			}

			return best;
		}
	}
}
```

- [ ] **Step 6: Run the test to verify it passes**

```bash
cmake --build build --config Debug -t client_data_tests
```

```bash
./bin/Debug/client_data_tests.exe "[character_outfit]"
```

Expected: `All tests passed` with 9 test cases.

- [ ] **Step 7: Verify the full suite still builds and passes**

```bash
cmake --build build --config Debug -t all_tests
```

```bash
cd build && ctest -C Debug --output-on-failure
```

Expected: every suite passes, including the new `client_data_tests`.

- [ ] **Step 8: Commit**

```bash
git add src/shared/proto_data/classes.proto src/shared/client_data/classes.proto src/shared/client_data/character_outfit.h src/shared/client_data/character_outfit.cpp src/tests/client_data_tests src/tests/CMakeLists.txt
```

```bash
git commit -m "feat(data): character creation outfits on ClassEntry" -m "Adds a CharacterOutfit message carrying item display ids, scoped by race and gender with -1 as the wildcard, mirrored at the same field numbers in the client subset schema so the editor's verbatim client export carries it. SelectCharacterOutfit picks the most specific match." -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: Apply the outfit to the creation preview

Dresses the model and plays the outfit's ready stance. Nothing is authored yet, so the
visible result after this task is unchanged — that is expected, and Task 3 supplies the data.

**Files:**
- Modify: `src/mmo_client/char_creation/char_create_info.h`
- Modify: `src/mmo_client/char_creation/char_create_info.cpp`

**Interfaces:**
- Consumes: `mmo::proto_client::SelectCharacterOutfit` and `mmo::proto_client::CharacterOutfit` from Task 1; the existing `ApplyItemDisplay` / `ClearItemDisplayAttachments` / `ItemDisplayAttachmentMap` from `shared/game_client/item_display_applier.h`.
- Produces: `CharCreateInfo::ApplyOutfit()`, `CharCreateInfo::ClearItemAttachments()` and `CharCreateInfo::FindOutfit()` as private members; `CharCreateInfo::RefreshModel()` gains the "keep customization" behaviour Task 4's toggle relies on.

**Background the implementer needs:**

`ModelFrame` binds its animation state only inside `OnModelFileChanged` (see
`src/mmo_client/ui/model_frame.cpp`), which fires when the `ModelFile` property changes.
`SetAnimation` on its own therefore does **not** rebind anything. The animation must be set
**before** the model file is set, which is why `RefreshModel` resolves the outfit first.

Item displays hide body parts through `hidden_by_name` / `hidden_by_tag`. Those names are
not necessarily covered by any customization visibility group, so the only reliable way to
undo them is to recreate the entity — `SetModelFile("")` followed by the real mesh. This is
the same trick `CharSelect::SelectCharacter` uses.

- [ ] **Step 1: Extend the header**

In `src/mmo_client/char_creation/char_create_info.h`, add the include next to the existing
ones:

```cpp
#include "game_client/item_display_applier.h"
```

Add the forward declaration to the existing `namespace proto_client` block so it reads:

```cpp
	namespace proto_client
	{
		class CharacterOutfit;
		class ModelDataEntry;
		class Project;
	}
```

Replace the private `RefreshModel` / `ApplyCustomizations` declarations with:

```cpp
	private:
		void RefreshModel();

		void ApplyCustomizations();

		/// Returns the outfit which should currently be shown, or nullptr when the outfit is
		///	hidden or no outfit of the selected class matches the selected race and gender.
		[[nodiscard]] const proto_client::CharacterOutfit* FindOutfit() const;

		/// Attaches and applies every item display of the current outfit to the preview entity.
		///	Does nothing when the outfit is hidden or no outfit matches.
		void ApplyOutfit();

		/// Detaches and destroys every item mesh attached to the preview entity.
		void ClearItemAttachments();
```

Add these public members after `GetSelectedGender()`:

```cpp
		/// Shows or hides the cosmetic creation outfit. Hiding rebuilds the preview model, which
		///	restores body parts the outfit's item displays had hidden.
		void SetOutfitVisible(bool visible);

		/// Returns whether the cosmetic creation outfit is currently shown.
		[[nodiscard]] bool IsOutfitVisible() const { return m_outfitVisible; }
```

Add these data members next to the existing ones:

```cpp
		ItemDisplayAttachmentMap m_itemAttachments;

		bool m_outfitVisible = true;

		String m_defaultAnimation;
```

- [ ] **Step 2: Include the new headers in the implementation**

At the top of `src/mmo_client/char_creation/char_create_info.cpp`, add below the existing
includes:

```cpp
#include "client_data/character_outfit.h"
#include "shared/client_data/proto_client/classes.pb.h"
#include "shared/client_data/proto_client/item_display.pb.h"
#include "scene_graph/entity.h"
```

- [ ] **Step 3: Capture the default animation and drop stale attachments when the frame changes**

Replace `CharCreateInfo::SetCharacterCreationFrame` with:

```cpp
	void CharCreateInfo::SetCharacterCreationFrame(Frame* frame)
	{
		m_frameConnection.disconnect();

		// Entity pointers in m_itemAttachments belong to the previous frame's scene and are now
		// dangling. Drop them before adopting the new frame.
		m_itemAttachments.clear();

		const auto modelFrame = dynamic_cast<ModelFrame*>(frame);
		m_characterCreationFrame = modelFrame;

		// The frame's configured animation is the idle pose to fall back to whenever no outfit
		// names a stance of its own.
		m_defaultAnimation = modelFrame ? modelFrame->GetAnimation() : String();

		m_modelChanged = true;
	}
```

- [ ] **Step 4: Re-dress the preview when the class changes**

Replace `CharCreateInfo::SetSelectedClass` with:

```cpp
	void CharCreateInfo::SetSelectedClass(int32 classId)
	{
		// TODO: Validate class id
		if (classId < 0)
		{
			ELOG("Unknown class selected!");
			return;
		}

		if (m_selectedClass == classId)
		{
			return;
		}

		m_selectedClass = classId;

		// The model itself does not change, but the outfit does, so the preview has to be
		// rebuilt. m_modelChanged stays false so the player's customization choices survive.
		RefreshModel();
	}
```

- [ ] **Step 5: Rework RefreshModel**

Replace the whole of `CharCreateInfo::RefreshModel` with:

```cpp
	void CharCreateInfo::RefreshModel()
	{
		m_propertyNameCache.clear();

		if (!m_characterCreationFrame)
		{
			return;
		}

		// Ensure definition is loaded
		const proto_client::RaceEntry* race = m_project.races.getById(m_selectedRace);
		if (!race)
		{
			return;
		}

		const proto_client::ModelDataEntry* model = m_project.models.getById(m_selectedGender == 0 ? race->malemodel() : race->femalemodel());
		if (!model)
		{
			ELOG("No model id set for race " << race->name() << "!");
			return;
		}

		// Only a model change invalidates the avatar configuration. Switching class rebuilds the
		// entity as well, but has to keep the player's customization choices.
		const bool resetCustomization = m_modelChanged;

		// Entity pointers in m_itemAttachments refer to the entity we are about to destroy.
		ClearItemAttachments();

		// Force entity recreation so sub entity visibility and material overrides are reset to
		// their defaults. Without this, body parts hidden by a previous outfit's item displays
		// would stay hidden.
		m_characterCreationFrame->SetModelFile("");

		// The model frame binds its animation state while the model file is applied, so the
		// stance has to be chosen before the entity is created.
		const proto_client::CharacterOutfit* outfit = FindOutfit();
		m_characterCreationFrame->SetAnimation(outfit && !outfit->animation().empty() ? outfit->animation() : m_defaultAnimation);

		m_selectedModel = model;
		if ((m_selectedModel->flags() & model_data_flags::IsCustomizable) == 0)
		{
			// Simple model, no customizations
			m_avatarDefinition.reset();
			m_characterCreationFrame->SetModelFile(m_selectedModel->filename());
		}
		else
		{
			if (resetCustomization)
			{
				// Reset avatar configuration
				m_configuration.chosenOptionPerGroup.clear();
				m_configuration.scalarValues.clear();
			}

			// Load avatar definition
			m_avatarDefinition = AvatarDefinitionManager::Get().Load(model->filename());
			if (m_avatarDefinition)
			{
				m_characterCreationFrame->SetModelFile(m_avatarDefinition->GetBaseMesh());

				for (const auto& property : *m_avatarDefinition)
				{
					m_propertyNameCache.push_back(property->GetName());

					if (resetCustomization)
					{
						CycleCustomizationProperty(property->GetName(), true, false);
					}
				}
			}
		}

		m_modelChanged = false;
		ApplyCustomizations();
	}
```

- [ ] **Step 6: Re-apply the outfit after every customization pass**

Replace `CharCreateInfo::ApplyCustomizations` with:

```cpp
	void CharCreateInfo::ApplyCustomizations()
	{
		if (m_avatarDefinition)
		{
			for (const auto& property : *m_avatarDefinition)
			{
				property->Apply(*this, m_configuration);
			}
		}

		// Visibility set properties rewrite the visibility of every sub entity carrying their
		// tag, which would undo the body parts the outfit's item displays hide. Re-applying the
		// outfit afterwards is what keeps armor covering the body.
		ApplyOutfit();
	}
```

- [ ] **Step 7: Add the outfit lookup, application and teardown**

Add these three methods to `src/mmo_client/char_creation/char_create_info.cpp`, directly
after `ApplyCustomizations`:

```cpp
	const proto_client::CharacterOutfit* CharCreateInfo::FindOutfit() const
	{
		if (!m_outfitVisible)
		{
			return nullptr;
		}

		const proto_client::ClassEntry* classEntry = m_project.classes.getById(m_selectedClass);
		if (!classEntry)
		{
			return nullptr;
		}

		return proto_client::SelectCharacterOutfit(*classEntry, m_selectedRace, m_selectedGender);
	}

	void CharCreateInfo::ApplyOutfit()
	{
		if (!m_characterCreationFrame || !m_selectedModel)
		{
			return;
		}

		Entity* entity = m_characterCreationFrame->GetEntity();
		if (!entity)
		{
			return;
		}

		const proto_client::CharacterOutfit* outfit = FindOutfit();
		if (!outfit)
		{
			return;
		}

		for (const uint32 displayId : outfit->item_displays())
		{
			if (displayId == 0)
			{
				continue;
			}

			const proto_client::ItemDisplayEntry* displayData = m_project.itemDisplays.getById(displayId);
			if (!displayData)
			{
				WLOG("Character creation outfit references unknown item display " << displayId << "!");
				continue;
			}

			// The creation preview is a hero shot, so weapons go into the hands rather than into
			// their sheath.
			ApplyItemDisplay(m_characterCreationFrame->GetScene(), *entity, m_selectedModel->id(), displayId, *displayData, true, m_itemAttachments);
		}
	}

	void CharCreateInfo::ClearItemAttachments()
	{
		Entity* entity = m_characterCreationFrame ? m_characterCreationFrame->GetEntity() : nullptr;
		if (!entity)
		{
			m_itemAttachments.clear();
			return;
		}

		ClearItemDisplayAttachments(m_characterCreationFrame->GetScene(), *entity, m_itemAttachments);
	}
```

- [ ] **Step 8: Build the client**

```bash
cmake --build build --config Debug -t mmo_client
```

Expected: a clean build. There is nothing to see in game yet — `classes.data` still has no
outfits, so `FindOutfit` returns nullptr and the preview behaves exactly as before.

- [ ] **Step 9: Verify nothing regressed in character creation**

Launch the client, reach the character creation screen and check that race, gender and class
switching still work, that customization cycling still works, and that switching class no
longer resets the customization choices (it previously did nothing at all, so this is a
behaviour improvement, not a regression).

- [ ] **Step 10: Commit**

```bash
git add src/mmo_client/char_creation/char_create_info.h src/mmo_client/char_creation/char_create_info.cpp
```

```bash
git commit -m "feat(client): apply character creation outfits to the preview" -m "CharCreateInfo now dresses the creation preview from the selected class's outfit, reusing the item display applier the character select screen uses, and plays the outfit's ready stance. RefreshModel recreates the entity on every refresh so item display visibility changes are reliably reset, keeping the customization configuration unless the model itself changed." -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: Author the outfit content

Fills `classes.data` in both the editor project and the client's ClientDB so the work from
Task 2 becomes visible.

**Files:**
- Create: `tools/apply_character_outfits.py`
- Modify: `data/editor/data/classes.data` (submodule)
- Modify: `data/client/ClientDB/classes.data` (submodule)

**Interfaces:**
- Consumes: the `outfits` field from Task 1 and the preview application from Task 2.
- Produces: authored outfits for class ids 0-4, which Tasks 4-6 are verified against.

**The chosen displays.** These were picked by decoding `item_displays.data`: only entries
that actually define variants can change anything, and helmets are deliberately excluded
because they hide the face the player is customizing.

| Class | Item displays (in order) | Animation |
|---|---|---|
| 0 Mage | 51 Battlemage Chest - White, 55 Battlemage Pants - White, 54 Battlemage Boots - White, 52 Battlemage Belt - White, 50 Battlemage Cloak Outfit - White, 76 Bent Staff | `2HLReady` |
| 1 Warrior | 72 Frayed Training Shirt, 164 Guard Leggings, 165 Westwatch Mail Boots, 169 Haven Guard Cloak, 111 Weathered Iron Shortsword, 130 Recruit's Training Shield | `1HReady` |
| 2 Cleric | 155 Robes of the Luminous Aegis, 154 Leggings of the Benevolent, 157 Boots of the Templar, 153 Belt of Sanctity, 150 Cloak of Radiant Devotion, 75 Worn Mace | `1HReady` |
| 3 Acolyte | 131 Initiate's Robe, 132 Initiate's Pants, 121 Dawnmere Prayer Sash, 133 Acolyte Staff | `2HLReady` |
| 4 Scout | 72 Frayed Training Shirt, 70 Frayed Training Pants, 115 Reedwalker Boots, 117 Poacher's Waxed Cloak, 74 Worn Dagger | `1HReady` |

Every entry is a wildcard (`race = -1`, `gender = -1`). The armor displays are either
model-agnostic (`model = 0`) or split across the Human male/female models 8 and 7, and their
sub entity names (`Naked_Body`, `Chest_Battlemage_01`, ...) come from the Human meshes. On
races whose mesh lacks those sub entities the armor silently does nothing while the weapon,
which attaches to a bone, still shows. Step 5 measures what that actually looks like.

- [ ] **Step 1: Write the authoring script**

Create `tools/apply_character_outfits.py`:

```python
#!/usr/bin/env python3
"""Writes the character creation preview outfits into classes.data.

Both the editor project file and the client's exported ClientDB copy are written, because
the editor's client export is a plain file copy and re-running it is a manual step.

Run from the repository root:

    python tools/apply_character_outfits.py

Pass --dry-run to print what would change without writing anything.
"""

from __future__ import annotations

import argparse
import importlib
import subprocess
import sys
import tempfile
from pathlib import Path

# class id -> (item display ids in application order, ready animation state)
OUTFITS = {
    0: ([51, 55, 54, 52, 50, 76], "2HLReady"),   # Mage: Battlemage white set + bent staff
    1: ([72, 164, 165, 169, 111, 130], "1HReady"),  # Warrior: guard kit + sword and shield
    2: ([155, 154, 157, 153, 150, 75], "1HReady"),  # Cleric: Luminous Aegis set + mace
    3: ([131, 132, 121, 133], "2HLReady"),       # Acolyte: initiate robes + acolyte staff
    4: ([72, 70, 115, 117, 74], "1HReady"),      # Scout: traveller leathers + dagger
}

DATA_FILES = (
    Path("data") / "editor" / "data" / "classes.data",
    Path("data") / "client" / "ClientDB" / "classes.data",
)


def find_protoc(repo_root: Path) -> Path:
    for config in ("Release", "RelWithDebInfo", "Debug"):
        candidate = repo_root / "build" / "_deps" / "protobuf-build" / config / "protoc.exe"
        if candidate.is_file():
            return candidate

    raise SystemExit(
        "protoc.exe not found under build/_deps/protobuf-build. Configure and build the "
        "project first."
    )


def load_classes_module(repo_root: Path):
    proto_dir = repo_root / "src" / "shared" / "proto_data"
    out_dir = Path(tempfile.mkdtemp(prefix="mmo_outfits_proto_"))
    subprocess.run(
        [str(find_protoc(repo_root)), f"-I{proto_dir}", f"--python_out={out_dir}", "classes.proto"],
        check=True,
        cwd=proto_dir,
    )
    sys.path.insert(0, str(out_dir))
    return importlib.import_module("classes_pb2")


def apply(path: Path, classes_pb2, dry_run: bool) -> None:
    classes = classes_pb2.Classes()
    classes.ParseFromString(path.read_bytes())

    for entry in classes.entry:
        if entry.id not in OUTFITS:
            continue

        displays, animation = OUTFITS[entry.id]

        # Replace rather than append so the script is safe to re-run.
        del entry.outfits[:]

        outfit = entry.outfits.add()
        outfit.race = -1
        outfit.gender = -1
        outfit.animation = animation
        outfit.item_displays.extend(displays)

        print(f"{path}: class {entry.id} ({entry.name}) -> {list(displays)} / {animation}")

    if dry_run:
        return

    path.write_bytes(classes.SerializeToString())


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    classes_pb2 = load_classes_module(repo_root)

    for relative in DATA_FILES:
        path = repo_root / relative
        if not path.is_file():
            raise SystemExit(f"{path} not found. Did you initialise the data submodules?")

        apply(path, classes_pb2, args.dry_run)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Dry-run the script**

```bash
python tools/apply_character_outfits.py --dry-run
```

Expected: ten lines (five classes × two files) naming Mage, Warrior, Cleric, Acolyte and
Scout with their display lists, and no file modified (`git -C data/editor status --short`
stays empty).

- [ ] **Step 3: Apply it**

```bash
python tools/apply_character_outfits.py
```

- [ ] **Step 4: Verify the data round-trips**

```bash
python tools/apply_character_outfits.py --dry-run
```

Expected: the same ten lines. Then confirm both files changed:

```bash
git -C data/editor status --short && git -C data/client status --short
```

Expected: `M data/classes.data` and `M ClientDB/classes.data` respectively.

- [ ] **Step 5: Verify in the client and record the race coverage**

```bash
cmake --build build --config Debug -t mmo_client
```

Launch the client, reach character creation, and for **each of the 3 races × 2 genders × 5
classes** confirm what shows. Record, in the commit message, which combinations show full
armor and which fall back to weapon-only. Do not add or invent art to close gaps — the gap
is the finding.

Check specifically that:
- Human male and female show the full sets for every class.
- Switching class swaps the outfit without resetting customization choices.
- The weapon sits in the hand (not on the back or hip) and the stance is the ready pose.
- Cycling a customization property does not make the body reappear through the armor.

- [ ] **Step 6: Commit the submodules, then the superproject**

```bash
git -C data/editor add data/classes.data && git -C data/editor commit -m "data: character creation preview outfits for all five classes" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

```bash
git -C data/client add ClientDB/classes.data && git -C data/client commit -m "data: character creation preview outfits for all five classes" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

```bash
git add tools/apply_character_outfits.py data/editor data/client
```

```bash
git commit -m "feat(content): author character creation outfits for every class" -m "<replace this line with the per-race coverage recorded in Step 5>" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: Show/hide toggle in the client

**Files:**
- Modify: `src/mmo_client/char_creation/char_create_info.cpp`

**Interfaces:**
- Consumes: `CharCreateInfo::RefreshModel()`, `m_outfitVisible` and the `SetOutfitVisible` / `IsOutfitVisible` declarations added in Task 2.
- Produces: the Lua globals `SetCharCreateOutfitVisible(visible)` and `IsCharCreateOutfitVisible()`, which Task 5's UI calls.

- [ ] **Step 1: Implement the setter**

Add to `src/mmo_client/char_creation/char_create_info.cpp`, after `SetSelectedGender`:

```cpp
	void CharCreateInfo::SetOutfitVisible(const bool visible)
	{
		if (m_outfitVisible == visible)
		{
			return;
		}

		m_outfitVisible = visible;

		// Item displays hide body parts by sub entity name and by tag, and nothing restores those
		// on its own, so the preview model has to be rebuilt. m_modelChanged stays false, which
		// keeps the player's customization choices.
		RefreshModel();
	}
```

- [ ] **Step 2: Register the Lua bindings**

In `CharCreateInfo::RegisterScriptFunctions`, add these two lines after the
`luabind::def_lambda("GetCharacterClass", ...)` entry (keep the trailing comma on the line
above and on these):

```cpp
			luabind::def_lambda("SetCharCreateOutfitVisible", [this](bool visible) { SetOutfitVisible(visible); }),
			luabind::def_lambda("IsCharCreateOutfitVisible", [this]() { return IsOutfitVisible(); }),
```

Note the luabind comma trap: every entry in the `LUABIND_MODULE` list is comma-separated and
the last one must have **no** trailing comma.

- [ ] **Step 3: Build**

```bash
cmake --build build --config Debug -t mmo_client
```

Expected: a clean build.

- [ ] **Step 4: Verify through the console**

Launch the client, reach character creation, open the console (the `^` / backtick key) and
run:

```
/script SetCharCreateOutfitVisible(false)
```

Expected: the character strips down to underwear and returns to the idle pose. Then:

```
/script SetCharCreateOutfitVisible(true)
```

Expected: the outfit and the ready stance come back, with the customization choices
(skin tone, hair, face) unchanged across both toggles.

If `/script` is not available in the glue screen console, verify in Task 5 instead through
the checkbox and note that here.

- [ ] **Step 5: Commit**

```bash
git add src/mmo_client/char_creation/char_create_info.cpp
```

```bash
git commit -m "feat(client): toggle the character creation outfit from Lua" -m "SetCharCreateOutfitVisible rebuilds the preview model, which is the only reliable way to restore body parts an item display hid by name or by tag." -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: Show/hide checkbox in the creation UI

**Files:**
- Modify: `data/client/Interface/GlueUI/CharCreate.xml` (submodule)
- Modify: `data/client/Interface/GlueUI/CharCreate.lua` (submodule)
- Modify: `data/client/Locales/Locale_enUS/Localization.txt` (submodule)
- Modify: `data/client/Locales/Locale_deDE/Localization.txt` (submodule)
- Modify: `data/client/Locales/Locale_frFR/Localization.txt` (submodule)
- Modify: `data/client/Locales/Locale_ruRU/Localization.txt` (submodule)

**Interfaces:**
- Consumes: `SetCharCreateOutfitVisible(visible)` and `IsCharCreateOutfitVisible()` from Task 4; the existing `ListCheckButtonBase` template declared at the top of `CharCreate.xml`.
- Produces: the frame `CharCreateShowOutfitButton` and the Lua function `CharCreate_ToggleOutfit(this)`.

The checkbox goes on `CharCreatePage2` — the customization page — because that is where the
player picks skin tone and face and therefore where stripping the gear is useful.

- [ ] **Step 1: Add the localized string in all four locales**

In `data/client/Locales/Locale_enUS/Localization.txt`, next to the existing
`(key = "CUSTOMIZATION", ...)` line, add:

```
	(key = "SHOW_OUTFIT", string = "Show Armor")
```

In `data/client/Locales/Locale_deDE/Localization.txt`:

```
	(key = "SHOW_OUTFIT", string = "Rüstung anzeigen")
```

In `data/client/Locales/Locale_frFR/Localization.txt`:

```
	(key = "SHOW_OUTFIT", string = "Afficher l'armure")
```

In `data/client/Locales/Locale_ruRU/Localization.txt`:

```
	(key = "SHOW_OUTFIT", string = "Показать доспехи")
```

Match each file's existing indentation (a single tab) and place the entry near the other
character creation keys.

- [ ] **Step 2: Add the checkbox to the layout**

In `data/client/Interface/GlueUI/CharCreate.xml`, inside `CharCreatePropertiesFrame`, add
this frame after the `CharCreatePropertyList` frame:

```xml
				<Frame name="CharCreateShowOutfitButton" inherits="ListCheckButtonBase">
					<Property name="Text" value="SHOW_OUTFIT" />
					<Property name="Checked" value="true" />
					<Area>
						<Anchor point="TOP" relativePoint="BOTTOM" relativeTo="CharCreatePropertyList" offset="16" />
						<Anchor point="H_CENTER" />
					</Area>
					<Scripts><OnClick>return function(this) CharCreate_ToggleOutfit(this); end</OnClick></Scripts>
				</Frame>
```

In the same file, grow `CharCreatePropertiesFrame` so the checkbox fits. Change:

```xml
				<Area><Anchor point="TOP" offset="32" /><Anchor point="RIGHT" offset="-32" /><Size><AbsDimension x="920" y="536" /></Size></Area>
```

to:

```xml
				<Area><Anchor point="TOP" offset="32" /><Anchor point="RIGHT" offset="-32" /><Size><AbsDimension x="920" y="680" /></Size></Area>
```

- [ ] **Step 3: Wire the Lua handler and keep the checkbox in sync**

In `data/client/Interface/GlueUI/CharCreate.lua`, add this function next to the other
`OnXChange_Clicked` handlers:

```lua
function CharCreate_ToggleOutfit(this)
	SetCharCreateOutfitVisible(this:IsChecked());
end
```

A checkable `Button` toggles its own checked state on mouse up before `OnClick` runs (see
`Button::OnMouseUp` in `src/shared/frame_ui/button.cpp`), so `this:IsChecked()` already
holds the new value here.

Then, in `CharCreate_ShowCustomizationPage`, add the sync line so the checkbox can never
disagree with the actual state:

```lua
function CharCreate_ShowCustomizationPage()
	CharCreatePage1:Hide();
	CharCreatePage2:Show();
	CharCreateShowOutfitButton:SetChecked(IsCharCreateOutfitVisible());
	CharCreateModel:SetProperty("Zoom", "2.25");
	CharCreateModel:SetProperty("OffsetY", "1.35");
	CharCreate:StopAnimation("BackgroundZoomOut");
	CharCreate:PlayAnimation("BackgroundZoomIn");
	charCreateBackdropZoomed = true;
	NewCharacterNameBox:CaptureInput();
end
```

- [ ] **Step 4: Verify in the client**

Launch the client, create a character up to the customization page and check:
- The checkbox reads "Show Armor" (not the raw `SHOW_OUTFIT` key — a raw key means the
  locale entry is missing or misplaced).
- It starts checked, and the character is wearing the outfit.
- Unchecking strips the character to underwear and returns them to the idle pose; the
  customization property buttons still work and still show the correct values.
- Re-checking restores the outfit and the ready stance.
- Going back to page 1, changing class, and returning to page 2 keeps the checkbox in sync.

- [ ] **Step 5: Commit the submodule, then the superproject**

```bash
git -C data/client add Interface/GlueUI/CharCreate.xml Interface/GlueUI/CharCreate.lua Locales/Locale_enUS/Localization.txt Locales/Locale_deDE/Localization.txt Locales/Locale_frFR/Localization.txt Locales/Locale_ruRU/Localization.txt
```

```bash
git -C data/client commit -m "feat(ui): show/hide armor checkbox on the character customization page" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

```bash
git add data/client && git commit -m "feat(ui): show/hide armor checkbox in character creation" -m "Adds the SHOW_OUTFIT checkbox to the customization page so a player can strip the cosmetic outfit to inspect skin tone, hair and face. Localized in all four locales." -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: Outfit authoring in the class editor

**Files:**
- Modify: `src/mmo_edit/editor_windows/class_editor_window.cpp`

**Interfaces:**
- Consumes: the server-side `mmo::proto::CharacterOutfit` and `ClassEntry::outfits()` from Task 1; the existing `ScopedEditorSection`, `DrawSuccessButton`, `DrawDangerButton` and `DrawHelpMarker` helpers from `editor_imgui_helpers.h` (already included by this file).
- Produces: nothing other tasks depend on.

- [ ] **Step 1: Add the section**

In `src/mmo_edit/editor_windows/class_editor_window.cpp`, inside `DrawDetailsImpl`, add this
block immediately after the closing brace of the existing `ScopedEditorSection("Spells", ...)`
block and before the closing brace of `DrawDetailsImpl`:

```cpp
		static const char* s_outfitAnyRace = "<Any Race>";
		static const char* s_outfitAnyGender = "<Any>";
		static const char* s_outfitGenders[] = { "Male", "Female" };
		static const char* s_outfitDisplayNone = "<None>";

		if (const auto section = ScopedEditorSection("Character Creation Outfits", ImGuiTreeNodeFlags_None))
		{
			ImGui::TextDisabled("Cosmetic only: item displays shown on the character creation preview. Grants nothing in game and is unrelated to the race's initial items.");

			if (DrawSuccessButton("Add Outfit", ImVec2(-1, 0)))
			{
				auto* newOutfit = currentEntry.add_outfits();
				newOutfit->set_race(-1);
				newOutfit->set_gender(-1);
			}

			for (int index = 0; index < currentEntry.outfits_size(); ++index)
			{
				auto* outfit = currentEntry.mutable_outfits(index);

				ImGui::PushID(index);
				ImGui::Separator();

				const auto* raceEntry = outfit->race() >= 0 ? m_project.races.getById(outfit->race()) : nullptr;
				if (ImGui::BeginCombo("Race", raceEntry != nullptr ? raceEntry->name().c_str() : s_outfitAnyRace, ImGuiComboFlags_None))
				{
					if (ImGui::Selectable(s_outfitAnyRace, outfit->race() < 0))
					{
						outfit->set_race(-1);
					}

					for (int i = 0; i < m_project.races.count(); ++i)
					{
						ImGui::PushID(i);
						const auto& race = m_project.races.getTemplates().entry(i);
						const bool item_selected = static_cast<int32>(race.id()) == outfit->race();
						if (ImGui::Selectable(race.name().c_str(), item_selected))
						{
							outfit->set_race(static_cast<int32>(race.id()));
						}
						if (item_selected)
						{
							ImGui::SetItemDefaultFocus();
						}
						ImGui::PopID();
					}

					ImGui::EndCombo();
				}

				const int32 gender = outfit->gender();
				const char* genderLabel = (gender == 0 || gender == 1) ? s_outfitGenders[gender] : s_outfitAnyGender;
				if (ImGui::BeginCombo("Gender", genderLabel, ImGuiComboFlags_None))
				{
					if (ImGui::Selectable(s_outfitAnyGender, gender < 0))
					{
						outfit->set_gender(-1);
					}
					if (ImGui::Selectable(s_outfitGenders[0], gender == 0))
					{
						outfit->set_gender(0);
					}
					if (ImGui::Selectable(s_outfitGenders[1], gender == 1))
					{
						outfit->set_gender(1);
					}

					ImGui::EndCombo();
				}

				String animation = outfit->animation();
				if (ImGui::InputText("Ready Animation", &animation))
				{
					outfit->set_animation(animation);
				}
				ImGui::SameLine();
				DrawHelpMarker("Animation state played while the outfit is shown, for example 1HReady, 2HReady or 2HLReady. Leave empty to keep the idle pose.");

				if (DrawSuccessButton("Add Item Display", ImVec2(-1, 0)))
				{
					outfit->add_item_displays(0);
				}

				for (int displayIndex = 0; displayIndex < outfit->item_displays_size(); ++displayIndex)
				{
					ImGui::PushID(displayIndex);

					const uint32 displayId = outfit->item_displays(displayIndex);
					const auto* displayEntry = m_project.itemDisplays.getById(displayId);
					if (ImGui::BeginCombo("##itemDisplay", displayEntry != nullptr ? displayEntry->name().c_str() : s_outfitDisplayNone, ImGuiComboFlags_None))
					{
						for (int i = 0; i < m_project.itemDisplays.count(); ++i)
						{
							ImGui::PushID(i);
							const auto& display = m_project.itemDisplays.getTemplates().entry(i);
							const bool item_selected = display.id() == displayId;
							if (ImGui::Selectable(display.name().c_str(), item_selected))
							{
								outfit->set_item_displays(displayIndex, display.id());
							}
							if (item_selected)
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
						outfit->mutable_item_displays()->erase(outfit->mutable_item_displays()->begin() + displayIndex);
						displayIndex--;
					}

					ImGui::PopID();
				}

				if (DrawDangerButton("Remove Outfit"))
				{
					currentEntry.mutable_outfits()->erase(currentEntry.mutable_outfits()->begin() + index);
					index--;
				}

				ImGui::PopID();
			}
		}
```

- [ ] **Step 2: Build the editor**

```bash
cmake --build build --config Debug -t mmo_edit
```

Expected: a clean build.

- [ ] **Step 3: Verify in the editor**

Launch `bin/Debug/mmo_edit.exe`, open the Classes window and select Warrior. Check that:
- The "Character Creation Outfits" section lists the outfit authored in Task 3, with
  `<Any Race>`, `<Any>`, `1HReady` and six item display rows naming real displays.
- "Add Item Display" appends a `<None>` row and the combo can set it to any display.
- "Remove" deletes the right row (remove the row you just added and confirm the remaining
  six are unchanged).
- "Add Outfit" appends a second entry defaulting to `<Any Race>` / `<Any>`, and
  "Remove Outfit" deletes it.
- Save the project, reopen the editor and confirm the Warrior outfit is unchanged.

Leave the data as it was authored in Task 3 — undo any experimental rows before saving, or
re-run `python tools/apply_character_outfits.py` to restore it.

- [ ] **Step 4: Confirm the data files are unchanged**

```bash
git -C data/editor status --short
```

Expected: empty output. If it is not, re-run `python tools/apply_character_outfits.py` and
check again.

- [ ] **Step 5: Commit**

```bash
git add src/mmo_edit/editor_windows/class_editor_window.cpp
```

```bash
git commit -m "feat(editor): author character creation outfits in the class editor" -m "Adds a Character Creation Outfits section with race and gender scoping, a ready animation field and an item display list." -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Final verification

- [ ] **Run the gate**

```bash
cmake --build build --config Debug -t all_tests
```

```bash
cd build && ctest -C Debug --output-on-failure
```

Then run `/gate`, which runs `tools/gate/verify.ps1` (Debug build + unit tests + E2E)
followed by a code review of the branch diff. `/ship` refuses to merge without a green,
HEAD-matching `tools/gate/last_report.json`.

- [ ] **Confirm no protocol change slipped in**

```bash
python tools/protocol_version_check.py
```

Expected: it passes without asking for a version bump. This work touches only ClientDB data
and client-side rendering, so a request to bump would mean something unintended changed.
