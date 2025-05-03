// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "trigger_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "game/aura.h"
#include "game/spell.h"
#include "game/zone.h"
#include "game_server/spells/spell_cast.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"
#include "proto_data/trigger_helper.h"

namespace ImGui
{
	bool HyperLink(const char* label, bool underlineWhenHoveredOnly = false)
	{
		const ImU32 linkColor = ImGui::ColorConvertFloat4ToU32({ 0.2, 0.3, 0.8, 1 });
		const ImU32 linkHoverColor = ImGui::ColorConvertFloat4ToU32({ 0.4, 0.6, 0.8, 1 });
		const ImU32 linkFocusColor = ImGui::ColorConvertFloat4ToU32({ 0.6, 0.4, 0.8, 1 });

		const ImGuiID id = ImGui::GetID(label);

		ImGuiWindow* const window = ImGui::GetCurrentWindow();
		ImDrawList* const draw = ImGui::GetWindowDrawList();

		const ImVec2 pos(window->DC.CursorPos.x, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
		const ImVec2 size = ImGui::CalcTextSize(label);
		ImRect bb(pos, { pos.x + size.x, pos.y + size.y });

		ImGui::ItemSize(bb, 0.0f);
		if (!ImGui::ItemAdd(bb, id))
			return false;

		bool isHovered = false;
		const bool isClicked = ImGui::ButtonBehavior(bb, id, &isHovered, nullptr);
		const bool isFocused = ImGui::IsItemFocused();

		const ImU32 color = isHovered ? linkHoverColor : isFocused ? linkFocusColor : linkColor;

		draw->AddText(bb.Min, color, label);

		if (isFocused)
			draw->AddRect(bb.Min, bb.Max, color);
		else if (!underlineWhenHoveredOnly || isHovered)
			draw->AddLine({ bb.Min.x, bb.Max.y }, bb.Max, color);

		return isClicked;
	}
}

namespace mmo
{
	static const char* s_eventTypeNames[] = {
		"Object - On Spawn",
		"Object - On Despawn",
		"Unit - On Aggro",
		"Unit - On Killed",
		"Unit - On Kill",
		"Unit - On Damaged",
		"Unit - On Healed",
		"Unit - On Auto Attack",
		"Unit - On Reset",
		"Unit - On Reached Home",
		"Object - On Interaction",
		"Unit - On Health Dropped Below Value",
		"Unit - On Reached Triggered Movement Target",
		"Object - On Spell Hit",
		"Unit - On Spell Aura Removed",
		"Unit - On Emote",
		"Unit - On Spell Cast",
		"Object - On Gossip Menu Action",
		"Object - On Quest Accepted"
	};

	static_assert(std::size(s_eventTypeNames) == trigger_event::Count_, "s_eventTypeNames size mismatch");

	namespace
	{
		int32 GetTriggerEventData(const proto::TriggerEvent& e, uint32 i)
		{
			if (static_cast<int>(i) >= e.data_size())
				return 0;

			return e.data(i);
		}

		void DrawTriggerEvents(proto::TriggerEntry& currentEntry)
		{
			// Table with 2 columns: "Handle" and "Name"
			const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders
				| ImGuiTableFlags_RowBg
				| ImGuiTableFlags_ScrollY
				| ImGuiTableFlags_Resizable;
			ImGui::BeginChild("TriggerEventsChild", ImVec2(0, 150), true);
			if (ImGui::BeginTable("TriggerEventsTable", 1, tableFlags))
			{
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn("Trigger", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (const auto& event : currentEntry.newevents())
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);

					switch (event.type())
					{
					case trigger_event::OnAggro:
						ImGui::Text("Owning unit enters combat");
						break;
					case trigger_event::OnAttackSwing:
						ImGui::Text("Owning unit executes auto attack swing");
						break;
					case trigger_event::OnDamaged:
						ImGui::Text("Owning unit received damage");
						break;
					case trigger_event::OnDespawn:
						ImGui::Text("Owner despawned");
						break;
					case trigger_event::OnHealed:
						ImGui::Text("Owning unit received heal");
						break;
					case trigger_event::OnKill:
						ImGui::Text("Owning unit killed someone");
						break;
					case trigger_event::OnKilled:
						ImGui::Text("Owning unit was killed");
						break;
					case trigger_event::OnSpawn:
						ImGui::Text("Owner spawned");
						break;
					case trigger_event::OnReset:
						ImGui::Text("Owning unit resets");
						break;
					case trigger_event::OnReachedHome:
						ImGui::Text("Owning unit reached home after reset");
						break;
					case trigger_event::OnInteraction:
						ImGui::Text("Player interacted with owner");
						break;
					case trigger_event::OnHealthDroppedBelow:
						ImGui::Text("Owning units health dropped below %d", GetTriggerEventData(event, 0));
						break;
					case trigger_event::OnReachedTriggeredTarget:
						ImGui::Text("Owning unit reached triggered movement target");
						break;
					case trigger_event::OnSpellHit:
						ImGui::Text("Owning unit was hit by spell %d", GetTriggerEventData(event, 0));
						break;
					case trigger_event::OnSpellAuraRemoved:
						ImGui::Text("Owning unit lost aura of spell %d", GetTriggerEventData(event, 0));
						break;
					case trigger_event::OnEmote:
						ImGui::Text("Owning unit was targeted by emote %d", GetTriggerEventData(event, 0));
						break;
					case trigger_event::OnSpellCast:
						ImGui::Text("Owning unit successfully casted spell %d", GetTriggerEventData(event, 0));
						break;
					case trigger_event::OnGossipAction:
						ImGui::Text("Player chose gossip menu %d's action %d", GetTriggerEventData(event, 0), GetTriggerEventData(event, 1));
						break;
					case trigger_event::OnQuestAccept:
						ImGui::Text("Player accepted quest %d", GetTriggerEventData(event, 0));
						break;
					}
				}

				ImGui::EndTable();
			}
			ImGui::EndChild();

		}

		// Draws a single trigger event in the editor.
		// Assumes that 'event' is a mutable reference from your proto TriggerEvent message.
		void DrawTriggerEvent(proto::TriggerEvent& event, int eventIndex, proto::TriggerEntry& currentEntry)
		{
			// Display a combo box to select the event type.
			int currentEventType = event.type();
			if (ImGui::Combo("Event Type", &currentEventType, s_eventTypeNames, IM_ARRAYSIZE(s_eventTypeNames)))
			{
				event.set_type(currentEventType);
				// Clear any previously set data when changing the type.
				event.clear_data();
			}

			// Based on the event type, show the appropriate parameter(s).
			switch (currentEventType)
			{
			case trigger_event::OnHealthDroppedBelow:
			{
				// This event requires a health percentage (0-100).
				int healthPercentage = (event.data_size() > 0) ? event.data(0) : 100;
				if (ImGui::InputInt("Health Percentage", &healthPercentage))
				{
					// Clamp the value between 0 and 100.
					if (healthPercentage < 0)
						healthPercentage = 0;
					if (healthPercentage > 100)
						healthPercentage = 100;
					if (event.data_size() > 0)
						event.set_data(0, healthPercentage);
					else
						event.add_data(healthPercentage);
				}
				break;
			}
			case trigger_event::OnQuestAccept:
			{
				int questId = (event.data_size() > 0) ? event.data(0) : 0;
				if (ImGui::InputInt("Quest ID", &questId))
				{
					if (event.data_size() > 0)
						event.set_data(0, questId);
					else
						event.add_data(questId);
				}
				break;
			}
			case trigger_event::OnGossipAction:
			{
				// This event requires an Emote ID.
				int menuId = (event.data_size() > 0) ? event.data(0) : 0;
				int actionId = (event.data_size() > 1) ? event.data(1) : 0;
				if (ImGui::InputInt("Gossiup Menu ID", &menuId))
				{
					if (event.data_size() > 0)
						event.set_data(0, menuId);
					else
						event.add_data(menuId);
				}
				if (ImGui::InputInt("Gossip Menu Action ID", &actionId))
				{
					if (event.data_size() > 1)
						event.set_data(1, actionId);
					else
						event.add_data(actionId);
				}
				break;
			}
			case trigger_event::OnSpellHit:
			case trigger_event::OnSpellAuraRemoved:
			case trigger_event::OnSpellCast:
			{
				// These events require a Spell ID.
				int spellId = (event.data_size() > 0) ? event.data(0) : 0;
				if (ImGui::InputInt("Spell ID", &spellId))
				{
					if (event.data_size() > 0)
						event.set_data(0, spellId);
					else
						event.add_data(spellId);
				}
				break;
			}
			case trigger_event::OnEmote:
			{
				// This event requires an Emote ID.
				int emoteId = (event.data_size() > 0) ? event.data(0) : 0;
				if (ImGui::InputInt("Emote ID", &emoteId))
				{
					if (event.data_size() > 0)
						event.set_data(0, emoteId);
					else
						event.add_data(emoteId);
				}
				break;
			}
			default:
			{
				// For all other event types, no parameters are required.
				ImGui::Text("No parameters.");
				break;
			}
			}

			// Provide a button to remove this event.
			if (ImGui::Button("Remove Event"))
			{
				// Removing an event invalidates the indices, so break out after removal.
				currentEntry.mutable_newevents()->DeleteSubrange(eventIndex, 1);
			}
		}

		// Draws a single trigger action in the editor.
		// Assumes that 'action' is a mutable reference from your proto TriggerAction message.
		void DrawTriggerAction(proto::TriggerAction& action, int actionIndex, proto::TriggerEntry& currentEntry)
		{
			// List of human-readable names for the trigger actions.
			static const char* s_actionTypeNames[] = {
				"Trigger", "Say", "Yell", "SetWorldObjectState", "SetSpawnState",
				"SetRespawnState", "CastSpell", "Delay", "MoveTo", "SetCombatMovement",
				"StopAutoAttack", "CancelCast", "SetStandState", "SetVirtualEquipmentSlot",
				"SetPhase", "SetSpellCooldown", "QuestKillCredit", "QuestEventOrExploration",
				"SetVariable", "Dismount", "SetMount", "Despawn"
			};

			// Select the trigger action type.
			int currentActionType = action.action();
			if (ImGui::Combo("Action Type", &currentActionType, s_actionTypeNames, IM_ARRAYSIZE(s_actionTypeNames)))
			{
				action.set_action(currentActionType);
				// Optionally, clear existing data/texts if the type changes.
				action.clear_data();
				action.clear_texts();
			}

			/*
			ImGui::SameLine();

			// Reordering controls: simple Up/Down arrow buttons
			if (i > 0 && ImGui::ArrowButton("Up", ImGuiDir_Up))
			{
				auto* prevAction = currentEntry.mutable_actions(i - 1);
				std::swap(*prevAction, *action);
			}
			ImGui::SameLine();
			if (i < currentEntry.actions_size() - 1 && ImGui::ArrowButton("Down", ImGuiDir_Down))
			{
				auto* nextAction = currentEntry.mutable_actions(i + 1);
				std::swap(*nextAction, *action);
			}
			*/

			static const char* s_actionTargetStrings[] = {
				"None",
				"Owning Object",
				"Owning Unit Victim",
				"Random unit",
				"Named World Object",
				"Named Creature",
				"Triggering Unit"
			};

			static_assert(std::size(s_actionTargetStrings) == trigger_action_target::Count_, "s_actionTargetStrings size mismatch");

			// For demonstration, we show the target selection for all actions except "Trigger" and "Delay".
			if (currentActionType != trigger_actions::Trigger && currentActionType != trigger_actions::Delay)
			{
				// Get the current target (as stored in the message).
				int currentTarget = action.target();
				if (ImGui::Combo("Target Type", &currentTarget, s_actionTargetStrings, IM_ARRAYSIZE(s_actionTargetStrings)))
				{
					action.set_target(currentTarget);
					// If the selected target is not one that requires a name, clear the target name.
					if (currentTarget != trigger_action_target::NamedWorldObject &&
						currentTarget != trigger_action_target::NamedCreature)
					{
						action.clear_targetname();
					}
				}

				// For targets that require a name (e.g. named world object or named creature), show an input field.
				if (currentTarget == trigger_action_target::NamedWorldObject ||
					currentTarget == trigger_action_target::NamedCreature)
				{
					std::string targetName = action.targetname();
					if (ImGui::InputText("Target Name", &targetName))
					{
						action.set_targetname(targetName);
					}
				}
			}
			// --- End Target Selection Section ---


			// Display the data fields based on the action type.
			switch (currentActionType)
			{
			case trigger_actions::Trigger:
			{
				// Data: <TRIGGER-ID>
				int triggerId = (action.data_size() > 0) ? action.data(0) : 0;
				if (ImGui::InputInt("Trigger ID", &triggerId))
				{
					if (action.data_size() > 0)
						action.set_data(0, triggerId);
					else
						action.add_data(triggerId);
				}
				break;
			}
			case trigger_actions::Say:
			case trigger_actions::Yell:
			{
				// Data: <SOUND-ID>, <LANGUAGE>; Texts: <TEXT>
				int soundId = (action.data_size() > 0) ? action.data(0) : 0;
				if (ImGui::InputInt("Sound ID", &soundId))
				{
					if (action.data_size() > 0)
						action.set_data(0, soundId);
					else
						action.add_data(soundId);
				}
				int language = (action.data_size() > 1) ? action.data(1) : 0;
				if (ImGui::InputInt("Language", &language))
				{
					if (action.data_size() > 1)
						action.set_data(1, language);
					else {
						// Ensure the first value is present.
						if (action.data_size() == 0)
							action.add_data(0);
						action.add_data(language);
					}
				}
				std::string text = (action.texts_size() > 0) ? action.texts(0) : "";
				if (ImGui::InputText("Text", &text))
				{
					if (action.texts_size() > 0)
						action.set_texts(0, text);
					else
						action.add_texts(text);
				}
				break;
			}
			case trigger_actions::SetWorldObjectState:
			{
				// Data: <NEW-STATE>
				int newState = (action.data_size() > 0) ? action.data(0) : 0;
				if (ImGui::InputInt("New State", &newState))
				{
					if (action.data_size() > 0)
						action.set_data(0, newState);
					else
						action.add_data(newState);
				}
				break;
			}
			case trigger_actions::SetSpawnState:
			case trigger_actions::SetRespawnState:
			{
				// Data: <0/1>
				int state = (action.data_size() > 0) ? action.data(0) : 0;
				bool enabled = (state != 0);
				if (ImGui::Checkbox("Enabled", &enabled))
				{
					state = enabled ? 1 : 0;
					if (action.data_size() > 0)
						action.set_data(0, state);
					else
						action.add_data(state);
				}
				break;
			}
			case trigger_actions::CastSpell:
			{
				// Data: <SPELL-ID>
				int spellId = (action.data_size() > 0) ? action.data(0) : 0;
				if (ImGui::InputInt("Spell ID", &spellId))
				{
					if (action.data_size() > 0)
						action.set_data(0, spellId);
					else
						action.add_data(spellId);
				}
				int target = (action.data_size() > 1) ? action.data(1) : 0;
				if (ImGui::InputInt("Target", &target))
				{
					if (action.data_size() > 1)
						action.set_data(1, target);
					else
						action.add_data(target);
				}
				break;
			}
			case trigger_actions::Delay:
			{
				// Data: <DELAY-TIME-MS>
				int delayMs = (action.data_size() > 0) ? action.data(0) : 0;
				if (ImGui::InputInt("Delay (ms)", &delayMs))
				{
					if (action.data_size() > 0)
						action.set_data(0, delayMs);
					else
						action.add_data(delayMs);
				}
				break;
			}
			case trigger_actions::MoveTo:
			{
				// Data: <X>, <Y>, <Z>
				float pos[3] = { 0.0f, 0.0f, 0.0f };
				if (action.data_size() >= 3)
				{
					pos[0] = static_cast<float>(action.data(0));
					pos[1] = static_cast<float>(action.data(1));
					pos[2] = static_cast<float>(action.data(2));
				}
				if (ImGui::InputFloat3("Position (X, Y, Z)", pos))
				{
					while (action.data_size() < 3)
						action.add_data(0);
					action.set_data(0, static_cast<int>(pos[0]));
					action.set_data(1, static_cast<int>(pos[1]));
					action.set_data(2, static_cast<int>(pos[2]));
				}
				break;
			}
			case trigger_actions::SetCombatMovement:
			{
				// Data: <0/1>
				int cm = (action.data_size() > 0) ? action.data(0) : 0;
				bool enabled = (cm != 0);
				if (ImGui::Checkbox("Combat Movement", &enabled))
				{
					cm = enabled ? 1 : 0;
					if (action.data_size() > 0)
						action.set_data(0, cm);
					else
						action.add_data(cm);
				}
				break;
			}
			case trigger_actions::StopAutoAttack:
			case trigger_actions::CancelCast:
			{
				// Data: NONE
				ImGui::Text("No parameters.");
				break;
			}
			case trigger_actions::SetStandState:
			{
				// Data: <STAND-STATE>
				int standState = (action.data_size() > 0) ? action.data(0) : 0;
				if (ImGui::InputInt("Stand State", &standState))
				{
					if (action.data_size() > 0)
						action.set_data(0, standState);
					else
						action.add_data(standState);
				}
				break;
			}
			case trigger_actions::SetVirtualEquipmentSlot:
			{
				// Data: <SLOT:0-2>, <ITEM-ENTRY>
				int slot = (action.data_size() > 0) ? action.data(0) : 0;
				if (ImGui::InputInt("Slot (0-2)", &slot))
				{
					if (action.data_size() > 0)
						action.set_data(0, slot);
					else
						action.add_data(slot);
				}
				int itemEntry = (action.data_size() > 1) ? action.data(1) : 0;
				if (ImGui::InputInt("Item Entry", &itemEntry))
				{
					if (action.data_size() > 1)
						action.set_data(1, itemEntry);
					else {
						if (action.data_size() == 0)
							action.add_data(0);
						action.add_data(itemEntry);
					}
				}
				break;
			}
			case trigger_actions::SetPhase:
			{
				// Data: <PHASE>
				int phase = (action.data_size() > 0) ? action.data(0) : 0;
				if (ImGui::InputInt("Phase", &phase))
				{
					if (action.data_size() > 0)
						action.set_data(0, phase);
					else
						action.add_data(phase);
				}
				break;
			}
			case trigger_actions::SetSpellCooldown:
			{
				// Data: <SPELL-ID>, <TIME-MS>
				int spellId = (action.data_size() > 0) ? action.data(0) : 0;
				if (ImGui::InputInt("Spell ID", &spellId))
				{
					if (action.data_size() > 0)
						action.set_data(0, spellId);
					else
						action.add_data(spellId);
				}
				int timeMs = (action.data_size() > 1) ? action.data(1) : 0;
				if (ImGui::InputInt("Time (ms)", &timeMs))
				{
					if (action.data_size() > 1)
						action.set_data(1, timeMs);
					else {
						if (action.data_size() == 0)
							action.add_data(0);
						action.add_data(timeMs);
					}
				}
				break;
			}
			case trigger_actions::QuestKillCredit:
			{
				// Data: <CREATURE-ENTRY-ID>
				int creatureEntryId = (action.data_size() > 0) ? action.data(0) : 0;
				if (ImGui::InputInt("Creature Entry ID", &creatureEntryId))
				{
					if (action.data_size() > 0)
						action.set_data(0, creatureEntryId);
					else
						action.add_data(creatureEntryId);
				}
				break;
			}
			case trigger_actions::QuestEventOrExploration:
			{
				// Data: <QUEST-ID>
				int questId = (action.data_size() > 0) ? action.data(0) : 0;
				if (ImGui::InputInt("Quest ID", &questId))
				{
					if (action.data_size() > 0)
						action.set_data(0, questId);
					else
						action.add_data(questId);
				}
				break;
			}
			case trigger_actions::SetVariable:
			{
				// Data: <VARIABLE-ID>, [<NUMERIC-VALUE>]; Texts: [<STRING_VALUE>]
				int variableId = (action.data_size() > 0) ? action.data(0) : 0;
				if (ImGui::InputInt("Variable ID", &variableId))
				{
					if (action.data_size() > 0)
						action.set_data(0, variableId);
					else
						action.add_data(variableId);
				}
				int numericValue = (action.data_size() > 1) ? action.data(1) : 0;
				if (ImGui::InputInt("Numeric Value", &numericValue))
				{
					if (action.data_size() > 1)
						action.set_data(1, numericValue);
					else {
						if (action.data_size() == 0)
							action.add_data(0);
						action.add_data(numericValue);
					}
				}
				std::string strValue = (action.texts_size() > 0) ? action.texts(0) : "";
				if (ImGui::InputText("String Value", &strValue))
				{
					if (action.texts_size() > 0)
						action.set_texts(0, strValue);
					else
						action.add_texts(strValue);
				}
				break;
			}
			case trigger_actions::Dismount:
			{
				// Data: NONE
				ImGui::Text("No parameters.");
				break;
			}
			case trigger_actions::SetMount:
			{
				// Data: <MOUNT-ID>
				int mountId = (action.data_size() > 0) ? action.data(0) : 0;
				if (ImGui::InputInt("Mount ID", &mountId))
				{
					if (action.data_size() > 0)
						action.set_data(0, mountId);
					else
						action.add_data(mountId);
				}
				break;
			}
			case trigger_actions::Despawn:
			{
				// Data: NONE
				ImGui::Text("No parameters.");
				break;
			}
			default:
				ImGui::Text("Unknown action type.");
				break;
			}

			// Provide a remove button for this action.
			if (ImGui::Button("Remove Action"))
			{
				// Removing an element from a repeated field invalidates indices so break out after removal.
				currentEntry.mutable_actions()->DeleteSubrange(actionIndex, 1);
			}
		}
	}

	TriggerEditorWindow::TriggerEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.triggers, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Triggers";
	}

	void TriggerEditorWindow::DrawDetailsImpl(proto::TriggerEntry& currentEntry)
	{

#define SLIDER_UNSIGNED_PROP(name, label, datasize, min, max) \
	{ \
		const char* format = "%d"; \
		uint##datasize value = currentEntry.name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_U##datasize, &value, nullptr, nullptr)) \
		{ \
			if (value >= min && value <= max) \
				currentEntry.set_##name(value); \
		} \
	}
#define CHECKBOX_BOOL_PROP(name, label) \
	{ \
		bool value = currentEntry.name(); \
		if (ImGui::Checkbox(label, &value)) \
		{ \
			currentEntry.set_##name(value); \
		} \
	}
#define CHECKBOX_FLAG_PROP(property, label, flags) \
	{ \
		bool value = (currentEntry.property() & static_cast<uint32>(flags)) != 0; \
		if (ImGui::Checkbox(label, &value)) \
		{ \
			if (value) \
				currentEntry.set_##property(currentEntry.property() | static_cast<uint32>(flags)); \
			else \
				currentEntry.set_##property(currentEntry.property() & ~static_cast<uint32>(flags)); \
		} \
	}
#define SLIDER_FLOAT_PROP(name, label, min, max) \
	{ \
		const char* format = "%.2f"; \
		float value = currentEntry.name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_Float, &value, nullptr, nullptr)) \
		{ \
			if (value >= min && value <= max) \
				currentEntry.set_##name(value); \
		} \
	}
#define SLIDER_UINT32_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 32, min, max)
#define SLIDER_UINT64_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 64, min, max)

		if (ImGui::CollapsingHeader("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("table", 2, ImGuiTableFlags_None))
			{
				if (ImGui::TableNextColumn())
				{
					ImGui::InputText("Name", currentEntry.mutable_name());
				}

				if (ImGui::TableNextColumn())
				{
					ImGui::BeginDisabled(true);
					String idString = std::to_string(currentEntry.id());
					ImGui::InputText("ID", &idString);
					ImGui::EndDisabled();
				}

				ImGui::EndTable();
			}

			uint32 probability = currentEntry.probability();
			if (ImGui::InputScalar("Probability", ImGuiDataType_U32, &probability, nullptr, nullptr))
			{
				currentEntry.set_probability(probability);
			}
		}

		if (ImGui::CollapsingHeader("Flags"))
		{
			CHECKBOX_FLAG_PROP(flags, "Cancel On Owner Death", trigger_flags::AbortOnOwnerDeath);
			CHECKBOX_FLAG_PROP(flags, "Only In Combat", trigger_flags::OnlyInCombat);
			CHECKBOX_FLAG_PROP(flags, "Only One Instance", trigger_flags::OnlyOneInstance);
		}

		ImGui::Separator();

		// Draw three tree nodes for: Events, Conditions, Actions

		// Extend the Trigger Events section to allow editing event data
		if (ImGui::CollapsingHeader("Trigger Events"))
		{
			// List current events with editable data fields
			for (int i = 0; i < currentEntry.newevents_size(); ++i)
			{
				auto* event = currentEntry.mutable_newevents(i);
				ImGui::PushID(i);
				DrawTriggerEvent(*event, i, currentEntry);
				ImGui::PopID();
			}

			ImGui::Separator();

			if (ImGui::Button("Add Event"))
			{
				ImGui::OpenPopup("Event Details");
			}
		}

		// Extend the Trigger Actions section
		if (ImGui::CollapsingHeader("Trigger Actions"))
		{
			// Loop over each action for editing and reordering
			for (int i = 0; i < currentEntry.actions_size(); ++i)
			{
				auto* action = currentEntry.mutable_actions(i);

				ImGui::PushID(i);
				DrawTriggerAction(*action, i, currentEntry);
				ImGui::PopID();
			}

			ImGui::Separator();

			// Button to add a new action
			if (ImGui::Button("Add Action"))
			{
				auto* newAction = currentEntry.add_actions();
				// Set default values if needed:
				newAction->set_action(0);
				newAction->set_target(0);
				newAction->set_targetname("");
			}
		}

		// Add Event popup
		if (ImGui::BeginPopupModal("Event Details", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			static int selectedEventType = 0;
			ImGui::Combo("Type", &selectedEventType, s_eventTypeNames, std::size(s_eventTypeNames));

			ImGui::Separator();

			if (ImGui::Button("Add"))
			{
				auto& event = *currentEntry.add_newevents();
				event.set_type(trigger_event::Type(selectedEventType));
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}
}
