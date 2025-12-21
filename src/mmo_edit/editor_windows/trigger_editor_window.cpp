// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "trigger_editor_window.h"
#include "game_server/spells/spell_cast.h"
#include "graphics/texture_mgr.h"
#include "proto_data/trigger_helper.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <iomanip>
#include <sstream>

#include "editor_imgui_helpers.h"


namespace ImGui
{
	bool HyperLink(const char* label, const bool underlineWhenHoveredOnly = false)
	{
		const ImU32 linkColor = ImGui::ColorConvertFloat4ToU32({ 0.2f, 0.3f, 0.8f, 1 });
		const ImU32 linkHoverColor = ImGui::ColorConvertFloat4ToU32({ 0.4f, 0.6f, 0.8f, 1 });
		const ImU32 linkFocusColor = ImGui::ColorConvertFloat4ToU32({ 0.6f, 0.4f, 0.8f, 1 });

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
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));

			// Display a combo box to select the event type.
			int currentEventType = event.type();
			ImGui::SetNextItemWidth(250);
			if (ImGui::Combo("##EventType", &currentEventType, s_eventTypeNames, IM_ARRAYSIZE(s_eventTypeNames)))
			{
				event.set_type(currentEventType);
				// Clear any previously set data when changing the type.
				event.clear_data();
			}
			ImGui::SameLine();
			ImGui::Text("Event Type");

			ImGui::Spacing();

			// Based on the event type, show the appropriate parameter(s).
			switch (currentEventType)
			{
			case trigger_event::OnHealthDroppedBelow:
			{
				// This event requires a health percentage (0-100).
				int healthPercentage = (event.data_size() > 0) ? event.data(0) : 100;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##HealthPercent", &healthPercentage))
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
				ImGui::SameLine();
				ImGui::Text("Health Percentage (%)");
				break;
			}
			case trigger_event::OnQuestAccept:
			{
				int questId = (event.data_size() > 0) ? event.data(0) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##QuestId", &questId))
				{
					if (event.data_size() > 0)
						event.set_data(0, questId);
					else
						event.add_data(questId);
				}
				ImGui::SameLine();
				ImGui::Text("Quest ID");
				break;
			}
			case trigger_event::OnGossipAction:
			{
				// This event requires a Menu ID and Action ID.
				int menuId = (event.data_size() > 0) ? event.data(0) : 0;
				int actionId = (event.data_size() > 1) ? event.data(1) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##MenuId", &menuId))
				{
					if (event.data_size() > 0)
						event.set_data(0, menuId);
					else
						event.add_data(menuId);
				}
				ImGui::SameLine();
				ImGui::Text("Gossip Menu ID");
				
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##ActionId", &actionId))
				{
					if (event.data_size() > 1)
						event.set_data(1, actionId);
					else
						event.add_data(actionId);
				}
				ImGui::SameLine();
				ImGui::Text("Action ID");
				break;
			}
			case trigger_event::OnSpellHit:
			case trigger_event::OnSpellAuraRemoved:
			case trigger_event::OnSpellCast:
			{
				// These events require a Spell ID.
				int spellId = (event.data_size() > 0) ? event.data(0) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##SpellId", &spellId))
				{
					if (event.data_size() > 0)
						event.set_data(0, spellId);
					else
						event.add_data(spellId);
				}
				ImGui::SameLine();
				ImGui::Text("Spell ID");
				break;
			}
			case trigger_event::OnEmote:
			{
				// This event requires an Emote ID.
				int emoteId = (event.data_size() > 0) ? event.data(0) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##EmoteId", &emoteId))
				{
					if (event.data_size() > 0)
						event.set_data(0, emoteId);
					else
						event.add_data(emoteId);
				}
				ImGui::SameLine();
				ImGui::Text("Emote ID");
				break;
			}
			default:
			{
				// For all other event types, no parameters are required.
				ImGui::TextDisabled("No additional parameters required for this event type.");
				break;
			}
			}

			ImGui::Spacing();
			ImGui::Separator();

			// Provide a button to remove this event.
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
			if (ImGui::Button("Remove Event", ImVec2(-1, 0)))
			{
				// Removing an event invalidates the indices, so break out after removal.
				currentEntry.mutable_newevents()->DeleteSubrange(eventIndex, 1);
			}
			ImGui::PopStyleColor();

			ImGui::PopStyleVar();
		}

		// Draws a single trigger action in the editor.
		// Assumes that 'action' is a mutable reference from your proto TriggerAction message.
		void DrawTriggerAction(proto::TriggerAction& action, int actionIndex, proto::TriggerEntry& currentEntry)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));

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
			ImGui::SetNextItemWidth(250);
			if (ImGui::Combo("##ActionType", &currentActionType, s_actionTypeNames, IM_ARRAYSIZE(s_actionTypeNames)))
			{
				action.set_action(currentActionType);
				// Optionally, clear existing data/texts if the type changes.
				action.clear_data();
				action.clear_texts();
			}
			ImGui::SameLine();
			ImGui::Text("Action Type");

			ImGui::Spacing();

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
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				// Get the current target (as stored in the message).
				int currentTarget = action.target();
				ImGui::SetNextItemWidth(200);
				if (ImGui::Combo("##TargetType", &currentTarget, s_actionTargetStrings, IM_ARRAYSIZE(s_actionTargetStrings)))
				{
					action.set_target(currentTarget);
					// If the selected target is not one that requires a name, clear the target name.
					if (currentTarget != trigger_action_target::NamedWorldObject &&
						currentTarget != trigger_action_target::NamedCreature)
					{
						action.clear_targetname();
					}
				}
				ImGui::SameLine();
				ImGui::Text("Target Type");

				// For targets that require a name (e.g. named world object or named creature), show an input field.
				if (currentTarget == trigger_action_target::NamedWorldObject ||
					currentTarget == trigger_action_target::NamedCreature)
				{
					ImGui::Spacing();
					std::string targetName = action.targetname();
					ImGui::SetNextItemWidth(300);
					if (ImGui::InputText("##TargetName", &targetName))
					{
						action.set_targetname(targetName);
					}
					ImGui::SameLine();
					ImGui::Text("Target Name");
				}

				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();
			}

			// Display the data fields based on the action type.
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			switch (currentActionType)
			{
			case trigger_actions::Trigger:
			{
				// Data: <TRIGGER-ID>
				int triggerId = (action.data_size() > 0) ? action.data(0) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##TriggerId", &triggerId))
				{
					if (action.data_size() > 0)
						action.set_data(0, triggerId);
					else
						action.add_data(triggerId);
				}
				ImGui::SameLine();
				ImGui::Text("Trigger ID");
				break;
			}
			case trigger_actions::Say:
			case trigger_actions::Yell:
			{
				// Data: <SOUND-ID>, <LANGUAGE>; Texts: <TEXT>
				int soundId = (action.data_size() > 0) ? action.data(0) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##SoundId", &soundId))
				{
					if (action.data_size() > 0)
						action.set_data(0, soundId);
					else
						action.add_data(soundId);
				}
				ImGui::SameLine();
				ImGui::Text("Sound ID");

				int language = (action.data_size() > 1) ? action.data(1) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##Language", &language))
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
				ImGui::SameLine();
				ImGui::Text("Language");

				std::string text = (action.texts_size() > 0) ? action.texts(0) : "";
				ImGui::SetNextItemWidth(-1);
				if (ImGui::InputText("##Text", &text))
				{
					if (action.texts_size() > 0)
						action.set_texts(0, text);
					else
						action.add_texts(text);
				}
				ImGui::SameLine();
				ImGui::Text("Text");
				break;
			}
			case trigger_actions::SetWorldObjectState:
			{
				// Data: <NEW-STATE>
				int newState = (action.data_size() > 0) ? action.data(0) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##NewState", &newState))
				{
					if (action.data_size() > 0)
						action.set_data(0, newState);
					else
						action.add_data(newState);
				}
				ImGui::SameLine();
				ImGui::Text("New State");
				break;
			}
			case trigger_actions::SetSpawnState:
			case trigger_actions::SetRespawnState:
			{
				// Data: <0/1>
				int state = (action.data_size() > 0) ? action.data(0) : 0;
				bool enabled = (state != 0);
				if (ImGui::Checkbox("Enabled##SpawnState", &enabled))
				{
					state = enabled ? 1 : 0;
					if (action.data_size() > 0)
						action.set_data(0, state);
					else
						action.add_data(state);
				}
				ImGui::SameLine();
				ImGui::Text("Spawn/Respawn Enabled");
				break;
			}
			case trigger_actions::CastSpell:
			{
				// Data: <SPELL-ID>, <TARGET>
				int spellId = (action.data_size() > 0) ? action.data(0) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##SpellId", &spellId))
				{
					if (action.data_size() > 0)
						action.set_data(0, spellId);
					else
						action.add_data(spellId);
				}
				ImGui::SameLine();
				ImGui::Text("Spell ID");

				int target = (action.data_size() > 1) ? action.data(1) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##CastTarget", &target))
				{
					if (action.data_size() > 1)
						action.set_data(1, target);
					else
						action.add_data(target);
				}
				ImGui::SameLine();
				ImGui::Text("Cast Target");
				break;
			}
			case trigger_actions::Delay:
			{
				// Data: <DELAY-TIME-MS>
				int delayMs = (action.data_size() > 0) ? action.data(0) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##DelayMs", &delayMs))
				{
					if (action.data_size() > 0)
						action.set_data(0, delayMs);
					else
						action.add_data(delayMs);
				}
				ImGui::SameLine();
				ImGui::Text("Delay (milliseconds)");
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
				ImGui::SetNextItemWidth(300);
				if (ImGui::InputFloat3("##Position", pos))
				{
					while (action.data_size() < 3)
						action.add_data(0);
					action.set_data(0, static_cast<int>(pos[0]));
					action.set_data(1, static_cast<int>(pos[1]));
					action.set_data(2, static_cast<int>(pos[2]));
				}
				ImGui::SameLine();
				ImGui::Text("Position (X, Y, Z)");
				break;
			}
			case trigger_actions::SetCombatMovement:
			{
				// Data: <0/1>
				int cm = (action.data_size() > 0) ? action.data(0) : 0;
				bool enabled = (cm != 0);
				if (ImGui::Checkbox("Enable Combat Movement##CombatMovement", &enabled))
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
				ImGui::TextDisabled("No additional parameters required for this action.");
				break;
			}
			case trigger_actions::SetStandState:
			{
				// Data: <STAND-STATE>
				int standState = (action.data_size() > 0) ? action.data(0) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##StandState", &standState))
				{
					if (action.data_size() > 0)
						action.set_data(0, standState);
					else
						action.add_data(standState);
				}
				ImGui::SameLine();
				ImGui::Text("Stand State");
				break;
			}
			case trigger_actions::SetVirtualEquipmentSlot:
			{
				// Data: <SLOT:0-2>, <ITEM-ENTRY>
				int slot = (action.data_size() > 0) ? action.data(0) : 0;
				ImGui::SetNextItemWidth(100);
				if (ImGui::InputInt("##EquipSlot", &slot))
				{
					if (action.data_size() > 0)
						action.set_data(0, slot);
					else
						action.add_data(slot);
				}
				ImGui::SameLine();
				ImGui::Text("Slot (0-2)");

				int itemEntry = (action.data_size() > 1) ? action.data(1) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##ItemEntry", &itemEntry))
				{
					if (action.data_size() > 1)
						action.set_data(1, itemEntry);
					else {
						if (action.data_size() == 0)
							action.add_data(0);
						action.add_data(itemEntry);
					}
				}
				ImGui::SameLine();
				ImGui::Text("Item Entry");
				break;
			}
			case trigger_actions::SetPhase:
			{
				// Data: <PHASE>
				int phase = (action.data_size() > 0) ? action.data(0) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##Phase", &phase))
				{
					if (action.data_size() > 0)
						action.set_data(0, phase);
					else
						action.add_data(phase);
				}
				ImGui::SameLine();
				ImGui::Text("Phase");
				break;
			}
			case trigger_actions::SetSpellCooldown:
			{
				// Data: <SPELL-ID>, <TIME-MS>
				int spellId = (action.data_size() > 0) ? action.data(0) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##CooldownSpellId", &spellId))
				{
					if (action.data_size() > 0)
						action.set_data(0, spellId);
					else
						action.add_data(spellId);
				}
				ImGui::SameLine();
				ImGui::Text("Spell ID");

				int timeMs = (action.data_size() > 1) ? action.data(1) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##CooldownTimeMs", &timeMs))
				{
					if (action.data_size() > 1)
						action.set_data(1, timeMs);
					else {
						if (action.data_size() == 0)
							action.add_data(0);
						action.add_data(timeMs);
					}
				}
				ImGui::SameLine();
				ImGui::Text("Cooldown Duration (ms)");
				break;
			}
			case trigger_actions::QuestKillCredit:
			{
				// Data: <CREATURE-ENTRY-ID>
				int creatureEntryId = (action.data_size() > 0) ? action.data(0) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##CreatureEntryId", &creatureEntryId))
				{
					if (action.data_size() > 0)
						action.set_data(0, creatureEntryId);
					else
						action.add_data(creatureEntryId);
				}
				ImGui::SameLine();
				ImGui::Text("Creature Entry ID");
				break;
			}
			case trigger_actions::QuestEventOrExploration:
			{
				// Data: <QUEST-ID>
				int questId = (action.data_size() > 0) ? action.data(0) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##QuestEventId", &questId))
				{
					if (action.data_size() > 0)
						action.set_data(0, questId);
					else
						action.add_data(questId);
				}
				ImGui::SameLine();
				ImGui::Text("Quest ID");
				break;
			}
			case trigger_actions::SetVariable:
			{
				// Data: <VARIABLE-ID>, [<NUMERIC-VALUE>]; Texts: [<STRING_VALUE>]
				int variableId = (action.data_size() > 0) ? action.data(0) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##VarId", &variableId))
				{
					if (action.data_size() > 0)
						action.set_data(0, variableId);
					else
						action.add_data(variableId);
				}
				ImGui::SameLine();
				ImGui::Text("Variable ID");

				int numericValue = (action.data_size() > 1) ? action.data(1) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##NumVal", &numericValue))
				{
					if (action.data_size() > 1)
						action.set_data(1, numericValue);
					else {
						if (action.data_size() == 0)
							action.add_data(0);
						action.add_data(numericValue);
					}
				}
				ImGui::SameLine();
				ImGui::Text("Numeric Value");

				std::string strValue = (action.texts_size() > 0) ? action.texts(0) : "";
				ImGui::SetNextItemWidth(300);
				if (ImGui::InputText("##StrVal", &strValue))
				{
					if (action.texts_size() > 0)
						action.set_texts(0, strValue);
					else
						action.add_texts(strValue);
				}
				ImGui::SameLine();
				ImGui::Text("String Value");
				break;
			}
			case trigger_actions::Dismount:
			{
				// Data: NONE
				ImGui::TextDisabled("No additional parameters required for this action.");
				break;
			}
			case trigger_actions::SetMount:
			{
				// Data: <MOUNT-ID>
				int mountId = (action.data_size() > 0) ? action.data(0) : 0;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##MountId", &mountId))
				{
					if (action.data_size() > 0)
						action.set_data(0, mountId);
					else
						action.add_data(mountId);
				}
				ImGui::SameLine();
				ImGui::Text("Mount ID");
				break;
			}
			case trigger_actions::Despawn:
			{
				// Data: NONE
				ImGui::TextDisabled("No additional parameters required for this action.");
				break;
			}
			default:
			{
				ImGui::TextDisabled("Unknown action type.");
				break;
			}
			}

			ImGui::Spacing();
			ImGui::Separator();

			// Provide a remove button for this action.
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
			if (ImGui::Button("Remove Action", ImVec2(-1, 0)))
			{
				// Removing an element from a repeated field invalidates indices so break out after removal.
				currentEntry.mutable_actions()->DeleteSubrange(actionIndex, 1);
			}
			ImGui::PopStyleColor();

			ImGui::PopStyleVar();
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
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Trigger Identification");

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
			ImGui::InputText("##TriggerName", currentEntry.mutable_name());
			ImGui::SameLine();
			ImGui::Text("Trigger Name");
			ImGui::SameLine();
			DrawHelpMarker("Display name of the trigger");

			ImGui::BeginDisabled(true);
			String idString = std::to_string(currentEntry.id());
			ImGui::SetNextItemWidth(150);
			ImGui::InputText("##TriggerId", &idString);
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::Text("Trigger ID");
			ImGui::SameLine();
			DrawHelpMarker("Unique identifier (auto-generated)");

			ImGui::Spacing();
			DrawSectionHeader("Probability Settings");

			uint32 probability = currentEntry.probability();
			ImGui::SetNextItemWidth(150);
			if (ImGui::InputScalar("##Probability", ImGuiDataType_U32, &probability, nullptr, nullptr))
			{
				currentEntry.set_probability(probability);
			}
			ImGui::SameLine();
			ImGui::Text("Probability (%)");
			ImGui::SameLine();
			DrawHelpMarker("Chance (0-100) this trigger will fire when conditions are met");

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("Flags"))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Trigger Behavior Flags");

			CHECKBOX_FLAG_PROP(flags, "Cancel On Owner Death", trigger_flags::AbortOnOwnerDeath);
			ImGui::SameLine();
			DrawHelpMarker("Disable trigger if the owner dies");

			CHECKBOX_FLAG_PROP(flags, "Only In Combat", trigger_flags::OnlyInCombat);
			ImGui::SameLine();
			DrawHelpMarker("Trigger only fires during combat");

			CHECKBOX_FLAG_PROP(flags, "Only One Instance", trigger_flags::OnlyOneInstance);
			ImGui::SameLine();
			DrawHelpMarker("Only one instance of this trigger can run simultaneously");

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Extend the Trigger Events section to allow editing event data
		if (ImGui::CollapsingHeader("Trigger Events"))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Event Management");

			// List current events with editable data fields
			if (currentEntry.newevents_size() == 0)
			{
				ImGui::TextDisabled("No events defined. Click 'Add Event' to create one.");
			}
			else
			{
				for (int i = 0; i < currentEntry.newevents_size(); ++i)
				{
					auto* event = currentEntry.mutable_newevents(i);
					ImGui::PushID(i);

					ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.4f, 0.4f, 0.5f, 0.5f));
					DrawTriggerEvent(*event, i, currentEntry);
					ImGui::PopStyleColor();

					ImGui::Spacing();
					ImGui::PopID();
				}
			}

			ImGui::Spacing();

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
			if (ImGui::Button("+ Add Event", ImVec2(-1, 0)))
			{
				ImGui::OpenPopup("Event Details");
			}
			ImGui::PopStyleColor();

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		// Extend the Trigger Actions section
		if (ImGui::CollapsingHeader("Trigger Actions"))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Action Management");

			ImGui::BeginChild("triggerActionsChild", ImVec2(0, 400), true);
			ImGui::Columns(2, nullptr, true);
			static bool actionListWidthSet = false;
			if (!actionListWidthSet)
			{
				ImGui::SetColumnWidth(ImGui::GetColumnIndex(), 280.0f);
				actionListWidthSet = true;
			}

			static int currentAction = -1;

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
			if (ImGui::Button("+ Add Action", ImVec2(-1, 0)))
			{
				auto* newAction = currentEntry.add_actions();
				newAction->set_action(0);
				newAction->set_target(0);
				newAction->set_targetname("");
				currentAction = currentEntry.actions_size() - 1;
			}
			ImGui::PopStyleColor();

			ImGui::BeginDisabled(currentAction == -1 || currentAction >= currentEntry.actions_size());
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
			if (ImGui::Button("Remove Action", ImVec2(-1, 0)))
			{
				currentEntry.mutable_actions()->erase(currentEntry.mutable_actions()->begin() + currentAction);
				currentAction = -1;
			}
			ImGui::PopStyleColor();
			ImGui::EndDisabled();

			ImGui::Spacing();
			ImGui::TextDisabled("%d trigger actions", currentEntry.actions_size());

			ImGui::BeginChild("actionListScrollable", ImVec2(-1, 0));

			// List of actions
			for (int idx = 0; idx < currentEntry.actions_size(); ++idx)
			{
				const auto& action = currentEntry.actions(idx);
				const bool isSelected = (currentAction == idx);

				ImGui::PushID(idx);

				// Get action type name
				static const char* s_actionTypeNames[] = {
					"Trigger", "Say", "Yell", "SetWorldObjectState", "SetSpawnState",
					"SetRespawnState", "CastSpell", "Delay", "MoveTo", "SetCombatMovement",
					"StopAutoAttack", "CancelCast", "SetStandState", "SetVirtualEquipmentSlot",
					"SetPhase", "SetSpellCooldown", "QuestKillCredit", "QuestEventOrExploration",
					"SetVariable", "Dismount", "SetMount", "Despawn"
				};

				const char* actionTypeName = (action.action() >= 0 && action.action() < static_cast<int>(std::size(s_actionTypeNames)))
					? s_actionTypeNames[action.action()]
					: "Unknown";

				std::ostringstream stream;
				stream << "#" << std::setw(3) << std::setfill('0') << idx << " - " << actionTypeName;

				if (ImGui::Selectable(stream.str().c_str(), isSelected))
				{
					currentAction = idx;
				}

				ImGui::PopID();
			}

			ImGui::EndChild();

			ImGui::NextColumn();

			// Show editable details of selected action
			ImGui::BeginChild("actionDetails", ImVec2(-1, -1));
			if (currentAction != -1 && currentAction < currentEntry.actions_size())
			{
				auto* action = currentEntry.mutable_actions(currentAction);
				DrawSectionHeader("Action Properties");

				ImGui::PushID(currentAction);
				DrawTriggerAction(*action, currentAction, currentEntry);
				ImGui::PopID();
			}
			else
			{
				ImGui::TextDisabled("Select an action to edit its properties");
			}
			ImGui::EndChild();

			ImGui::EndChild();

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		// Add Event popup
		if (ImGui::BeginPopupModal("Event Details", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			static int selectedEventType = 0;

			ImGui::Text("Select Event Type:");
			ImGui::Spacing();

			ImGui::SetNextItemWidth(300);
			ImGui::Combo("##EventType", &selectedEventType, s_eventTypeNames, std::size(s_eventTypeNames));

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
			if (ImGui::Button("Add", ImVec2(120, 0)))
			{
				auto& event = *currentEntry.add_newevents();
				event.set_type(trigger_event::Type(selectedEventType));
				ImGui::CloseCurrentPopup();
			}
			ImGui::PopStyleColor();

			ImGui::SameLine();

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 0.8f));
			if (ImGui::Button("Cancel", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::PopStyleColor();

			ImGui::EndPopup();
		}
	}
}
