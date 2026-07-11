# NPC Gossip Voice Lines Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Play a client-local gossip voice line at an NPC's position when the player clicks a friendly NPC, switching to "pissed" voice lines after 4+ consecutive clicks on the same unit.

**Architecture:** Two new optional fields on `ModelDataEntry` (both proto mirrors) reference `SoundEntry` ids. A new `UnitGossipVoice` singleton (modeled on `CastErrorVoice`) owns click-count/throttle state and plays entries via the existing `SoundEntryPlayer`. `PlayerController::OnMouseUp` is the single trigger point. The editor's `ModelEditorWindow` gets two sound-entry pickers via a helper extracted from `ZoneEditorWindow`.

**Tech Stack:** C++17, protobuf (proto2), ImGui (editor), existing `SoundEntryPlayer`/`IAudio` stack. No server, network, or Lua changes.

**Spec:** `docs/superpowers/specs/2026-07-11-npc-gossip-voice-design.md`

## Global Constraints

- Code style: Allman braces, tabs, `m_camelCase` members, `PascalCase` methods, `camelCase` for locals/anonymous-namespace constants.
- Every new source file starts with: `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- No exceptions (`SIMPLE_NO_EXCEPTIONS`); headers use `#pragma once`; Doxygen comments on public members.
- **ClientDB field-number invariant:** `src/shared/proto_data/model_data.proto` (package `mmo.proto`) and `src/shared/client_data/model_data.proto` (package `mmo.proto_client`) must use IDENTICAL field numbers — the client reads binary data exported from the proto_data schema.
- Build directory is `build/`, config `Debug` unless stated otherwise. Generated protobuf code regenerates automatically when a `.proto` file changes (custom command in each proto CMakeLists).
- **Testing reality:** the unit-test/E2E harnesses do not cover client audio systems (`CastErrorVoice` has no tests either, and `mmo_client` has no test target). Verification per task = successful compilation of the affected targets; end-to-end behavior is verified manually in Task 5. Do not invent test scaffolding.

---

### Task 1: Proto schema fields (both mirrors)

**Files:**
- Modify: `src/shared/proto_data/model_data.proto`
- Modify: `src/shared/client_data/model_data.proto`

**Interfaces:**
- Consumes: nothing.
- Produces: `gossip_sound_id()` / `gossip_pissed_sound_id()` accessors (returning `uint32`, default 0) and `set_gossip_sound_id(uint32)` / `set_gossip_pissed_sound_id(uint32)` setters on `mmo::proto::ModelDataEntry` (editor side) and `mmo::proto_client::ModelDataEntry` (client side). Used by Tasks 2 and 4.

- [ ] **Step 1: Add the fields to the editor/server schema**

Replace the full content of `src/shared/proto_data/model_data.proto` with:

```proto
syntax = "proto2";
package mmo.proto;

// NOTE: Keep this file in sync with src/shared/client_data/model_data.proto!
// The client reads the exported binary data with the mirrored proto_client schema,
// so every field number in here has to match the mirror exactly.

message ModelDataEntry {
	required uint32 id = 1;
	required string name = 2;
	optional uint32 flags = 3;
	required string filename = 4;
	map<uint32, uint32> customizationProperties = 5;

	// SoundEntry id played when the player clicks / interacts with a friendly unit
	// using this display model. 0 = no gossip sound (default).
	optional uint32 gossip_sound_id = 6 [default = 0];

	// SoundEntry id played instead of gossip_sound_id after the player has pestered
	// the same unit repeatedly. 0 = keep playing the normal gossip sound (default).
	optional uint32 gossip_pissed_sound_id = 7 [default = 0];
}

message ModelDatas {
	repeated ModelDataEntry entry = 1;
}
```

- [ ] **Step 2: Mirror the fields in the client schema**

Replace the full content of `src/shared/client_data/model_data.proto` with (note the different package name — everything else identical):

```proto
syntax = "proto2";
package mmo.proto_client;

// NOTE: Keep this file in sync with src/shared/proto_data/model_data.proto!
// The client reads the exported binary data with this mirrored schema,
// so every field number in here has to match the mirror exactly.

message ModelDataEntry {
	required uint32 id = 1;
	required string name = 2;
	optional uint32 flags = 3;
	required string filename = 4;
	map<uint32, uint32> customizationProperties = 5;

	// SoundEntry id played when the player clicks / interacts with a friendly unit
	// using this display model. 0 = no gossip sound (default).
	optional uint32 gossip_sound_id = 6 [default = 0];

	// SoundEntry id played instead of gossip_sound_id after the player has pestered
	// the same unit repeatedly. 0 = keep playing the normal gossip sound (default).
	optional uint32 gossip_pissed_sound_id = 7 [default = 0];
}

message ModelDatas {
	repeated ModelDataEntry entry = 1;
}
```

- [ ] **Step 3: Build both proto libraries to regenerate and compile the code**

Run:
```powershell
cmake --build build -t proto_data --config Debug
cmake --build build -t client_data --config Debug
```
Expected: both succeed; protoc regenerates `model_data.pb.cc/.h` in each library's binary dir.

- [ ] **Step 4: Commit**

```powershell
git add src/shared/proto_data/model_data.proto src/shared/client_data/model_data.proto
git commit -m @'
Add gossip sound entry fields to ModelDataEntry

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 2: UnitGossipVoice client system

**Files:**
- Create: `src/mmo_client/systems/unit_gossip_voice.h`
- Create: `src/mmo_client/systems/unit_gossip_voice.cpp`
- Modify: `src/mmo_client/client_application.cpp` (initialization, next to `CastErrorVoice` at line ~182)
- Modify: `src/mmo_client/game_states/world_state.cpp` (`WorldState::OnLeave` at line ~430)

**Interfaces:**
- Consumes: `SoundEntryPlayer::PlayEntry(uint32 soundId, const Vector3& position)` → `ChannelIndex`, `SoundEntryPlayer::GetEntryLength(uint32) const` → `float` seconds (both existing, `src/shared/game_client/sound_entry_player.h`); `proto_client::ModelDataManager::getById(uint32)` → `const proto_client::ModelDataEntry*`; the Task 1 accessors `gossip_sound_id()` / `gossip_pissed_sound_id()`.
- Produces: `UnitGossipVoice::Get()` singleton with `Initialize(SoundEntryPlayer*, const proto_client::ModelDataManager*)`, `OnUnitClicked(GameUnitC&)`, `Reset()`. Task 3 calls `OnUnitClicked`.

- [ ] **Step 1: Write the header**

Create `src/mmo_client/systems/unit_gossip_voice.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include "client_data/project.h"

namespace mmo
{
	class GameUnitC;
	class SoundEntryPlayer;

	/// @brief Plays a gossip voice line at a friendly NPC's position when the player
	/// clicks (selects) or interacts with it. The voice line is configured per model
	/// display id (ModelDataEntry::gossip_sound_id); after repeatedly pestering the
	/// same unit, an annoyed line (gossip_pissed_sound_id) plays instead.
	///
	/// The system is fully client-local. While a line is (likely) still playing,
	/// further clicks are ignored entirely - no sound and no annoyance increment.
	/// The annoyance counter resets when a different unit is clicked or when the
	/// unit was left alone for a while.
	class UnitGossipVoice final : public NonCopyable
	{
	public:
		/// @brief Gets the singleton instance.
		static UnitGossipVoice& Get();

	public:
		/// @brief Initializes the service with its dependencies. Passing nullptrs disables it.
		void Initialize(SoundEntryPlayer* player, const proto_client::ModelDataManager* models);

		/// @brief Notifies the service that the player clicked a friendly, alive NPC.
		/// Plays the gossip (or pissed) voice line of the unit's display model, if any.
		void OnUnitClicked(GameUnitC& unit);

		/// @brief Clears all click/throttle state (called when leaving the world).
		void Reset();

	private:
		UnitGossipVoice() = default;

	private:
		SoundEntryPlayer* m_player = nullptr;
		const proto_client::ModelDataManager* m_models = nullptr;

		/// Guid of the unit the player is currently pestering.
		ObjectGuid m_pesteredGuid = 0;
		/// Counted clicks on that unit.
		uint32 m_clickCount = 0;
		/// Time of the last counted click (for the annoyance decay).
		GameTime m_lastClickTime = 0;
		/// Until this time the current voice line is (likely) still playing.
		GameTime m_busyUntil = 0;
	};
}
```

- [ ] **Step 2: Write the implementation**

Create `src/mmo_client/systems/unit_gossip_voice.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "unit_gossip_voice.h"

#include "base/clock.h"
#include "game_client/game_unit_c.h"
#include "game_client/sound_entry_player.h"

namespace mmo
{
	namespace
	{
		/// Clicks 1 to (pissedClickThreshold - 1) play the normal gossip line; from
		/// this click on, the pissed line plays instead (if one is configured).
		constexpr uint32 pissedClickThreshold = 4;

		/// Time without a counted click after which the annoyance counter resets.
		constexpr GameTime annoyanceResetMs = 30000;
	}

	UnitGossipVoice& UnitGossipVoice::Get()
	{
		static UnitGossipVoice s_instance;
		return s_instance;
	}

	void UnitGossipVoice::Initialize(SoundEntryPlayer* player, const proto_client::ModelDataManager* models)
	{
		m_player = player;
		m_models = models;
		Reset();
	}

	void UnitGossipVoice::Reset()
	{
		m_pesteredGuid = 0;
		m_clickCount = 0;
		m_lastClickTime = 0;
		m_busyUntil = 0;
	}

	void UnitGossipVoice::OnUnitClicked(GameUnitC& unit)
	{
		if (!m_player || !m_models)
		{
			return;
		}

		const uint32 displayId = unit.Get<uint32>(object_fields::DisplayId);
		const proto_client::ModelDataEntry* model = m_models->getById(displayId);
		if (!model || model->gossip_sound_id() == 0)
		{
			return;
		}

		// While a gossip line is (likely) still playing, clicks are ignored entirely:
		// no sound and no annoyance increment. Time based on purpose - channels are
		// recycled, so a stored channel index could no longer belong to the line.
		const GameTime now = GetAsyncTimeMs();
		if (now < m_busyUntil)
		{
			return;
		}

		// Clicking a different unit or leaving this one alone for a while calms it down.
		if (unit.GetGuid() != m_pesteredGuid || now - m_lastClickTime > annoyanceResetMs)
		{
			m_pesteredGuid = unit.GetGuid();
			m_clickCount = 0;
		}

		++m_clickCount;
		m_lastClickTime = now;

		uint32 soundId = model->gossip_sound_id();
		if (m_clickCount >= pissedClickThreshold && model->gossip_pissed_sound_id() != 0)
		{
			soundId = model->gossip_pissed_sound_id();
		}

		// 3D playback at the unit's position; out-of-range non-looped 3D sounds are
		// not started at all, in which case we also don't block follow-up clicks.
		if (m_player->PlayEntry(soundId, unit.GetPosition()) == InvalidChannel)
		{
			return;
		}

		m_busyUntil = now + static_cast<GameTime>(m_player->GetEntryLength(soundId) * 1000.0f);
	}
}
```

Note: `object_fields::DisplayId` comes in through `game_client/game_unit_c.h`, `InvalidChannel` through `sound_entry_player.h` → `shared/audio/audio.h`. If `object_fields` is unresolved, add `#include "game/object_fields.h"` — check how `game_unit_c.cpp` gets it and match.

- [ ] **Step 3: Initialize the singleton at startup**

In `src/mmo_client/client_application.cpp`:

Add next to the existing include (line ~52 `#include "systems/cast_error_voice.h"`):
```cpp
#include "systems/unit_gossip_voice.h"
```

In `LoadProjectAndCache`, directly below the `CastErrorVoice` initialization (line ~182):
```cpp
		CastErrorVoice::Get().Initialize(context.soundEntryPlayer.get(), &context.project->races);
		UnitGossipVoice::Get().Initialize(context.soundEntryPlayer.get(), &context.project->models);
```
(The first line already exists — add only the second.)

- [ ] **Step 4: Reset state when leaving the world**

In `src/mmo_client/game_states/world_state.cpp`:

Add next to the existing include (line ~70 `#include "systems/cast_error_voice.h"`):
```cpp
#include "systems/unit_gossip_voice.h"
```

In `WorldState::OnLeave()` (line ~430), after `m_zoneMusic.Stop(); m_zoneAmbience.Stop();`:
```cpp
		UnitGossipVoice::Get().Reset();
```

- [ ] **Step 5: Build the client**

New files are picked up by the recursive source glob, but CMake must re-run to see them — building the target does that automatically when CMakeLists globs are configured with `CONFIGURE_DEPENDS`; if the new files don't compile into the target, re-run `cmake -S . -B build` first.

Run:
```powershell
cmake --build build -t mmo_client --config Debug
```
Expected: builds without errors and `unit_gossip_voice.cpp` appears in the compile output.

- [ ] **Step 6: Commit**

```powershell
git add src/mmo_client/systems/unit_gossip_voice.h src/mmo_client/systems/unit_gossip_voice.cpp src/mmo_client/client_application.cpp src/mmo_client/game_states/world_state.cpp
git commit -m @'
Add client-local UnitGossipVoice system

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 3: Trigger hook in PlayerController

**Files:**
- Modify: `src/mmo_client/player_controller.cpp` (`OnMouseUp`, lines ~1007–1030)

**Interfaces:**
- Consumes: `UnitGossipVoice::Get().OnUnitClicked(GameUnitC&)` from Task 2; existing `GameObjectC::IsUnit()`, `IsPlayer()`, `AsUnit()`, `GameUnitC::IsAlive()`, `GameUnitC::IsFriendlyTo(GameUnitC&)`.
- Produces: nothing consumed by later tasks.

- [ ] **Step 1: Add the include**

In `src/mmo_client/player_controller.cpp`, next to the other systems includes (lines ~14–23, e.g. after `#include "mmo_client/systems/bank_client.h"`):
```cpp
#include "mmo_client/systems/unit_gossip_voice.h"
```

- [ ] **Step 2: Call OnUnitClicked once per click on a friendly NPC**

In `OnMouseUp`, inside the `if (m_mouseMoved <= 16)` click branch, extend the hovered-object block. Current code (lines ~1011–1022):

```cpp
			if (const auto hoveredObject = GetHoveredObject())
			{
				if (hoveredObject->GetGuid() != previousSelectedUnit)
				{
					m_controlledUnit->SetTargetUnit(ObjectMgr::Get<GameUnitC>(hoveredObject->GetGuid()));
				}

				if (button == MouseButton_Right)
				{
					InteractWithObject(*hoveredObject);
				}
			}
```

New code (insert the gossip block between selection and interaction):

```cpp
			if (const auto hoveredObject = GetHoveredObject())
			{
				if (hoveredObject->GetGuid() != previousSelectedUnit)
				{
					m_controlledUnit->SetTargetUnit(ObjectMgr::Get<GameUnitC>(hoveredObject->GetGuid()));
				}

				// Friendly NPCs greet the player when clicked - both selection (left) and
				// interaction (right) clicks count, exactly once per click. Player characters
				// never gossip, even when their race model has gossip sounds assigned.
				if (hoveredObject->IsUnit() && !hoveredObject->IsPlayer())
				{
					GameUnitC& unit = hoveredObject->AsUnit();
					if (unit.IsAlive() && m_controlledUnit->IsFriendlyTo(unit))
					{
						UnitGossipVoice::Get().OnUnitClicked(unit);
					}
				}

				if (button == MouseButton_Right)
				{
					InteractWithObject(*hoveredObject);
				}
			}
```

- [ ] **Step 3: Build the client**

Run:
```powershell
cmake --build build -t mmo_client --config Debug
```
Expected: builds without errors.

- [ ] **Step 4: Commit**

```powershell
git add src/mmo_client/player_controller.cpp
git commit -m @'
Trigger NPC gossip voice lines on friendly unit clicks

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 4: Editor support in ModelEditorWindow

**Files:**
- Create: `src/mmo_edit/editor_windows/sound_entry_combo.h`
- Modify: `src/mmo_edit/editor_windows/zone_editor_window.h` (remove `DrawSoundEntryCombo` declaration, line ~29–30)
- Modify: `src/mmo_edit/editor_windows/zone_editor_window.cpp` (delegate to the shared helper, lines ~205–267)
- Modify: `src/mmo_edit/editor_windows/model_editor_window.h` (two `ImGuiTextFilter` members)
- Modify: `src/mmo_edit/editor_windows/model_editor_window.cpp` (new "Audio" section in `DrawDetailsImpl`)

**Interfaces:**
- Consumes: Task 1 setters `set_gossip_sound_id(uint32)` / `set_gossip_pissed_sound_id(uint32)` on `proto::ModelDataEntry`; `proto::SoundManager` (`m_project.sounds`, typedef in `src/shared/proto_data/project.h:110`).
- Produces: `mmo::DrawSoundEntryCombo(const proto::SoundManager&, const char* label, uint32 currentSoundId, ImGuiTextFilter&, const std::function<void(uint32)>& setter, const char* noneLabel = "(None)")` — shared inline editor helper.

- [ ] **Step 1: Create the shared sound-entry combo helper**

Create `src/mmo_edit/editor_windows/sound_entry_combo.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <functional>

#include <imgui.h>

#include "base/typedefs.h"
#include "proto_data/project.h"

namespace mmo
{
	/// @brief Draws a filtered combo box to pick a sound entry (0 = none).
	/// @param sounds The sound entry manager to list entries from.
	/// @param label The combo label.
	/// @param currentSoundId The currently assigned sound entry id (0 = none).
	/// @param filter Persistent text filter backing the popup's search box.
	/// @param setter Invoked with the picked sound entry id (0 when cleared).
	/// @param noneLabel Display label of the "no sound" option.
	inline void DrawSoundEntryCombo(const proto::SoundManager& sounds, const char* label, const uint32 currentSoundId, ImGuiTextFilter& filter, const std::function<void(uint32)>& setter, const char* noneLabel = "(None)")
	{
		const proto::SoundEntry* currentSound = nullptr;
		if (currentSoundId != 0)
		{
			currentSound = sounds.getById(currentSoundId);
		}

		if (ImGui::BeginCombo(label, currentSound ? currentSound->name().c_str() : noneLabel, ImGuiComboFlags_HeightLargest))
		{
			if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0))
			{
				ImGui::SetKeyboardFocusHere(0);
			}

			filter.Draw("##sound_filter", -1.0f);

			if (ImGui::Selectable(noneLabel))
			{
				setter(0);
				filter.Clear();
				ImGui::CloseCurrentPopup();
			}

			if (ImGui::BeginChild("##sound_scroll_area", ImVec2(0, 400)))
			{
				for (const auto& sound : sounds.getTemplates().entry())
				{
					// Skipped due to filter
					if (filter.IsActive() && !filter.PassFilter(sound.name().c_str()))
					{
						continue;
					}

					ImGui::PushID(sound.id());
					if (ImGui::Selectable(sound.name().c_str()))
					{
						setter(sound.id());

						filter.Clear();
						ImGui::CloseCurrentPopup();
					}
					ImGui::PopID();
				}
			}
			ImGui::EndChild();

			ImGui::EndCombo();
		}
	}
}
```

- [ ] **Step 2: Make ZoneEditorWindow use the shared helper**

In `src/mmo_edit/editor_windows/zone_editor_window.h`, delete the private method declaration (lines ~29–30):
```cpp
		/// Draws a filtered combo box to pick a sound entry (0 = inherit/none).
		void DrawSoundEntryCombo(const char* label, uint32 currentSoundId, ImGuiTextFilter& filter, const std::function<void(uint32)>& setter);
```
Keep the `m_musicSoundFilter` / `m_ambienceSoundFilter` members.

In `src/mmo_edit/editor_windows/zone_editor_window.cpp`:
1. Add the include below `#include "editor_imgui_helpers.h"`:
```cpp
#include "sound_entry_combo.h"
```
2. Delete the entire member function definition `void ZoneEditorWindow::DrawSoundEntryCombo(...)` (lines ~218–267).
3. Replace the two call sites in the "Audio" section (lines ~207–211) with:
```cpp
			DrawSoundEntryCombo(m_project.sounds, "Music", currentEntry.music_sound(), m_musicSoundFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_music_sound(id); }, "(Inherit / None)");

			DrawSoundEntryCombo(m_project.sounds, "Ambience", currentEntry.ambience_sound(), m_ambienceSoundFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_ambience_sound(id); }, "(Inherit / None)");
```

- [ ] **Step 3: Add the Audio section to ModelEditorWindow**

In `src/mmo_edit/editor_windows/model_editor_window.h`:
1. Add `#include <imgui.h>` below `#include "base/non_copyable.h"` (needed for `ImGuiTextFilter`).
2. Add two members below `std::shared_ptr<CustomizableAvatarDefinition> m_definition;`:
```cpp
		ImGuiTextFilter m_gossipSoundFilter;
		ImGuiTextFilter m_gossipPissedSoundFilter;
```

In `src/mmo_edit/editor_windows/model_editor_window.cpp`:
1. Add the include below `#include "editor_imgui_helpers.h"`:
```cpp
#include "sound_entry_combo.h"
```
2. In `DrawDetailsImpl`, after the closing brace of the `if (const auto section = ScopedEditorSection("Display", ...))` block (line ~714), add:
```cpp
		if (const auto section = ScopedEditorSection("Audio", ImGuiTreeNodeFlags_None))
		{
			DrawSoundEntryCombo(m_project.sounds, "Gossip Sound", currentEntry.gossip_sound_id(), m_gossipSoundFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_gossip_sound_id(id); });
			ImGui::TextDisabled("Voice line played when the player clicks / interacts with a friendly unit using this model.");

			DrawSoundEntryCombo(m_project.sounds, "Gossip Pissed Sound", currentEntry.gossip_pissed_sound_id(), m_gossipPissedSoundFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_gossip_pissed_sound_id(id); });
			ImGui::TextDisabled("Annoyed voice line played after repeatedly clicking the same unit. Falls back to the normal gossip sound when unset.");
		}
```

- [ ] **Step 4: Build the editor**

Run:
```powershell
cmake --build build -t mmo_edit --config Debug
```
Expected: builds without errors. (If the new header isn't picked up, re-run `cmake -S . -B build` once and build again.)

- [ ] **Step 5: Commit**

```powershell
git add src/mmo_edit/editor_windows/sound_entry_combo.h src/mmo_edit/editor_windows/zone_editor_window.h src/mmo_edit/editor_windows/zone_editor_window.cpp src/mmo_edit/editor_windows/model_editor_window.h src/mmo_edit/editor_windows/model_editor_window.cpp
git commit -m @'
Add gossip sound pickers to the model editor

Extracts the zone editor sound entry combo into a shared helper.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 5: Manual verification (requires a human with speakers)

**Files:** none (verification only).

- [ ] **Step 1: Author test data in the editor**

Launch `mmo_edit`, open the Sounds window and pick (or create) two sound entries with voice-like files, category VOICE, `is_3d` enabled, sensible `max_distance` (e.g. 25). In the Models window, select an NPC model in use (e.g. an innkeeper), set "Gossip Sound" and "Gossip Pissed Sound" in the new Audio section, then save/export the project so `data/editor/` and the ClientDB export are updated.

- [ ] **Step 2: Verify in-game behavior**

Start the dev server stack and client, then check with an NPC using that model:
1. Left-click the NPC → a gossip line plays at the NPC's position (3D, quieter when farther away).
2. Clicks 1–3 → normal lines; 4th and later consecutive clicks → pissed lines.
3. Clicking rapidly while a line still plays → silent, and those clicks do NOT advance the counter.
4. Select another unit (or wait >30s), click again → back to normal gossip lines.
5. Right-click interact (e.g. vendor) → gossip line plays alongside the opened dialog.
6. Click a hostile creature and another player → no gossip.
7. An NPC whose model has no gossip sound → silent, no errors in the client log.

- [ ] **Step 3: Report results**

No commit; report any deviation from the expected behavior list.
