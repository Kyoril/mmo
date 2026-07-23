# NPC Goodbye Voice Line Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** NPCs play a per-model-display "goodbye" voice line when the player closes an NPC dialog for real — not when one dialog of the same NPC transitions into another (gossip → quest detail, gossip → vendor/trainer/bank window).

**Architecture:** Fully client-local, extending the existing `UnitGossipVoice` singleton (which already plays hello/pissed lines). A new `goodbye_sound_id` proto field on `ModelDataEntry`; the voice system tracks which dialog *source* (Quest / Vendor / Trainer / Bank) currently has which NPC guid open, and a close only arms a 500 ms deferred goodbye when the guid is no longer open in ANY source slot. A generation counter invalidates scheduled timer events (the client `TimerQueue` has no cancellation).

**Tech Stack:** C++17, protobuf (proto2), luabind, ImGui editor, frame_ui XML/Lua.

**Spec:** `docs/superpowers/specs/2026-07-23-npc-goodbye-voice-design.md`

## Global Constraints

- Allman braces, tabs, `m_camelCase` members, `PascalCase` methods, `#pragma once`, copyright header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` on new files.
- No exceptions; use `ASSERT`/`DLOG`/`ELOG` from `base/macros.h`.
- Proto field numbers MUST match between `src/shared/proto_data/model_data.proto` and `src/shared/client_data/model_data.proto`. The new field number is **9** (8 is taken by `animation_profile`).
- Enum pseudo-namespace pattern: `namespace npc_dialog_source { enum Type { ... }; }`.
- Build config used for verification: `Debug` (matches the dev workflow). Commands run from repo root `H:\mmo`.
- No unit tests for this feature: it lives in a client-only singleton wired to audio + scene objects (same as the existing hello system, which has none). Verification = compile green + manual in-game checklist (Task 5) using the DLOG lines added in Task 2.

---

### Task 1: Proto field + editor picker

**Files:**
- Modify: `src/shared/proto_data/model_data.proto` (add field after line 25)
- Modify: `src/shared/client_data/model_data.proto` (same)
- Modify: `src/mmo_edit/editor_windows/model_editor_window.cpp:750-759` (Audio section)
- Modify: `src/mmo_edit/editor_windows/model_editor_window.h:53-54` (filter member)

**Interfaces:**
- Produces: `proto::ModelDataEntry::goodbye_sound_id()` / `set_goodbye_sound_id(uint32)` and the mirrored `proto_client::ModelDataEntry::goodbye_sound_id()` — Task 2 reads the `proto_client` accessor.

- [ ] **Step 1: Add the field to both proto mirrors**

In `src/shared/proto_data/model_data.proto`, after the `animation_profile` field (line 25), inside `message ModelDataEntry`:

```proto
	// SoundEntry id played when the player closes this NPC's dialog for real,
	// i.e. without another dialog of the same NPC opening right after.
	// 0 = no goodbye sound (default).
	optional uint32 goodbye_sound_id = 9 [default = 0];
```

Add the exact same block at the same position in `src/shared/client_data/model_data.proto` (only the package differs between the two files; field number 9 must match).

- [ ] **Step 2: Add the editor picker**

In `src/mmo_edit/editor_windows/model_editor_window.h`, after line 54 (`ImGuiTextFilter m_gossipPissedSoundFilter;`), add:

```cpp
		ImGuiTextFilter m_goodbyeSoundFilter;
```

In `src/mmo_edit/editor_windows/model_editor_window.cpp`, inside the `if (const auto section = ScopedEditorSection("Audio", ...))` block, after the "Gossip Pissed Sound" combo and its `ImGui::TextDisabled` line (line 758), add:

```cpp
			DrawSoundEntryCombo(m_project.sounds, "Goodbye Sound", currentEntry.goodbye_sound_id(), m_goodbyeSoundFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_goodbye_sound_id(id); });
			ImGui::TextDisabled("Voice line played when the player closes this NPC's dialog without opening another one.");
```

- [ ] **Step 3: Build to regenerate protos and verify compile**

Run:
```powershell
cmake --build build -t mmo_edit --config Debug
```
Expected: build succeeds (protoc regenerates `model_data.pb.h/.cc` for both mirrors; editor compiles with the new accessors).

- [ ] **Step 4: Commit**

```powershell
git add src/shared/proto_data/model_data.proto src/shared/client_data/model_data.proto src/mmo_edit/editor_windows/model_editor_window.cpp src/mmo_edit/editor_windows/model_editor_window.h
git commit -m "Add goodbye_sound_id to model data with editor picker"
```

---

### Task 2: Goodbye engine in UnitGossipVoice

**Files:**
- Modify: `src/mmo_client/systems/unit_gossip_voice.h`
- Modify: `src/mmo_client/systems/unit_gossip_voice.cpp`
- Modify: `src/mmo_client/client_application.cpp:185` (Initialize call site)

**Interfaces:**
- Consumes: `proto_client::ModelDataEntry::goodbye_sound_id()` (Task 1), `TimerQueue::AddEvent(const EventCallback&, GameTime)` (`src/shared/base/timer_queue.h:38`), `ObjectMgr::Get<GameUnitC>(guid)` (`game_client/object_mgr.h`), `GameUnitC::IsAlive()/GetPosition()/Get<uint32>(object_fields::DisplayId)`, `SoundEntryPlayer::PlayEntry(id, position)` / `GetEntryLength(id)`.
- Produces (used by Tasks 3–4):
  - `namespace npc_dialog_source { enum Type { Quest = 0, Vendor = 1, Trainer = 2, Bank = 3, Count_ = 4 }; }`
  - `void UnitGossipVoice::OnNpcDialogOpened(npc_dialog_source::Type source, ObjectGuid guid)`
  - `void UnitGossipVoice::OnNpcDialogClosed(npc_dialog_source::Type source, ObjectGuid guid)`
  - `void UnitGossipVoice::Initialize(SoundEntryPlayer* player, const proto_client::ModelDataManager* models, TimerQueue* timers)` (signature change!)

- [ ] **Step 1: Extend the header**

In `src/mmo_client/systems/unit_gossip_voice.h`:

Add `class TimerQueue;` to the forward declarations (next to `class GameUnitC;`), and the source enum before the class:

```cpp
	class GameUnitC;
	class SoundEntryPlayer;
	class TimerQueue;

	/// Identifies which kind of NPC dialog window an open/close notification
	/// refers to. Each source can have at most one npc dialog open at a time.
	namespace npc_dialog_source
	{
		enum Type
		{
			Quest = 0,
			Vendor = 1,
			Trainer = 2,
			Bank = 3,

			Count_ = 4
		};
	}
```

Replace the `Initialize` declaration and add the new methods in the public section:

```cpp
		/// @brief Initializes the service with its dependencies. Passing nullptrs disables it.
		void Initialize(SoundEntryPlayer* player, const proto_client::ModelDataManager* models, TimerQueue* timers);

		/// @brief Notifies the service that an NPC dialog window (gossip/quest, vendor,
		/// trainer, bank) has opened for the given npc. Cancels a pending goodbye line
		/// of that npc - the conversation continues instead of ending.
		void OnNpcDialogOpened(npc_dialog_source::Type source, ObjectGuid guid);

		/// @brief Notifies the service that an NPC dialog window has closed. If no other
		/// dialog window of the same npc remains open and none opens within a short grace
		/// period, the model's goodbye voice line plays at the npc's position.
		void OnNpcDialogClosed(npc_dialog_source::Type source, ObjectGuid guid);
```

Add to the private section:

```cpp
		/// @brief Timer callback: plays the pending goodbye line if it wasn't cancelled.
		void PlayPendingGoodbye(uint32 generation);
```

and the members (after `m_busyUntil`):

```cpp
		TimerQueue* m_timers = nullptr;

		/// Which npc guid each dialog source currently has open (0 = none).
		ObjectGuid m_openDialogGuids[npc_dialog_source::Count_] = {};
		/// Npc whose goodbye line is armed and waiting for the grace period.
		ObjectGuid m_pendingGoodbyeGuid = 0;
		/// Bumped to invalidate already-scheduled goodbye timer events.
		uint32 m_goodbyeGeneration = 0;
```

Also update the class doc comment: append a line `/// It also plays a goodbye voice line when the npc's last dialog window closes for real.`

- [ ] **Step 2: Implement in the .cpp**

In `src/mmo_client/systems/unit_gossip_voice.cpp`:

Add includes after the existing ones:

```cpp
#include "base/timer_queue.h"
#include "game_client/object_mgr.h"
```

Add to the anonymous namespace:

```cpp
		/// After a dialog closed, another dialog of the same npc may open within this
		/// window (gossip -> vendor etc.) without counting as "closed for real".
		constexpr GameTime goodbyeGraceMs = 500;
```

Update `Initialize` and `Reset`:

```cpp
	void UnitGossipVoice::Initialize(SoundEntryPlayer* player, const proto_client::ModelDataManager* models, TimerQueue* timers)
	{
		m_player = player;
		m_models = models;
		m_timers = timers;
		Reset();
	}

	void UnitGossipVoice::Reset()
	{
		m_pesteredGuid = 0;
		m_clickCount = 0;
		m_lastClickTime = 0;
		m_busyUntil = 0;

		for (ObjectGuid& openGuid : m_openDialogGuids)
		{
			openGuid = 0;
		}
		m_pendingGoodbyeGuid = 0;
		++m_goodbyeGeneration;
	}
```

Add the new methods at the end of the namespace (after `OnUnitClicked`):

```cpp
	void UnitGossipVoice::OnNpcDialogOpened(const npc_dialog_source::Type source, const ObjectGuid guid)
	{
		if (guid == 0)
		{
			return;
		}

		m_openDialogGuids[source] = guid;

		if (m_pendingGoodbyeGuid == guid)
		{
			m_pendingGoodbyeGuid = 0;
			++m_goodbyeGeneration;
			DLOG("Goodbye voice line cancelled: follow-up dialog opened for npc " << guid);
		}
	}

	void UnitGossipVoice::OnNpcDialogClosed(const npc_dialog_source::Type source, const ObjectGuid guid)
	{
		if (guid == 0)
		{
			return;
		}

		if (m_openDialogGuids[source] == guid)
		{
			m_openDialogGuids[source] = 0;
		}

		for (const ObjectGuid openGuid : m_openDialogGuids)
		{
			if (openGuid == guid)
			{
				// Another dialog window of the same npc is still open - the
				// conversation continues, this was only a window transition.
				return;
			}
		}

		if (!m_player || !m_models || !m_timers)
		{
			return;
		}

		m_pendingGoodbyeGuid = guid;
		const uint32 generation = ++m_goodbyeGeneration;
		m_timers->AddEvent([generation]() { UnitGossipVoice::Get().PlayPendingGoodbye(generation); }, GetAsyncTimeMs() + goodbyeGraceMs);
	}

	void UnitGossipVoice::PlayPendingGoodbye(const uint32 generation)
	{
		if (generation != m_goodbyeGeneration || m_pendingGoodbyeGuid == 0)
		{
			return;
		}

		const ObjectGuid guid = m_pendingGoodbyeGuid;
		m_pendingGoodbyeGuid = 0;

		const std::shared_ptr<GameUnitC> unit = ObjectMgr::Get<GameUnitC>(guid);
		if (!unit || !unit->IsAlive())
		{
			return;
		}

		const GameTime now = GetAsyncTimeMs();
		if (now < m_busyUntil)
		{
			DLOG("Goodbye voice line skipped for npc " << guid << ": another voice line is still playing");
			return;
		}

		const uint32 displayId = unit->Get<uint32>(object_fields::DisplayId);
		const proto_client::ModelDataEntry* model = m_models->getById(displayId);
		if (!model || model->goodbye_sound_id() == 0)
		{
			return;
		}

		if (m_player->PlayEntry(model->goodbye_sound_id(), unit->GetPosition()) == InvalidChannel)
		{
			return;
		}

		m_busyUntil = now + static_cast<GameTime>(m_player->GetEntryLength(model->goodbye_sound_id()) * 1000.0f);
		DLOG("Goodbye voice line played for npc " << guid);
	}
```

Note: capturing only `generation` (not `this`) in the timer lambda is deliberate — the singleton is fetched via `Get()` at fire time and outlives everything; the generation check makes stale events (including any scheduled before a `Reset()`) no-ops.

- [ ] **Step 3: Update the Initialize call site**

In `src/mmo_client/client_application.cpp:185`, change:

```cpp
		UnitGossipVoice::Get().Initialize(context.soundEntryPlayer.get(), &context.project->models);
```

to:

```cpp
		UnitGossipVoice::Get().Initialize(context.soundEntryPlayer.get(), &context.project->models, context.timerQueue.get());
```

(`context.timerQueue` is created earlier in startup — `client_application.cpp:110`.)

- [ ] **Step 4: Build**

Run:
```powershell
cmake --build build -t mmo_client --config Debug
```
Expected: build succeeds.

- [ ] **Step 5: Commit**

```powershell
git add src/mmo_client/systems/unit_gossip_voice.h src/mmo_client/systems/unit_gossip_voice.cpp src/mmo_client/client_application.cpp
git commit -m "Add deferred goodbye voice line engine to UnitGossipVoice"
```

---

### Task 3: Open/close hooks in the four client dialog systems

**Files:**
- Modify: `src/mmo_client/systems/quest_client.cpp` (`CloseQuest` at 199, `OnGossipMenu` at ~693, `OnQuestGiverQuestList` at ~760, `OnQuestGiverQuestDetails` at ~785, `OnQuestGiverOfferReward` at ~906, `OnQuestGiverRequestItems` at ~991)
- Modify: `src/mmo_client/systems/vendor_client.cpp` (`CloseVendor` at 70, `OnListInventory` at ~139)
- Modify: `src/mmo_client/systems/trainer_client.cpp` (`CloseTrainer` at 34, `OnTrainerList` at ~118)
- Modify: `src/mmo_client/systems/bank_client.cpp` (`CloseBank` at 59, `OnShowBank` at ~103)

**Interfaces:**
- Consumes: `UnitGossipVoice::Get().OnNpcDialogOpened(npc_dialog_source::Type, ObjectGuid)` and `OnNpcDialogClosed(...)` from Task 2. All four .cpp files need `#include "unit_gossip_voice.h"` (same directory).
- Produces: `QuestClient::CloseQuest()` becomes idempotent (early-out on `m_questGiverGuid == 0`) — Task 4's Lua binding relies on this.

Open hooks go at the **synchronous** point where the packet handler stores the npc guid, NOT where the (sometimes async) UI event fires — ordering with the displaced window's `OnHide` close depends on it.

- [ ] **Step 1: QuestClient close hook + idempotency**

In `src/mmo_client/systems/quest_client.cpp`, add `#include "unit_gossip_voice.h"` next to the existing includes at the top of the file. Replace `CloseQuest` (lines 199-210):

```cpp
	void QuestClient::CloseQuest()
	{
		if (m_questGiverGuid == 0)
		{
			// Already closed - makes the Lua OnHide notification and the server
			// driven close path safely re-entrant.
			return;
		}

		UnitGossipVoice::Get().OnNpcDialogClosed(npc_dialog_source::Quest, m_questGiverGuid);

		m_gossipMenu = 0;
		m_questGiverGuid = 0;
		m_questDetails.Clear();
		m_greetingText.clear();
		m_questList.clear();
		m_gossipActions.clear();

		// Raise UI event to show the quest list window to the user
		FrameManager::Get().TriggerLuaEvent("QUEST_FINISHED");
	}
```

(The early-out also means the `CloseQuest()` call in `QuestClient::Shutdown()` no longer fires a redundant `QUEST_FINISHED` when no dialog was open — intended.)

- [ ] **Step 2: QuestClient open hooks**

Five packet handlers, each right after the point where `m_questGiverGuid` is known:

In `OnGossipMenu`, after `m_questGiverGuid = npcGuid;` (line ~693):

```cpp
		m_questGiverGuid = npcGuid;
		UnitGossipVoice::Get().OnNpcDialogOpened(npc_dialog_source::Quest, m_questGiverGuid);
```

In `OnQuestGiverQuestList`, after the successful read of `m_questGiverGuid` (the `if (!(packet >> io::read<uint64>(m_questGiverGuid) ...)` block ending line ~766):

```cpp
		UnitGossipVoice::Get().OnNpcDialogOpened(npc_dialog_source::Quest, m_questGiverGuid);
```

In `OnQuestGiverQuestDetails`, after the read block `if (!(packet >> io::read<uint64>(m_questGiverGuid) >> io::read<uint32>(m_questDetails.questId)))` (line ~789):

```cpp
		UnitGossipVoice::Get().OnNpcDialogOpened(npc_dialog_source::Quest, m_questGiverGuid);
```

In `OnQuestGiverOfferReward`, after its guid/questId read block (line ~910): same line as above.

In `OnQuestGiverRequestItems`, after its guid/questId read block (line ~995): same line as above.

Do NOT add one to `OnQuestGiverQuestComplete` — that handler is a turn-in toast which immediately calls `CloseQuest()`; the close there IS the real end of the dialog and should arm the goodbye.

- [ ] **Step 3: VendorClient hooks**

In `src/mmo_client/systems/vendor_client.cpp`, add `#include "unit_gossip_voice.h"` at the top. In `CloseVendor` (line 70), notify before clearing:

```cpp
	void VendorClient::CloseVendor()
	{
		if (m_vendorGuid == 0)
		{
			return;
		}

		UnitGossipVoice::Get().OnNpcDialogClosed(npc_dialog_source::Vendor, m_vendorGuid);

		m_vendorGuid = 0;
		m_vendorItems.clear();

		FrameManager::Get().TriggerLuaEvent("VENDOR_CLOSED");
	}
```

In `OnListInventory`, after `m_vendorGuid = vendorGuid;` (line ~139, the success path after the `listCount == 0` error block):

```cpp
		m_vendorGuid = vendorGuid;
		UnitGossipVoice::Get().OnNpcDialogOpened(npc_dialog_source::Vendor, m_vendorGuid);
```

(Leave the `listCount == 0` error path untouched — no dialog opened there.)

- [ ] **Step 4: TrainerClient hooks**

In `src/mmo_client/systems/trainer_client.cpp`, add `#include "unit_gossip_voice.h"` at the top. In `CloseTrainer` (line 34):

```cpp
	void TrainerClient::CloseTrainer()
	{
		if (!HasTrainer())
		{
			return;
		}

		UnitGossipVoice::Get().OnNpcDialogClosed(npc_dialog_source::Trainer, m_trainerGuid);

		m_trainerGuid = 0;
		m_trainerSpells.clear();

		FrameManager::Get().TriggerLuaEvent("TRAINER_CLOSED");
	}
```

In `OnTrainerList`, after `m_trainerGuid = trainerGuid;` (line ~118):

```cpp
		m_trainerGuid = trainerGuid;
		UnitGossipVoice::Get().OnNpcDialogOpened(npc_dialog_source::Trainer, m_trainerGuid);
```

- [ ] **Step 5: BankClient hooks**

In `src/mmo_client/systems/bank_client.cpp`, add `#include "unit_gossip_voice.h"` at the top. In `CloseBank` (line 59):

```cpp
	void BankClient::CloseBank()
	{
		if (m_bankerGuid == 0)
		{
			return;
		}

		UnitGossipVoice::Get().OnNpcDialogClosed(npc_dialog_source::Bank, m_bankerGuid);

		m_bankerGuid = 0;

		FrameManager::Get().TriggerLuaEvent("BANK_CLOSED");
	}
```

In `OnShowBank`, after `m_bankerGuid = bankerGuid;` (line ~103):

```cpp
		m_bankerGuid = bankerGuid;
		UnitGossipVoice::Get().OnNpcDialogOpened(npc_dialog_source::Bank, m_bankerGuid);
```

- [ ] **Step 6: Build**

Run:
```powershell
cmake --build build -t mmo_client --config Debug
```
Expected: build succeeds.

- [ ] **Step 7: Commit**

```powershell
git add src/mmo_client/systems/quest_client.cpp src/mmo_client/systems/vendor_client.cpp src/mmo_client/systems/trainer_client.cpp src/mmo_client/systems/bank_client.cpp
git commit -m "Notify goodbye voice system from npc dialog open/close paths"
```

---

### Task 4: Lua-only close paths (QuestFrame + TrainerFrame)

**Files:**
- Modify: `src/mmo_client/systems/quest_client.cpp` (`RegisterScriptFunctions`, def_lambda list around line 171)
- Modify: `data/client/Interface/GameUI/QuestFrame.xml` (`<Scripts>` block, lines 686-695)
- Modify: `data/client/Interface/GameUI/TrainerFrame.xml` (`<Scripts>` block, lines 441-452)

**Interfaces:**
- Consumes: idempotent `QuestClient::CloseQuest()` from Task 3; existing Lua binding `CloseTrainer()` (registered in `game_script.cpp:1607`).
- Produces: Lua global `NotifyQuestDialogClosed()`.

Background: closing a frame with the close button or Escape only calls `HideUIPanel` in Lua — C++ is never told. `VendorFrame.xml` and `BankFrame.xml` already solve this with an `OnHide` → `CloseVendor()`/`CloseBank()` handler; QuestFrame and TrainerFrame lack it.

- [ ] **Step 1: Register the Lua binding**

In `src/mmo_client/systems/quest_client.cpp`, in `RegisterScriptFunctions`, directly after the `GossipAction` line (line ~171: `luabind::def_lambda("GossipAction", [this](int32 index) { return ExecuteGossipAction(index); }),`), add:

```cpp
			luabind::def_lambda("NotifyQuestDialogClosed", [this]() { CloseQuest(); }),
```

- [ ] **Step 2: QuestFrame OnHide handler**

In `data/client/Interface/GameUI/QuestFrame.xml`, the frame's `<Scripts>` block currently ends with the `<OnShow>` handler (lines 690-694). Add an `<OnHide>` block after `</OnShow>`, before `</Scripts>`:

```xml
			<OnHide>
				return function(this)
					NotifyQuestDialogClosed();
				end
			</OnHide>
```

Re-entrancy is safe: manual close → `OnHide` → `CloseQuest` (guid set → goodbye armed, fires `QUEST_FINISHED`) → Lua hides the already-hidden frame; any second `OnHide` hits the guid==0 early-out. Server-driven closes clear the guid before Lua hides the frame, so their `OnHide` is a no-op too.

- [ ] **Step 3: TrainerFrame OnHide handler**

In `data/client/Interface/GameUI/TrainerFrame.xml`, the frame's `<Scripts>` block (lines 441-452) currently has `<OnLoad>` and `<OnShow>`. Add after `</OnShow>`, before `</Scripts>`:

```xml
			<OnHide>
				return function(this)
					CloseTrainer();
				end
			</OnHide>
```

(`CloseTrainer` is already idempotent via its `HasTrainer()` early-out, mirroring VendorFrame's existing `OnHide` → `CloseVendor()` pattern.)

- [ ] **Step 4: Build**

Run:
```powershell
cmake --build build -t mmo_client --config Debug
```
Expected: build succeeds (XML/Lua are data files; this catches the C++ binding change).

- [ ] **Step 5: Commit**

`data/client` is a git submodule — the XML files must be committed inside it, the .cpp in the main repo:

```powershell
git -C data/client add Interface/GameUI/QuestFrame.xml Interface/GameUI/TrainerFrame.xml
git -C data/client commit -m "Notify goodbye voice system when quest/trainer frames hide"
git add src/mmo_client/systems/quest_client.cpp
git commit -m "Route Lua-side quest/trainer frame closes into the goodbye voice system"
```

---

### Task 5: Full build + manual verification handoff

**Files:** none (verification only).

- [ ] **Step 1: Build everything that was touched**

Run:
```powershell
cmake --build build -t mmo_client mmo_edit --config Debug
```
Expected: both targets build without errors.

Note: `data/client` is a git submodule — the XML changes there are committed inside the submodule; do not commit a submodule pointer bump in the main repo unless the user asks.

- [ ] **Step 2: Hand the manual test checklist to the user**

The feature is silent until a SoundEntry (3D, VOICE category) is assigned as "Goodbye Sound" on a model display entry in the editor's Model Editor → Audio section. With one assigned, verify in-game (DLOG lines appear in Client.log — remember it is write-buffered; use `Get-Content -Raw` or the in-game console):

1. Open an NPC's gossip dialog, close it with the close button → goodbye plays after ~0.5 s.
2. Open gossip, press Escape → same.
3. Open gossip → click a quest (detail panel) → back/accept → no goodbye during panel switches; goodbye after the frame really closes (accepting a quest closes it).
4. Open gossip → "browse goods" (vendor action) → NO goodbye when the vendor window replaces the gossip window; close the vendor window → goodbye plays. Same for trainer and banker actions.
5. Turn in a quest (offer-reward flow) → goodbye plays after the reward toast closes the dialog.
6. Quickly open and close the dialog while the hello line is still playing → goodbye is skipped (DLOG says so).
7. Walk away from an open vendor window until it auto-closes → goodbye plays, attenuated by distance.
8. Close NPC A's dialog by immediately talking to NPC B → A's goodbye still plays while B greets.
