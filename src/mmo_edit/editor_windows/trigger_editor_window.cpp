// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "trigger_editor_window.h"
#include "game/object_type_id.h"
#include "game_server/spells/spell_cast.h"
#include "graphics/texture_mgr.h"
#include "proto_data/trigger_helper.h"
#include "proto_entry_picker.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include "imgui_node_editor.h"

#include <algorithm>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <vector>

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
		"Object - On Quest Accepted",
		"Instance - On All Players Dead",
		"Instance - On Player Enter Instance",
		"Instance - On Player Leave Instance",
		"Unit/Instance - On Timer (periodic)",
		"Unit - On Summoned Unit Died",
		"Instance - On Encounter State Changed",
		"Player - On Level Up",
		"Player - On Stand State Changed"
	};

	static_assert(std::size(s_eventTypeNames) == trigger_event::Count_, "s_eventTypeNames size mismatch");

	/// Human-readable names for the trigger actions, indexed by trigger_actions::Type. One
	/// definition on purpose: this used to be two local copies that silently drifted apart, so the
	/// action summary listed everything past Emote as "Unknown".
	static const char* s_actionTypeNames[] = {
		"Trigger", "Say", "Yell", "SetWorldObjectState", "SetSpawnState",
		"SetRespawnState", "CastSpell", "Delay", "MoveTo", "SetCombatMovement",
		"StopAutoAttack", "CancelCast", "SetStandState", "SetVirtualEquipmentSlot",
		"SetPhase", "SetSpellCooldown", "QuestKillCredit", "QuestEventOrExploration",
		"SetVariable", "Dismount", "SetMount", "Despawn", "Teleport Player", "Emote",
		"SetEncounterState", "SummonCreature", "Taunt", "ModifyThreat", "ResetThreat",
		"ApplyAura", "RemoveAura", "SetInstanceVariable", "BroadcastMessage",
		"QuestExplorationCredit", "QuestFailQuest", "SetFollowTarget", "ClearFollowTarget",
		"PlaySpellVisual"
	};

	static_assert(std::size(s_actionTypeNames) == trigger_actions::Count_, "s_actionTypeNames size mismatch");

	// === Named values for the integer data fields ===
	//
	// Every table below stands in for an enum the proto stores as a plain int. They are indexed by
	// the stored value, so the order is the enum's order and not a display order; the static_asserts
	// are what keeps them from drifting the way the action-name list once did.

	/// Names for proto::SpellVisualEvent, in enum order.
	static const char* s_spellVisualEventNames[] = {
		"Start Cast", "Cancel Cast", "Casting", "Cast Succeeded", "Impact",
		"Aura Applied", "Aura Removed", "Aura Tick", "Aura Idle"
	};

	static_assert(std::size(s_spellVisualEventNames) == proto::SpellVisualEvent_ARRAYSIZE,
		"s_spellVisualEventNames size mismatch");

	/// Names for trigger_spell_cast_target::Type, in enum order.
	static const char* s_spellCastTargetNames[] = {
		"Caster", "Current Target", "Triggering Unit"
	};

	static_assert(std::size(s_spellCastTargetNames) == trigger_spell_cast_target::Count_,
		"s_spellCastTargetNames size mismatch");

	/// Names for unit_stand_state::Type, in enum order.
	static const char* s_standStateNames[] = {
		"Stand", "Sit", "Sleep", "Dead", "Kneel"
	};

	static_assert(std::size(s_standStateNames) == unit_stand_state::Count_,
		"s_standStateNames size mismatch");

	/// Stand states as an event *filter*. Trigger event data uses zero as a wildcard
	/// (see proto::TriggerEventDataMatches), so slot 0 reads "any state" here and Stand is
	/// not expressible as a filter.
	static const char* s_standStateFilterNames[] = {
		"Any", "Sit", "Sleep", "Dead", "Kneel"
	};

	static_assert(std::size(s_standStateFilterNames) == std::size(s_standStateNames),
		"stand state filter names must cover the same range as the stand state names");

	/// Values of a world object's State field. Doors are the only consumer today, where the field
	/// drives both the visual state and the dynamic line-of-sight collision.
	static const char* s_worldObjectStateNames[] = {
		"Closed / Inactive", "Open / Active"
	};

	/// Virtual equipment slots, matching object_fields::VirtualItem0..2.
	static const char* s_virtualEquipmentSlotNames[] = {
		"Main Hand", "Off Hand", "Ranged"
	};

	/// Names for encounter_state::Type, in enum order. Used by the SetEncounterState action, which
	/// writes a real state and therefore can write NotStarted.
	static const char* s_encounterStateNames[] = {
		"Not Started", "In Progress", "Done", "Fail"
	};

	/// Encounter states as an event *filter*. Trigger event data uses zero as a wildcard
	/// (see proto::TriggerEventDataMatches), so slot 0 reads "any state" here and NotStarted is
	/// not expressible as a filter.
	static const char* s_encounterStateFilterNames[] = {
		"Any", "In Progress", "Done", "Fail"
	};

	static_assert(std::size(s_encounterStateFilterNames) == std::size(s_encounterStateNames),
		"encounter state filter names must cover the same range as the state names");

	/// Names for trigger_action_target::Type, in enum order.
	static const char* s_actionTargetStrings[] = {
		"None",
		"Owning Object",
		"Owning Unit Victim",
		"Random unit",
		"Named World Object",
		"Named Creature",
		"Triggering Unit",
		"Random Player",
		"Nearest Player",
		"Highest Threat (Tank)",
		"All Players"
	};

	static_assert(std::size(s_actionTargetStrings) == trigger_action_target::Count_, "s_actionTargetStrings size mismatch");


	namespace
	{
		// Sets action.data[index] = value, growing the repeated field with zeros as needed.
		void SetActionDataValue(proto::TriggerAction& action, int index, int value)
		{
			while (action.data_size() <= index)
			{
				action.add_data(0);
			}
			action.set_data(index, value);
		}

		// Reads action.data[index], treating a value that was never written as zero. That is what
		// the world server's GetActionData does, so the editor must agree with it.
		int GetActionDataValue(const proto::TriggerAction& action, int index)
		{
			return (action.data_size() > index) ? action.data(index) : 0;
		}

		// Sets event.data[index] = value, growing the repeated field with zeros as needed.
		//
		// Every event case used to inline "if (size > i) set else add", which appends to the wrong
		// slot whenever an earlier index was never written - editing only the max of an OnTimer
		// range wrote the max into the interval.
		void SetEventDataValue(proto::TriggerEvent& event, int index, int value)
		{
			while (event.data_size() <= index)
			{
				event.add_data(0);
			}
			event.set_data(index, value);
		}

		int GetEventDataValue(const proto::TriggerEvent& event, int index)
		{
			return (event.data_size() > index) ? event.data(index) : 0;
		}

		// Draws an InputInt control bound to action.data[index].
		void DrawActionDataInt(proto::TriggerAction& action, int index, const char* id, const char* label,
			float width = 150.0f, const char* tooltip = nullptr)
		{
			int value = GetActionDataValue(action, index);
			ImGui::SetNextItemWidth(width);
			if (ImGui::InputInt(id, &value))
			{
				SetActionDataValue(action, index, value);
			}
			ImGui::SameLine();
			ImGui::Text("%s", label);

			if (tooltip != nullptr)
			{
				ImGui::SameLine();
				DrawHelpMarker(tooltip);
			}
		}

		// Draws a named-value combo bound to action.data[index].
		void DrawActionDataEnum(proto::TriggerAction& action, int index, const char* id, const char* label,
			const char* const* names, int count, const char* tooltip = nullptr)
		{
			int value = GetActionDataValue(action, index);
			if (DrawEnumCombo(id, label, value, names, count, tooltip))
			{
				SetActionDataValue(action, index, value);
			}
		}

		// Draws a by-name entry picker bound to action.data[index].
		template <class Manager>
		void DrawActionDataPicker(proto::TriggerAction& action, int index, const char* id, const char* label,
			const Manager& manager, const char* tooltip = nullptr)
		{
			int value = GetActionDataValue(action, index);
			if (DrawEntryPicker(id, label, value, manager, tooltip))
			{
				SetActionDataValue(action, index, value);
			}
		}

		// Draws an InputInt control bound to event.data[index].
		void DrawEventDataInt(proto::TriggerEvent& event, int index, const char* id, const char* label,
			float width = 150.0f, const char* tooltip = nullptr)
		{
			int value = GetEventDataValue(event, index);
			ImGui::SetNextItemWidth(width);
			if (ImGui::InputInt(id, &value))
			{
				SetEventDataValue(event, index, value);
			}
			ImGui::SameLine();
			ImGui::Text("%s", label);

			if (tooltip != nullptr)
			{
				ImGui::SameLine();
				DrawHelpMarker(tooltip);
			}
		}

		// Draws a named-value combo bound to event.data[index].
		void DrawEventDataEnum(proto::TriggerEvent& event, int index, const char* id, const char* label,
			const char* const* names, int count, const char* tooltip = nullptr)
		{
			int value = GetEventDataValue(event, index);
			if (DrawEnumCombo(id, label, value, names, count, tooltip))
			{
				SetEventDataValue(event, index, value);
			}
		}

		// Draws a by-name entry picker bound to event.data[index].
		template <class Manager>
		void DrawEventDataPicker(proto::TriggerEvent& event, int index, const char* id, const char* label,
			const Manager& manager, const char* tooltip = nullptr)
		{
			int value = GetEventDataValue(event, index);
			if (DrawEntryPicker(id, label, value, manager, tooltip))
			{
				SetEventDataValue(event, index, value);
			}
		}

		/// One-line summary of what raises an event, for the blueprint node body and any other
		/// place a compact description is wanted. This used to be a switch inside a table renderer
		/// that nothing called; the descriptions are the part worth keeping.
		String DescribeEvent(const proto::TriggerEvent& event)
		{
			char buffer[256];

			switch (event.type())
			{
			case trigger_event::OnAggro:
				return "Owning unit enters combat";
			case trigger_event::OnAttackSwing:
				return "Owning unit executes auto attack swing";
			case trigger_event::OnDamaged:
				return "Owning unit received damage";
			case trigger_event::OnDespawn:
				return "Owner despawned";
			case trigger_event::OnHealed:
				return "Owning unit received heal";
			case trigger_event::OnKill:
				return "Owning unit killed someone";
			case trigger_event::OnKilled:
				return "Owning unit was killed";
			case trigger_event::OnSpawn:
				return "Owner spawned";
			case trigger_event::OnReset:
				return "Owning unit resets";
			case trigger_event::OnReachedHome:
				return "Owning unit reached home after reset";
			case trigger_event::OnInteraction:
				return "Player interacted with owner";
			case trigger_event::OnHealthDroppedBelow:
				snprintf(buffer, sizeof(buffer), "Health dropped below %d%%", GetEventDataValue(event, 0));
				return buffer;
			case trigger_event::OnReachedTriggeredTarget:
				return "Reached triggered movement target";
			case trigger_event::OnSpellHit:
				snprintf(buffer, sizeof(buffer), "Hit by spell %d", GetEventDataValue(event, 0));
				return buffer;
			case trigger_event::OnSpellAuraRemoved:
				snprintf(buffer, sizeof(buffer), "Lost aura of spell %d", GetEventDataValue(event, 0));
				return buffer;
			case trigger_event::OnEmote:
				snprintf(buffer, sizeof(buffer), "Targeted by emote %d", GetEventDataValue(event, 0));
				return buffer;
			case trigger_event::OnSpellCast:
				snprintf(buffer, sizeof(buffer), "Successfully cast spell %d", GetEventDataValue(event, 0));
				return buffer;
			case trigger_event::OnGossipAction:
				snprintf(buffer, sizeof(buffer), "Gossip menu %d, action %d",
					GetEventDataValue(event, 0), GetEventDataValue(event, 1));
				return buffer;
			case trigger_event::OnQuestAccept:
				snprintf(buffer, sizeof(buffer), "Player accepted quest %d", GetEventDataValue(event, 0));
				return buffer;
			case trigger_event::OnAllPlayersDead:
				return "All players in instance are dead (wipe)";
			case trigger_event::OnPlayerEnterInstance:
				return "A player entered the instance";
			case trigger_event::OnPlayerLeaveInstance:
				return "A player left the instance";
			case trigger_event::OnTimer:
				if (GetEventDataValue(event, 1) > 0)
				{
					snprintf(buffer, sizeof(buffer), "Every %d-%d ms",
						GetEventDataValue(event, 0), GetEventDataValue(event, 1));
				}
				else
				{
					snprintf(buffer, sizeof(buffer), "Every %d ms", GetEventDataValue(event, 0));
				}
				return buffer;
			case trigger_event::OnSummonedUnitDied:
				return "A summoned creature died";
			case trigger_event::OnEncounterStateChanged:
				snprintf(buffer, sizeof(buffer), "Encounter slot %d -> state %d (0 = any)",
					GetEventDataValue(event, 0), GetEventDataValue(event, 1));
				return buffer;
			case trigger_event::OnPlayerLevelUp:
				if (GetEventDataValue(event, 0) > 0)
				{
					snprintf(buffer, sizeof(buffer), "Player reached level %d", GetEventDataValue(event, 0));
					return buffer;
				}
				return "Player gained any level";
			case trigger_event::OnPlayerStandStateChanged:
				if (const int standState = GetEventDataValue(event, 0); standState > 0)
				{
					snprintf(buffer, sizeof(buffer), "Player entered stand state %s",
						standState < static_cast<int>(std::size(s_standStateNames)) ? s_standStateNames[standState] : "?");
					return buffer;
				}
				return "Player changed stand state";
			default:
				return "";
			}
		}

		/// Resolves an entry's name for a node summary, falling back to the bare id so a dangling
		/// reference reads as a broken link rather than as an empty node.
		template <class Manager>
		String DescribeEntryRef(const Manager& manager, const int id)
		{
			if (id == 0)
			{
				return "none";
			}

			const auto* entry = manager.getById(static_cast<uint32>(id));
			if (entry == nullptr)
			{
				return "<missing #" + std::to_string(id) + ">";
			}

			return entry->name();
		}

		/// One-line summary of what an action does, shown in its blueprint node under the action
		/// name. Only the field that identifies the action is summarised - the rest is what the
		/// details panel is for.
		String DescribeAction(const proto::Project& project, const proto::TriggerAction& action)
		{
			char buffer[256];

			const auto quoteText = [&action]() -> String
			{
				if (action.texts_size() == 0 || action.texts(0).empty())
				{
					return "(no text)";
				}

				String text = action.texts(0);
				if (text.size() > 40)
				{
					text = text.substr(0, 37) + "...";
				}

				return "\"" + text + "\"";
			};

			switch (action.action())
			{
			case trigger_actions::Trigger:
				return "-> " + DescribeEntryRef(project.triggers, GetActionDataValue(action, 0));
			case trigger_actions::Say:
			case trigger_actions::Yell:
			case trigger_actions::Emote:
			case trigger_actions::BroadcastMessage:
				return quoteText();
			case trigger_actions::CastSpell:
			case trigger_actions::ApplyAura:
			case trigger_actions::RemoveAura:
			case trigger_actions::SetSpellCooldown:
				return DescribeEntryRef(project.spells, GetActionDataValue(action, 0));
			case trigger_actions::SummonCreature:
			case trigger_actions::QuestKillCredit:
				return DescribeEntryRef(project.units, GetActionDataValue(action, 0));
			case trigger_actions::QuestEventOrExploration:
			case trigger_actions::QuestExplorationCredit:
			case trigger_actions::QuestFailQuest:
				return DescribeEntryRef(project.quests, GetActionDataValue(action, 0));
			case trigger_actions::Teleport:
				return DescribeEntryRef(project.maps, GetActionDataValue(action, 0));
			case trigger_actions::PlaySpellVisual:
				return DescribeEntryRef(project.spellVisualizations, GetActionDataValue(action, 0));
			case trigger_actions::SetVariable:
				return DescribeEntryRef(project.variables, GetActionDataValue(action, 0));
			case trigger_actions::Delay:
				snprintf(buffer, sizeof(buffer), "%d ms", GetActionDataValue(action, 0));
				return buffer;
			case trigger_actions::SetPhase:
				snprintf(buffer, sizeof(buffer), "phase %d", GetActionDataValue(action, 0));
				return buffer;
			case trigger_actions::SetEncounterState:
			{
				const int state = GetActionDataValue(action, 1);
				snprintf(buffer, sizeof(buffer), "slot %d -> %s", GetActionDataValue(action, 0),
					(state >= 0 && state < static_cast<int>(std::size(s_encounterStateNames)))
						? s_encounterStateNames[state] : "?");
				return buffer;
			}
			case trigger_actions::SetInstanceVariable:
				snprintf(buffer, sizeof(buffer), "key %d = %d",
					GetActionDataValue(action, 0), GetActionDataValue(action, 1));
				return buffer;
			case trigger_actions::MoveTo:
				snprintf(buffer, sizeof(buffer), "to %d, %d, %d", GetActionDataValue(action, 0),
					GetActionDataValue(action, 1), GetActionDataValue(action, 2));
				return buffer;
			default:
				break;
			}

			// Everything else is best identified by who it acts on.
			const int target = static_cast<int>(action.target());
			if (target > 0 && target < static_cast<int>(trigger_action_target::Count_))
			{
				return String("on ") + s_actionTargetStrings[target];
			}

			return "";
		}

		/// Whether an action suspends the rest of the sequence rather than running straight into
		/// the next one. Both cases re-enter ExecuteTrigger later with an action offset, so the
		/// link leaving these nodes is a resumption rather than a plain hand-off.
		bool ActionSuspendsSequence(const proto::TriggerAction& action)
		{
			if (action.action() == trigger_actions::Delay)
			{
				return GetActionDataValue(action, 0) > 0;
			}

			if (action.action() == trigger_actions::MoveTo)
			{
				return GetActionDataValue(action, 3) != 0;
			}

			return false;
		}

		// Draws a single trigger event in the editor.
		// Assumes that 'event' is a mutable reference from your proto TriggerEvent message.
		void DrawTriggerEvent(proto::TriggerEvent& event, int eventIndex, proto::TriggerEntry& currentEntry,
			const proto::Project& project)
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
					SetEventDataValue(event, 0, healthPercentage);
				}
				ImGui::SameLine();
				ImGui::Text("Health Percentage (%)");
				break;
			}
			case trigger_event::OnQuestAccept:
			{
				DrawEventDataPicker(event, 0, "##QuestId", "Quest", project.quests);
				break;
			}
			case trigger_event::OnAllPlayersDead:
			case trigger_event::OnPlayerEnterInstance:
			case trigger_event::OnPlayerLeaveInstance:
			{
				ImGui::TextDisabled("No additional parameters required for this event type.");
				break;
			}
			case trigger_event::OnGossipAction:
			{
				// This event requires a Menu ID and Action ID.
				DrawEventDataPicker(event, 0, "##MenuId", "Gossip Menu", project.gossipMenus);
				DrawEventDataInt(event, 1, "##ActionId", "Action ID", 150.0f,
					"Index of the action inside the gossip menu. Creature gossip events are matched exactly rather than through the usual zero-is-a-wildcard rule, so 0 means the first action here.");
				break;
			}
			case trigger_event::OnSpellHit:
			case trigger_event::OnSpellAuraRemoved:
			case trigger_event::OnSpellCast:
			{
				// These events require a Spell ID.
				DrawEventDataPicker(event, 0, "##SpellId", "Spell", project.spells);
				break;
			}
			case trigger_event::OnEmote:
			{
				// This event requires an Emote ID.
				DrawEventDataPicker(event, 0, "##EmoteId", "Emote", project.emotes);
				break;
			}
				case trigger_event::OnTimer:
				{
					// Data: <INTERVAL-MS>[, <INTERVAL-MAX-MS>]
					int intervalMin = GetEventDataValue(event, 0);
					ImGui::SetNextItemWidth(150);
					if (ImGui::InputInt("##TimerIntervalMin", &intervalMin))
					{
						if (intervalMin < 0) intervalMin = 0;
						SetEventDataValue(event, 0, intervalMin);
					}
					ImGui::SameLine();
					ImGui::Text("Interval (ms)");

					int intervalMax = GetEventDataValue(event, 1);
					ImGui::SetNextItemWidth(150);
					if (ImGui::InputInt("##TimerIntervalMax", &intervalMax))
					{
						if (intervalMax < 0) intervalMax = 0;
						SetEventDataValue(event, 1, intervalMax);
					}
					ImGui::SameLine();
					ImGui::Text("Max Interval (ms, 0 = fixed)");
					ImGui::TextDisabled("Set the trigger's 'Only In Combat' flag to only fire this timer during combat.");
					break;
				}
				case trigger_event::OnEncounterStateChanged:
				{
					// Data: [<SLOT-ID>], [<STATE>] - 0 acts as a wildcard.
					int slotId = GetEventDataValue(event, 0);
					ImGui::SetNextItemWidth(150);
					if (ImGui::InputInt("##EncStateChangedSlot", &slotId))
					{
						if (slotId < 0) slotId = 0;
						SetEventDataValue(event, 0, slotId);
					}
					ImGui::SameLine();
					ImGui::Text("Encounter Slot (0 = any)");

					DrawEventDataEnum(event, 1, "##EncStateChangedState", "State",
						s_encounterStateFilterNames, static_cast<int>(std::size(s_encounterStateFilterNames)),
						"Event data treats 0 as a wildcard, so 'Not Started' cannot be filtered on here - a trigger that needs it has to test the EncounterState condition function instead.");
					break;
				}
				case trigger_event::OnPlayerLevelUp:
				{
					// Data: [<LEVEL>]; zero is the usual event-data wildcard.
					DrawEventDataInt(event, 0, "##LevelUpLevel", "Level (0 = any)", 150.0f,
						"Requires the trigger's 'Player Trigger' flag, since players carry no trigger list of their own.");
					break;
				}
				case trigger_event::OnPlayerStandStateChanged:
				{
					// Data: [<STAND-STATE>]; zero is the usual event-data wildcard, so "Stand"
					// cannot be filtered on and reads as "Any" here.
					DrawEventDataEnum(event, 0, "##StandStateChangedState", "Stand State",
						s_standStateFilterNames, static_cast<int>(std::size(s_standStateFilterNames)),
						"Requires the trigger's 'Player Trigger' flag, since players carry no trigger list of their own.");
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
			if (DrawDangerButton("Remove Event", ImVec2(-1, 0)))
			{
				// Removing an event invalidates the indices, so break out after removal.
				currentEntry.mutable_newevents()->DeleteSubrange(eventIndex, 1);
			}

			ImGui::PopStyleVar();
		}

		// Draws a single trigger action in the editor.
		// Assumes that 'action' is a mutable reference from your proto TriggerAction message.
		void DrawTriggerAction(proto::TriggerAction& action, int actionIndex, proto::TriggerEntry& currentEntry,
			const proto::Project& project)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));

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

			ImGui::SameLine();

			// Reordering controls: simple Up/Down arrow buttons
			if (actionIndex > 0 && ImGui::ArrowButton("Up", ImGuiDir_Up))
			{
				auto* prevAction = currentEntry.mutable_actions(actionIndex - 1);
				std::swap(*prevAction, action);
			}
			ImGui::SameLine();
			if (actionIndex < currentEntry.actions_size() - 1 && ImGui::ArrowButton("Down", ImGuiDir_Down))
			{
				auto* nextAction = currentEntry.mutable_actions(actionIndex + 1);
				std::swap(*nextAction, action);
			}

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
				DrawActionDataPicker(action, 0, "##TriggerId", "Trigger", project.triggers,
					"The trigger to execute. These are the links the Chain View draws.");
				break;
			}
			case trigger_actions::Say:
			case trigger_actions::Yell:
			{
				// Data: <SOUND-ID>, <LANGUAGE>; Texts: <TEXT>
				DrawActionDataPicker(action, 0, "##SoundId", "Sound", project.sounds,
					"Played alongside the chat line for every player who can hear it.");

				// data[1] is still written so authored values survive a round trip, but the world
				// server's HandleSay / HandleYell never read it - there is no per-language chat.
				ImGui::BeginDisabled(true);
				DrawActionDataInt(action, 1, "##Language", "Language");
				ImGui::EndDisabled();
				ImGui::SameLine();
				DrawHelpMarker("Not implemented: the world server ignores this value. It is kept so existing data is not silently discarded.");

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
				DrawActionDataEnum(action, 0, "##NewState", "New State",
					s_worldObjectStateNames, static_cast<int>(std::size(s_worldObjectStateNames)),
					"Doors also update their dynamic line-of-sight collision when this changes.");
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
				DrawActionDataPicker(action, 0, "##SpellId", "Spell", project.spells);
				DrawActionDataEnum(action, 1, "##CastTarget", "Cast Target",
					s_spellCastTargetNames, static_cast<int>(std::size(s_spellCastTargetNames)),
					"Who the spell is aimed at, resolved relative to the caster picked above. The cast fails outright if this target cannot be resolved.");
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
				// Data: <X>, <Y>, <Z>, [<WAIT:0/1>]
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

				bool waitFlag = (action.data_size() > 3) ? (action.data(3) != 0) : false;
				if (ImGui::Checkbox("Wait for Arrival##MoveToWait", &waitFlag))
				{
					while (action.data_size() < 4)
						action.add_data(0);
					action.set_data(3, waitFlag ? 1 : 0);
				}
				ImGui::SameLine();
				DrawHelpMarker("When enabled, the following actions are paused until this unit reaches the target position");
				break;
			}
			case trigger_actions::Teleport:
			{
				DrawActionDataPicker(action, 0, "##MapId", "Map", project.maps);

				// Data: <Map>, <X>, <Y>, <Z>, <Facing>
				float pos[3] = { 0.0f, 0.0f, 0.0f };
				if (action.data_size() >= 4)
				{
					pos[0] = static_cast<float>(action.data(1));
					pos[1] = static_cast<float>(action.data(2));
					pos[2] = static_cast<float>(action.data(3));
				}
				ImGui::SetNextItemWidth(300);
				if (ImGui::InputFloat3("##Position", pos))
				{
					while (action.data_size() < 4)
					{
						action.add_data(0);
					}
					action.set_data(1, static_cast<int>(pos[0]));
					action.set_data(2, static_cast<int>(pos[1]));
					action.set_data(3, static_cast<int>(pos[2]));
				}
				ImGui::SameLine();
				ImGui::Text("Position (X, Y, Z)");

				float facing = 0.0f;
				if (action.data_size() >= 5)
				{
					facing = static_cast<float>(action.data(4));
				}
				ImGui::SetNextItemWidth(150.0f);
				if (ImGui::InputFloat("##Facing", &facing))
				{
					if (action.data_size() < 5)
					{
						action.add_data(0);
					}
					action.set_data(4, static_cast<int>(facing));
				}
				ImGui::SameLine();
				ImGui::Text("Facing (Degree)");
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
				DrawActionDataEnum(action, 0, "##StandState", "Stand State",
					s_standStateNames, static_cast<int>(std::size(s_standStateNames)));
				break;
			}
			case trigger_actions::SetVirtualEquipmentSlot:
			{
				// Data: <SLOT:0-2>, <ITEM-DISPLAY-ID>
				DrawActionDataEnum(action, 0, "##EquipSlot", "Slot",
					s_virtualEquipmentSlotNames, static_cast<int>(std::size(s_virtualEquipmentSlotNames)));

				// The world server writes this straight into object_fields::VirtualItem0..2, which
				// hold display ids. The field was labelled "Item Entry" here, which is a different
				// id space entirely.
				DrawActionDataPicker(action, 1, "##ItemDisplayId", "Item Display", project.itemDisplays,
					"Display id, not an item entry: the value goes directly into the unit's VirtualItem field.");
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
				DrawActionDataPicker(action, 0, "##CooldownSpellId", "Spell", project.spells);
				DrawActionDataInt(action, 1, "##CooldownTimeMs", "Cooldown Duration (ms)");
				break;
			}
			case trigger_actions::QuestKillCredit:
			{
				// Data: <CREATURE-ENTRY-ID>
				DrawActionDataPicker(action, 0, "##CreatureEntryId", "Creature", project.units);
				break;
			}
			case trigger_actions::QuestEventOrExploration:
			{
				// Data: <QUEST-ID>
				DrawActionDataPicker(action, 0, "##QuestEventId", "Quest", project.quests);
				break;
			}
			case trigger_actions::SetVariable:
			{
				// Data: <VARIABLE-ID>, [<NUMERIC-VALUE>]; Texts: [<STRING_VALUE>]
				DrawActionDataPicker(action, 0, "##VarId", "Variable", project.variables);
				DrawActionDataInt(action, 1, "##NumVal", "Numeric Value");

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
				// Data: <MOUNT-DISPLAY-ID>; written straight to object_fields::MountDisplayId.
				DrawActionDataPicker(action, 0, "##MountId", "Mount Display", project.models,
					"Model display id, not a mount item or spell. Zero dismounts the unit.");
				break;
			}
			case trigger_actions::Despawn:
			{
				// Data: NONE
				ImGui::TextDisabled("No additional parameters required for this action.");
				break;
			}
			case trigger_actions::Emote:
			{
				// Data: <SOUND-ID>; Texts: <TEXT>
				DrawActionDataPicker(action, 0, "##SoundId", "Sound", project.sounds);

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
			case trigger_actions::SetEncounterState:
			{
				DrawActionDataInt(action, 0, "##EncounterSlotId", "Encounter Slot ID");
				DrawActionDataEnum(action, 1, "##EncounterState", "New State",
					s_encounterStateNames, static_cast<int>(std::size(s_encounterStateNames)));
				break;
			}
							case trigger_actions::SummonCreature:
				{
					// Data: <CREATURE-ENTRY>, [<X>,<Y>,<Z>], [<DESPAWN-MS>], [<ATTACK-NEAREST:0/1>]
					DrawActionDataPicker(action, 0, "##SummonEntry", "Creature", project.units);
					ImGui::TextDisabled("Leave X/Y/Z at 0 to spawn at the owner/target position.");
					DrawActionDataInt(action, 1, "##SummonX", "X");
					DrawActionDataInt(action, 2, "##SummonY", "Y");
					DrawActionDataInt(action, 3, "##SummonZ", "Z");
					DrawActionDataInt(action, 4, "##SummonDespawn", "Despawn after (ms, 0 = never)");

					bool attackNearest = (action.data_size() > 5) ? (action.data(5) != 0) : false;
					if (ImGui::Checkbox("Attack nearest player##SummonAttack", &attackNearest))
					{
						SetActionDataValue(action, 5, attackNearest ? 1 : 0);
					}
					break;
				}
				case trigger_actions::Taunt:
				{
					ImGui::TextDisabled("Forces the target creature's threat onto the triggering unit. Target = the creature to taunt (defaults to owner).");
					break;
				}
				case trigger_actions::ModifyThreat:
				{
					// Data: <AMOUNT> (signed)
					DrawActionDataInt(action, 0, "##ThreatAmount", "Threat Amount (may be negative)");
					ImGui::TextDisabled("Modifies threat the target has on the owning creature.");
					break;
				}
				case trigger_actions::ResetThreat:
				{
					ImGui::TextDisabled("Wipes the entire threat table of the target creature (defaults to owner).");
					break;
				}
				case trigger_actions::ApplyAura:
				case trigger_actions::RemoveAura:
				{
					// Data: <SPELL-ID>
					DrawActionDataPicker(action, 0, "##AuraSpellId", "Spell", project.spells);
					if (currentActionType == trigger_actions::ApplyAura)
					{
						ImGui::TextDisabled("Instantly applies the spell's auras to the target (no cast time/cost).");
					}
					else
					{
						ImGui::TextDisabled("Removes all auras of this spell from the target.");
					}
					break;
				}
				case trigger_actions::SetInstanceVariable:
				{
					// Data: <KEY>, <VALUE>
					DrawActionDataInt(action, 0, "##InstanceVarKey", "Variable Key");
					DrawActionDataInt(action, 1, "##InstanceVarValue", "Value");
					ImGui::TextDisabled("Stores a counter on the world instance (readable via the InstanceVariable condition).");
					break;
				}
				case trigger_actions::BroadcastMessage:
				{
					// Texts: <MESSAGE>
					std::string text = (action.texts_size() > 0) ? action.texts(0) : "";
					ImGui::SetNextItemWidth(-1);
					if (ImGui::InputText("##BroadcastText", &text))
					{
						if (action.texts_size() > 0)
							action.set_texts(0, text);
						else
							action.add_texts(text);
					}
					ImGui::SameLine();
					ImGui::Text("Message");
					ImGui::TextDisabled("Sent as a system message to every player in the instance,");
					ImGui::TextDisabled("resolved into each recipient's own client locale.");
					ImGui::TextDisabled("The documented raid-warning message type is not implemented;");
					ImGui::TextDisabled("every message goes out as ChatType::System.");
					break;
				}
				case trigger_actions::QuestExplorationCredit:
				{
					// Data: <QUEST-ID>
					DrawActionDataPicker(action, 0, "##ExploreQuestId", "Quest", project.quests);
					ImGui::TextDisabled("Marks the exploration/event objective of the quest as done for the player target without completing its other objectives.");
					break;
				}
				case trigger_actions::QuestFailQuest:
				{
					// Data: <QUEST-ID>
					DrawActionDataPicker(action, 0, "##FailQuestId", "Quest", project.quests);
					ImGui::TextDisabled("Fails the quest for the player target if it is in their quest log (e.g. escort npc died).");
					break;
				}
				case trigger_actions::SetFollowTarget:
				{
					// Data: [<DISTANCE-TENTHS>]
					DrawActionDataInt(action, 0, "##FollowDistance", "Distance (tenths, 0 = 2.5 units)");
					ImGui::TextDisabled("Makes the creature target follow the unit that raised this trigger (escort). Combat interrupts; follow resumes after reset.");
					break;
				}
				case trigger_actions::ClearFollowTarget:
				{
					ImGui::TextDisabled("Stops the creature target from following and resumes its normal idle movement.");
					break;
				}
				case trigger_actions::PlaySpellVisual:
				{
					// Data: <VISUALIZATION-ID>, [<EVENT>]
					DrawActionDataPicker(action, 0, "##SpellVisualizationId", "Visualization", project.spellVisualizations);
					// Unlike every other action data field, an absent value here does not mean zero:
					// the world server substitutes IMPACT. Showing 'Start Cast' for unset data
					// would name an event that is not the one that plays, so the effective default
					// is displayed instead - and only written once the author picks a value.
					int visualEvent = (action.data_size() > 1)
						? action.data(1)
						: static_cast<int>(proto::IMPACT);
					if (DrawEnumCombo("##SpellVisualEvent", "Event", visualEvent,
						s_spellVisualEventNames, static_cast<int>(std::size(s_spellVisualEventNames)),
						"Use Impact: the cast and aura events expect a matching lifecycle event to clean up after them, and nothing raises those for a visual played this way."))
					{
						SetActionDataValue(action, 1, visualEvent);
					}
					ImGui::TextDisabled("Plays a spell visualization on the unit target for every client that can see it.");
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
			if (DrawDangerButton("Remove Action", ImVec2(-1, 0)))
			{
				// Removing an element from a repeated field invalidates indices so break out after removal.
				currentEntry.mutable_actions()->DeleteSubrange(actionIndex, 1);
			}

			ImGui::PopStyleVar();
		}

		// === Condition editing ===
		//
		// A TriggerCondition is a small tree: `type` says whether the two sides are compared or
		// combined logically, and each side can itself be another condition. The editor used to
		// model none of that — it never wrote `type`, never rendered a nested condition, and showed
		// one as a plain integer 0. Typing in that box called set_leftlong, which clears the oneof
		// and takes the whole subtree with it while `type` stayed AndCondition, silently degrading
		// an authored "A and B" gate into "B alone" with nothing logged.

		/// One entry of the trigger-function picker.
		struct TriggerFunctionInfo
		{
			/// Enum value written to the condition.
			proto::TriggerFunction value;

			/// Name shown in the picker.
			const char* name;

			/// Label for the function's data argument, or nullptr when it takes none.
			const char* dataLabel;

			/// Labels for the two-argument form, for the one function that has one. RandomValue
			/// reads a lone value as the maximum but a pair as [min, max], so a stored pair has to
			/// be edited as a pair — labelling index 0 "Max" is how a range like [10, 50] silently
			/// became [60, 50] when someone typed the maximum they wanted.
			const char* pairLabels[2];
		};

		/// Every function a condition can call, in one place. The left- and right-hand pickers used
		/// to carry their own copy of this list behind a hardcoded count, which is how
		/// LivingCreatureCount ended up authorable on neither side.
		const TriggerFunctionInfo s_triggerFunctions[] = {
			{ proto::Phase,               "Phase",               nullptr,      { nullptr, nullptr } },
			{ proto::Health,              "Health",              nullptr,      { nullptr, nullptr } },
			{ proto::HealthPct,           "HealthPct",           nullptr,      { nullptr, nullptr } },
			{ proto::Mana,                "Mana",                nullptr,      { nullptr, nullptr } },
			{ proto::ManaPct,             "ManaPct",             nullptr,      { nullptr, nullptr } },
			{ proto::IsInCombat,          "IsInCombat",          nullptr,      { nullptr, nullptr } },
			{ proto::EncounterState,      "EncounterState",      "Slot",       { nullptr, nullptr } },
			{ proto::PlayerCount,         "PlayerCount",         nullptr,      { nullptr, nullptr } },
			{ proto::TargetHealthPct,     "TargetHealthPct",     nullptr,      { nullptr, nullptr } },
			{ proto::HasAura,             "HasAura",             "Spell",      { nullptr, nullptr } },
			{ proto::RandomValue,         "RandomValue",         "Max",        { "Min", "Max" } },
			{ proto::InstanceVariable,    "InstanceVariable",    "Key",        { nullptr, nullptr } },
			{ proto::LivingCreatureCount, "LivingCreatureCount", "Unit Entry", { nullptr, nullptr } },
		};

		// The guard that keeps this from drifting again: adding a TriggerFunction without listing it
		// here means it cannot be authored, which is exactly the bug this table replaces.
		static_assert(std::size(s_triggerFunctions) == proto::TriggerFunction_ARRAYSIZE,
			"s_triggerFunctions must list every TriggerFunction");

		/// Labels for TriggerConditionType, in enum order.
		const char* s_conditionTypeNames[] = {
			"Compare (Bool)", "Compare (Integer)", "Compare (Float)", "Compare (String)",
			"All of (And)", "Any of (Or)"
		};

		static_assert(std::size(s_conditionTypeNames) == proto::TriggerConditionType_ARRAYSIZE,
			"s_conditionTypeNames size mismatch");

		/// Which member of a condition's value oneof is currently set.
		enum class ConditionValueKind
		{
			/// Nothing set. Comparisons read this as 0; And/Or read a missing side as pass/fail.
			Unset,
			Integer,
			Float,
			Function,
			Condition,
			/// Set, but the world server has no evaluation for it. Shown read-only so it survives.
			String
		};

		/// Which half of a condition is being edited.
		enum class ConditionSide { Left, Right };

		// Protobuf generates a separate method per field, so the two sides need this dispatch. It is
		// deliberately mechanical and kept apart from the UI below, which stays single-copy.

		ConditionValueKind GetValueKind(const proto::TriggerCondition& c, const ConditionSide side)
		{
			if (side == ConditionSide::Left)
			{
				if (c.has_leftlong())      return ConditionValueKind::Integer;
				if (c.has_leftfloat())     return ConditionValueKind::Float;
				if (c.has_leftfunction())  return ConditionValueKind::Function;
				if (c.has_leftcondition()) return ConditionValueKind::Condition;
				if (c.has_leftstring())    return ConditionValueKind::String;
				return ConditionValueKind::Unset;
			}

			if (c.has_rightlong())      return ConditionValueKind::Integer;
			if (c.has_rightfloat())     return ConditionValueKind::Float;
			if (c.has_rightfunction())  return ConditionValueKind::Function;
			if (c.has_rightcondition()) return ConditionValueKind::Condition;
			if (c.has_rightstring())    return ConditionValueKind::String;
			return ConditionValueKind::Unset;
		}

		void ClearValue(proto::TriggerCondition& c, const ConditionSide side)
		{
			if (side == ConditionSide::Left)
			{
				c.clear_leftlong();
				c.clear_leftfloat();
				c.clear_leftstring();
				c.clear_leftfunction();
				c.clear_leftcondition();
				c.clear_leftfunctiondata();
				return;
			}

			c.clear_rightlong();
			c.clear_rightfloat();
			c.clear_rightstring();
			c.clear_rightfunction();
			c.clear_rightcondition();
			c.clear_rightfunctiondata();
		}

		int64 GetLongValue(const proto::TriggerCondition& c, const ConditionSide side)
		{
			return side == ConditionSide::Left ? c.leftlong() : c.rightlong();
		}

		void SetLongValue(proto::TriggerCondition& c, const ConditionSide side, const int64 value)
		{
			if (side == ConditionSide::Left) c.set_leftlong(value); else c.set_rightlong(value);
		}

		float GetFloatValue(const proto::TriggerCondition& c, const ConditionSide side)
		{
			return side == ConditionSide::Left ? c.leftfloat() : c.rightfloat();
		}

		void SetFloatValue(proto::TriggerCondition& c, const ConditionSide side, const float value)
		{
			if (side == ConditionSide::Left) c.set_leftfloat(value); else c.set_rightfloat(value);
		}

		const String& GetStringValue(const proto::TriggerCondition& c, const ConditionSide side)
		{
			return side == ConditionSide::Left ? c.leftstring() : c.rightstring();
		}

		proto::TriggerFunction GetFunctionValue(const proto::TriggerCondition& c, const ConditionSide side)
		{
			return side == ConditionSide::Left ? c.leftfunction() : c.rightfunction();
		}

		void SetFunctionValue(proto::TriggerCondition& c, const ConditionSide side, const proto::TriggerFunction value)
		{
			if (side == ConditionSide::Left) c.set_leftfunction(value); else c.set_rightfunction(value);
		}

		int GetFunctionDataSize(const proto::TriggerCondition& c, const ConditionSide side)
		{
			return side == ConditionSide::Left ? c.leftfunctiondata_size() : c.rightfunctiondata_size();
		}

		int64 GetFunctionData(const proto::TriggerCondition& c, const ConditionSide side, const int index)
		{
			return side == ConditionSide::Left ? c.leftfunctiondata(index) : c.rightfunctiondata(index);
		}

		void SetFunctionData(proto::TriggerCondition& c, const ConditionSide side, const int index, const int64 value)
		{
			if (side == ConditionSide::Left) c.set_leftfunctiondata(index, value); else c.set_rightfunctiondata(index, value);
		}

		void AddFunctionData(proto::TriggerCondition& c, const ConditionSide side, const int64 value)
		{
			if (side == ConditionSide::Left) c.add_leftfunctiondata(value); else c.add_rightfunctiondata(value);
		}

		void ClearFunctionData(proto::TriggerCondition& c, const ConditionSide side)
		{
			if (side == ConditionSide::Left) c.clear_leftfunctiondata(); else c.clear_rightfunctiondata();
		}

		proto::TriggerCondition* GetMutableSubCondition(proto::TriggerCondition& c, const ConditionSide side)
		{
			return side == ConditionSide::Left ? c.mutable_leftcondition() : c.mutable_rightcondition();
		}

		/// How deep the editor will render nested conditions. Deeper subtrees are left untouched
		/// rather than hidden behind widgets that could overwrite them.
		constexpr int s_maxConditionDepth = 4;

		void DrawTriggerCondition(proto::TriggerCondition& condition, int depth);
		void DrawConditionValue(proto::TriggerCondition& condition, ConditionSide side, int depth, bool isLogical);

		/// Draws the value picker for one side of a condition.
		/// @param isLogical True when the parent combines the two sides with And/Or rather than
		///        comparing them, which changes what an unset side means.
		void DrawConditionValue(proto::TriggerCondition& condition, const ConditionSide side, const int depth,
			const bool isLogical)
		{
			const bool left = (side == ConditionSide::Left);
			ImGui::PushID(left ? "LeftValue" : "RightValue");

			ImGui::Text(left ? "Left Value:" : "Right Value:");

			const ConditionValueKind kind = GetValueKind(condition, side);

			// A string value has no server-side evaluation, so there is no widget that could write
			// one. It is still shown rather than silently reinterpreted as an integer.
			if (kind == ConditionValueKind::String)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
					"String \"%s\" - the world server does not evaluate string values.",
					GetStringValue(condition, side).c_str());
				if (DrawDangerButton("Clear Value", ImVec2(120, 0)))
				{
					ClearValue(condition, side);
				}
				ImGui::PopID();
				return;
			}

			static const char* s_valueKinds[] = { "Unset", "Integer", "Float", "Function", "Condition" };

			// Nesting one level deeper than the editor renders would create a condition it then
			// refuses to show — an empty comparison that evaluates 0 == 0, so the side silently
			// always passes. Drop the option rather than offer a choice that cannot be finished.
			// A subtree that is already there still has to display as "Condition", so the option is
			// only withheld when picking it would create one.
			const bool allowNesting = (depth + 1 <= s_maxConditionDepth) || (kind == ConditionValueKind::Condition);
			const int kindCount = static_cast<int>(std::size(s_valueKinds)) - (allowNesting ? 0 : 1);

			int kindIndex = static_cast<int>(kind);

			ImGui::SetNextItemWidth(130);
			if (ImGui::Combo("##Kind", &kindIndex, s_valueKinds, kindCount))
			{
				// Switching kinds is the one place a subtree is legitimately discarded, and it takes
				// a deliberate change of the picker to get here.
				ClearValue(condition, side);
				switch (static_cast<ConditionValueKind>(kindIndex))
				{
				case ConditionValueKind::Integer:   SetLongValue(condition, side, 0); break;
				case ConditionValueKind::Float:     SetFloatValue(condition, side, 0.0f); break;
				case ConditionValueKind::Function:  SetFunctionValue(condition, side, proto::Phase); break;
				case ConditionValueKind::Condition: GetMutableSubCondition(condition, side)->set_operator_(proto::Equal); break;
				default: break;
				}
			}

			switch (static_cast<ConditionValueKind>(kindIndex))
			{
			case ConditionValueKind::Unset:
				ImGui::SameLine();
				ImGui::TextDisabled(isLogical ? "(this side is skipped)" : "(counts as 0)");
				break;

			case ConditionValueKind::Integer:
			{
				ImGui::SameLine();
				int64 value = GetLongValue(condition, side);
				ImGui::SetNextItemWidth(120);
				if (ImGui::InputScalar("##Int", ImGuiDataType_S64, &value))
				{
					SetLongValue(condition, side, value);
				}
				break;
			}

			case ConditionValueKind::Float:
			{
				ImGui::SameLine();
				float value = GetFloatValue(condition, side);
				ImGui::SetNextItemWidth(120);
				if (ImGui::InputFloat("##Float", &value))
				{
					SetFloatValue(condition, side, value);
				}
				break;
			}

			case ConditionValueKind::Function:
			{
				ImGui::SameLine();

				const proto::TriggerFunction current = GetFunctionValue(condition, side);
				int funcIndex = 0;
				for (int i = 0; i < static_cast<int>(std::size(s_triggerFunctions)); ++i)
				{
					if (s_triggerFunctions[i].value == current)
					{
						funcIndex = i;
						break;
					}
				}

				const char* funcNames[std::size(s_triggerFunctions)] = {};
				for (size_t i = 0; i < std::size(s_triggerFunctions); ++i)
				{
					funcNames[i] = s_triggerFunctions[i].name;
				}

				ImGui::SetNextItemWidth(180);
				if (ImGui::Combo("##Func", &funcIndex, funcNames, static_cast<int>(std::size(funcNames))))
				{
					const TriggerFunctionInfo& picked = s_triggerFunctions[funcIndex];
					SetFunctionValue(condition, side, picked.value);

					// The previous function's argument means nothing to the new one — leaving it
					// would silently reinterpret a spell id as an instance-variable key. Seed the
					// new function's argument instead, so what the box shows is what is stored:
					// an argument left implicit is read as 0 by the server but written as nothing.
					ClearFunctionData(condition, side);
					if (picked.dataLabel != nullptr)
					{
						AddFunctionData(condition, side, 0);
					}
				}

				const TriggerFunctionInfo& func = s_triggerFunctions[funcIndex];
				if (func.dataLabel != nullptr)
				{
					// A stored pair is edited as a pair; everything else takes the single label.
					// See the note on TriggerFunctionInfo::pairLabels.
					const bool isPair = (GetFunctionDataSize(condition, side) >= 2) && (func.pairLabels[0] != nullptr);
					const int argCount = isPair ? 2 : 1;

					for (int arg = 0; arg < argCount; ++arg)
					{
						ImGui::SameLine();
						int64 funcData = GetFunctionDataSize(condition, side) > arg ? GetFunctionData(condition, side, arg) : 0;
						ImGui::SetNextItemWidth(90);
						if (ImGui::InputScalar(isPair ? func.pairLabels[arg] : func.dataLabel, ImGuiDataType_S64, &funcData))
						{
							if (GetFunctionDataSize(condition, side) > arg)
							{
								SetFunctionData(condition, side, arg, funcData);
							}
							else
							{
								AddFunctionData(condition, side, funcData);
							}
						}
					}
				}
				break;
			}

			case ConditionValueKind::Condition:
			{
				if (depth + 1 > s_maxConditionDepth)
				{
					ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
						"Nested condition is deeper than the editor shows; it is left unchanged.");
					break;
				}

				ImGui::Indent();
				ImGui::Separator();
				ImGui::BeginGroup();
				DrawTriggerCondition(*GetMutableSubCondition(condition, side), depth + 1);
				ImGui::EndGroup();
				ImGui::Separator();
				ImGui::Unindent();
				break;
			}

			default:
				break;
			}

			ImGui::PopID();
		}

		/// Draws one condition, recursing into nested conditions.
		void DrawTriggerCondition(proto::TriggerCondition& condition, const int depth)
		{
			ImGui::PushID(&condition);

			int typeIndex = static_cast<int>(condition.type());
			ImGui::SetNextItemWidth(180);
			if (ImGui::Combo("Type##CondType", &typeIndex, s_conditionTypeNames,
				static_cast<int>(std::size(s_conditionTypeNames))))
			{
				condition.set_type(static_cast<proto::TriggerConditionType>(typeIndex));
			}

			const bool isLogical = (condition.type() == proto::AndCondition || condition.type() == proto::OrCondition);

			if (isLogical)
			{
				ImGui::TextWrapped("Both sides are evaluated as conditions in their own right; the operator is not used.");

				// The world server's fallbacks for a missing side are deliberate but easy to author
				// by accident, so they are stated where the mistake is made.
				for (const ConditionSide side : { ConditionSide::Left, ConditionSide::Right })
				{
					if (GetValueKind(condition, side) != ConditionValueKind::Condition)
					{
						ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s side is not a condition: it %s.",
							side == ConditionSide::Left ? "Left" : "Right",
							condition.type() == proto::AndCondition ? "counts as passed" : "counts as failed");
					}
				}
			}
			else
			{
				static const char* s_operators[] = { "==", "!=", ">", ">=", "<", "<=" };
				int opValue = condition.operator_();
				ImGui::SetNextItemWidth(80);
				if (ImGui::Combo("Operator##CondOp", &opValue, s_operators, static_cast<int>(std::size(s_operators))))
				{
					condition.set_operator_(static_cast<proto::TriggerComparisonOperator>(opValue));
				}
			}

			ImGui::Spacing();
			DrawConditionValue(condition, ConditionSide::Left, depth, isLogical);
			ImGui::Spacing();
			DrawConditionValue(condition, ConditionSide::Right, depth, isLogical);

			ImGui::PopID();
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

	TriggerEditorWindow::~TriggerEditorWindow()
	{
		if (m_nodeEditorCtx)
		{
			ax::NodeEditor::DestroyEditor(m_nodeEditorCtx);
			m_nodeEditorCtx = nullptr;
		}
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

		// Handle a pending jump queued by a double-clicked Trigger action node last frame. The
		// view is deliberately not switched: following a chain hand-off should land in the same
		// kind of view the author was already reading.
		if (m_jumpToTriggerId != 0)
		{
			SelectEntryById(m_jumpToTriggerId);
			m_jumpToTriggerId = 0;
			m_blueprintSelection = { BlueprintNodeKind::None, -1 };
			m_relayoutBlueprint = true;
			return;
		}

		// Blueprint / Form view toggle.
		if (ImGui::Button(m_showBlueprintView ? "Form View" : "Blueprint View"))
		{
			m_showBlueprintView = !m_showBlueprintView;
		}

		ImGui::SameLine();
		DrawHelpMarker("Blueprint View lays the trigger out as a graph: red event nodes feed the "
			"blue action chain, and the details panel edits whichever node is selected. Form View "
			"is the same data as lists, which is still the faster way to reorder a long action list.");

		// The blueprint draws its own header and owns the full remaining region, so the separator
		// the form view opens with would only eat vertical space.
		if (m_showBlueprintView)
		{
			DrawBlueprintView(currentEntry);
			return;
		}

		ImGui::Separator();
		ImGui::Spacing();

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

			CHECKBOX_FLAG_PROP(flags, "Player Trigger", trigger_flags::PlayerTrigger);
			ImGui::SameLine();
			DrawHelpMarker("Evaluate this trigger for every player character. Players have no entry to carry a trigger list, so this flag is what makes a trigger global to all of them. Needed for player events such as On Level Up.");

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("Condition (Optional Gate)"))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Execution Condition");
			ImGui::TextWrapped("If set, the trigger only executes when this condition passes. Leave type as 'None' to run unconditionally.");

			ImGui::Spacing();

			bool hasCondition = currentEntry.has_condition();
			if (ImGui::Checkbox("Enable Condition##CondGate", &hasCondition))
			{
				if (hasCondition)
				{
					auto* cond = currentEntry.mutable_condition();
					cond->set_operator_(proto::Equal);
				}
				else
				{
					currentEntry.clear_condition();
				}
			}

			if (hasCondition)
			{
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				DrawTriggerCondition(*currentEntry.mutable_condition(), 0);
			}

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
					DrawTriggerEvent(*event, i, currentEntry, m_project);
					ImGui::PopStyleColor();

					ImGui::Spacing();
					ImGui::PopID();
				}
			}

			ImGui::Spacing();

			if (DrawSuccessButton("+ Add Event", ImVec2(-1, 0)))
			{
				ImGui::OpenPopup("Event Details");
			}

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

			if (DrawSuccessButton("+ Add Action", ImVec2(-1, 0)))
			{
				auto* newAction = currentEntry.add_actions();
				newAction->set_action(0);
				newAction->set_target(0);
				newAction->set_targetname("");
				currentAction = currentEntry.actions_size() - 1;
			}

			ImGui::BeginDisabled(currentAction == -1 || currentAction >= currentEntry.actions_size());
			if (DrawDangerButton("Remove Action", ImVec2(-1, 0)))
			{
				currentEntry.mutable_actions()->erase(currentEntry.mutable_actions()->begin() + currentAction);
				currentAction = -1;
			}
			ImGui::EndDisabled();

			ImGui::Spacing();
			ImGui::TextDisabled("%d trigger actions", currentEntry.actions_size());

			ImGui::BeginChild("actionListScrollable", ImVec2(-1, 0));

			// List of actions with drag & drop reordering
			for (int idx = 0; idx < currentEntry.actions_size(); ++idx)
			{
				const auto& action = currentEntry.actions(idx);
				const bool isSelected = (currentAction == idx);

				ImGui::PushID(idx);


				const char* actionTypeName = (action.action() >= 0 && action.action() < static_cast<int>(std::size(s_actionTypeNames)))
					? s_actionTypeNames[action.action()]
					: "Unknown";

				std::ostringstream stream;
				stream << "#" << std::setw(3) << std::setfill('0') << idx << " - " << actionTypeName;

				if (ImGui::Selectable(stream.str().c_str(), isSelected))
				{
					currentAction = idx;
				}

				// Drag & drop source
				if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
				{
					ImGui::SetDragDropPayload("TRIGGER_ACTION", &idx, sizeof(idx));
					ImGui::Text("Reorder to...");
					ImGui::EndDragDropSource();
				}

				// Drag & drop target
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TRIGGER_ACTION"))
					{
						int* pSourceIdx = (int*)payload->Data;
						int sourceIdx = *pSourceIdx;

						if (sourceIdx != idx)
						{
							// Swap the actions
							auto* actions = currentEntry.mutable_actions();
							std::swap(*actions->Mutable(sourceIdx), *actions->Mutable(idx));

							// Update selection if needed
							if (currentAction == sourceIdx)
								currentAction = idx;
							else if (currentAction == idx)
								currentAction = sourceIdx;
						}
					}
					ImGui::EndDragDropTarget();
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
				DrawTriggerAction(*action, currentAction, currentEntry, m_project);
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

			if (DrawSuccessButton("Add", ImVec2(120, 0)))
			{
				auto& event = *currentEntry.add_newevents();
				event.set_type(trigger_event::Type(selectedEventType));
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			if (DrawNeutralButton("Cancel", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	namespace
	{
		// Node editor object ids. Events and actions are addressed by index within the selected
		// trigger, so the three id spaces are kept apart by base offset rather than by hashing
		// something that could collide.
		constexpr uint64 s_eventNodeBase = 0x0001'0000ull;
		constexpr uint64 s_actionNodeBase = 0x0002'0000ull;
		constexpr uint64 s_eventOutPinBase = 0x0010'0000ull;
		constexpr uint64 s_actionInPinBase = 0x0020'0000ull;
		constexpr uint64 s_actionOutPinBase = 0x0030'0000ull;
		constexpr uint64 s_linkBase = 0x0100'0000ull;

		ax::NodeEditor::NodeId EventNodeId(const int index)
		{
			return ax::NodeEditor::NodeId(s_eventNodeBase + static_cast<uint64>(index));
		}

		ax::NodeEditor::NodeId ActionNodeId(const int index)
		{
			return ax::NodeEditor::NodeId(s_actionNodeBase + static_cast<uint64>(index));
		}

		ax::NodeEditor::PinId EventOutPin(const int index)
		{
			return ax::NodeEditor::PinId(s_eventOutPinBase + static_cast<uint64>(index));
		}

		ax::NodeEditor::PinId ActionInPin(const int index)
		{
			return ax::NodeEditor::PinId(s_actionInPinBase + static_cast<uint64>(index));
		}

		ax::NodeEditor::PinId ActionOutPin(const int index)
		{
			return ax::NodeEditor::PinId(s_actionOutPinBase + static_cast<uint64>(index));
		}

		/// Turns a node id back into what it stands for. Returns kind None for anything that is
		/// not a node of the current graph, which is what a stale selection looks like after the
		/// selected trigger changed.
		BlueprintNodeRef DecodeNodeId(const ax::NodeEditor::NodeId nodeId)
		{
			const uint64 raw = nodeId.Get();

			if (raw >= s_actionNodeBase && raw < s_actionNodeBase + 0x1'0000ull)
			{
				return { BlueprintNodeKind::Action, static_cast<int>(raw - s_actionNodeBase) };
			}

			if (raw >= s_eventNodeBase && raw < s_eventNodeBase + 0x1'0000ull)
			{
				return { BlueprintNodeKind::Event, static_cast<int>(raw - s_eventNodeBase) };
			}

			return { BlueprintNodeKind::None, -1 };
		}

		// Node colours. Events are the red entry points, actions the blue body, and an action that
		// hands off to another trigger gets its own colour because it leaves this graph.
		const ImVec4 s_eventNodeColor(0.42f, 0.13f, 0.15f, 1.0f);
		const ImVec4 s_actionNodeColor(0.13f, 0.22f, 0.38f, 1.0f);
		const ImVec4 s_chainActionNodeColor(0.28f, 0.16f, 0.38f, 1.0f);
		const ImVec4 s_flowLinkColor(0.55f, 0.65f, 0.85f, 1.0f);
		const ImVec4 s_suspendLinkColor(0.90f, 0.65f, 0.25f, 1.0f);
	}

	void TriggerEditorWindow::DrawBlueprintCanvas(proto::TriggerEntry& currentEntry)
	{
		const int eventCount = currentEntry.newevents_size();
		const int actionCount = currentEntry.actions_size();

		// --- Nodes: events in a left column, actions in one row to their right. ---
		for (int i = 0; i < eventCount; ++i)
		{
			const auto& event = currentEntry.newevents(i);

			ax::NodeEditor::PushStyleColor(ax::NodeEditor::StyleColor_NodeBg, s_eventNodeColor);
			ax::NodeEditor::BeginNode(EventNodeId(i));

			ImGui::BeginGroup();
			{
				ImGui::Dummy(ImVec2(s_blueprintEventWidth, 0.0f));

				const char* typeName = (event.type() < std::size(s_eventTypeNames))
					? s_eventTypeNames[event.type()] : "Unknown Event";
				ImGui::TextUnformatted(typeName);

				const String summary = DescribeEvent(event);
				if (!summary.empty())
				{
					ImGui::TextDisabled("%s", summary.c_str());
				}
			}
			ImGui::EndGroup();

			ImGui::SameLine();

			// Events have an output only: the data model gives them nowhere to be wired from.
			ImGui::BeginGroup();
			ax::NodeEditor::BeginPin(EventOutPin(i), ax::NodeEditor::PinKind::Output);
			ImGui::TextUnformatted(">");
			ax::NodeEditor::EndPin();
			ImGui::EndGroup();

			ax::NodeEditor::EndNode();
			ax::NodeEditor::PopStyleColor();
		}

		for (int i = 0; i < actionCount; ++i)
		{
			const auto& action = currentEntry.actions(i);
			const bool isChainAction = (action.action() == trigger_actions::Trigger);

			ax::NodeEditor::PushStyleColor(ax::NodeEditor::StyleColor_NodeBg,
				isChainAction ? s_chainActionNodeColor : s_actionNodeColor);
			ax::NodeEditor::BeginNode(ActionNodeId(i));

			ImGui::BeginGroup();
			ax::NodeEditor::BeginPin(ActionInPin(i), ax::NodeEditor::PinKind::Input);
			ImGui::TextUnformatted(">");
			ax::NodeEditor::EndPin();
			ImGui::EndGroup();

			ImGui::SameLine();

			ImGui::BeginGroup();
			{
				ImGui::Dummy(ImVec2(s_blueprintActionWidth, 0.0f));

				const char* typeName = (action.action() < std::size(s_actionTypeNames))
					? s_actionTypeNames[action.action()] : "Unknown Action";
				ImGui::Text("%d. %s", i, typeName);

				const String summary = DescribeAction(m_project, action);
				if (!summary.empty())
				{
					ImGui::TextDisabled("%s", summary.c_str());
				}

				if (ActionSuspendsSequence(action))
				{
					ImGui::TextColored(s_suspendLinkColor, "waits before continuing");
				}

				if (isChainAction)
				{
					ImGui::TextDisabled("double-click to open");
				}
			}
			ImGui::EndGroup();

			ImGui::SameLine();

			ImGui::BeginGroup();
			ax::NodeEditor::BeginPin(ActionOutPin(i), ax::NodeEditor::PinKind::Output);
			ImGui::TextUnformatted(">");
			ax::NodeEditor::EndPin();
			ImGui::EndGroup();

			ax::NodeEditor::EndNode();
			ax::NodeEditor::PopStyleColor();
		}

		// --- Links. Every one of these is derived from the data, never authored: each event runs
		// the trigger from action 0, and the actions run in list order. ---
		uint64 linkId = s_linkBase;

		if (actionCount > 0)
		{
			for (int i = 0; i < eventCount; ++i)
			{
				ax::NodeEditor::Link(ax::NodeEditor::LinkId(linkId++),
					EventOutPin(i), ActionInPin(0), s_flowLinkColor, 2.0f);
			}
		}

		for (int i = 0; i + 1 < actionCount; ++i)
		{
			const bool suspends = ActionSuspendsSequence(currentEntry.actions(i));
			ax::NodeEditor::Link(ax::NodeEditor::LinkId(linkId++),
				ActionOutPin(i), ActionInPin(i + 1),
				suspends ? s_suspendLinkColor : s_flowLinkColor, suspends ? 3.0f : 2.0f);
		}
	}

	void TriggerEditorWindow::LayoutBlueprint(const proto::TriggerEntry& currentEntry)
	{
		// The topology is fixed, so the layout can be too: events stack in a column on the left,
		// actions run left to right in list order at a height that clears the event column.
		const int eventCount = currentEntry.newevents_size();
		const int actionCount = currentEntry.actions_size();

		constexpr float eventRowPitch = 110.0f;
		constexpr float actionColumnPitch = 300.0f;
		constexpr float actionRowY = 40.0f;
		constexpr float actionStartX = 360.0f;

		for (int i = 0; i < eventCount; ++i)
		{
			ax::NodeEditor::SetNodePosition(EventNodeId(i), ImVec2(0.0f, i * eventRowPitch));
		}

		for (int i = 0; i < actionCount; ++i)
		{
			ax::NodeEditor::SetNodePosition(ActionNodeId(i),
				ImVec2(actionStartX + i * actionColumnPitch, actionRowY));
		}
	}

	void TriggerEditorWindow::DrawBlueprintContextMenus(proto::TriggerEntry& currentEntry)
	{
		// Popups must be opened and drawn outside the canvas coordinate space.
		ax::NodeEditor::Suspend();

		ax::NodeEditor::NodeId contextNodeId;
		if (ax::NodeEditor::ShowNodeContextMenu(&contextNodeId))
		{
			m_blueprintContextNode = DecodeNodeId(contextNodeId);
			ImGui::OpenPopup("BlueprintNodeContext");
		}
		else if (ax::NodeEditor::ShowBackgroundContextMenu())
		{
			m_blueprintContextNode = { BlueprintNodeKind::None, -1 };
			ImGui::OpenPopup("BlueprintBackgroundContext");
		}

		if (ImGui::BeginPopup("BlueprintNodeContext"))
		{
			const BlueprintNodeRef node = m_blueprintContextNode;

			if (node.kind == BlueprintNodeKind::Event && node.index < currentEntry.newevents_size())
			{
				ImGui::TextDisabled("Event %d", node.index);
				ImGui::Separator();

				if (ImGui::MenuItem("Delete Event"))
				{
					currentEntry.mutable_newevents()->DeleteSubrange(node.index, 1);
					m_blueprintSelection = { BlueprintNodeKind::None, -1 };
					m_relayoutBlueprint = true;
				}
			}
			else if (node.kind == BlueprintNodeKind::Action && node.index < currentEntry.actions_size())
			{
				ImGui::TextDisabled("Action %d", node.index);
				ImGui::Separator();

				// Order is the whole of the model here, so moving a node is the only structural
				// edit the graph can offer. Dragging one somewhere else would mean nothing.
				if (ImGui::MenuItem("Move Earlier", nullptr, false, node.index > 0))
				{
					currentEntry.mutable_actions()->SwapElements(node.index, node.index - 1);
					m_blueprintPendingSelect = { BlueprintNodeKind::Action, node.index - 1 };
					m_relayoutBlueprint = true;
				}

				if (ImGui::MenuItem("Move Later", nullptr, false, node.index + 1 < currentEntry.actions_size()))
				{
					currentEntry.mutable_actions()->SwapElements(node.index, node.index + 1);
					m_blueprintPendingSelect = { BlueprintNodeKind::Action, node.index + 1 };
					m_relayoutBlueprint = true;
				}

				ImGui::Separator();

				if (ImGui::MenuItem("Delete Action"))
				{
					currentEntry.mutable_actions()->DeleteSubrange(node.index, 1);
					m_blueprintSelection = { BlueprintNodeKind::None, -1 };
					m_relayoutBlueprint = true;
				}
			}
			else
			{
				ImGui::TextDisabled("(node no longer exists)");
			}

			ImGui::EndPopup();
		}

		if (ImGui::BeginPopup("BlueprintBackgroundContext"))
		{
			if (ImGui::BeginMenu("Add Event"))
			{
				for (int i = 0; i < static_cast<int>(std::size(s_eventTypeNames)); ++i)
				{
					if (ImGui::MenuItem(s_eventTypeNames[i]))
					{
						currentEntry.add_newevents()->set_type(static_cast<uint32>(i));
						m_blueprintPendingSelect = { BlueprintNodeKind::Event, currentEntry.newevents_size() - 1 };
						m_relayoutBlueprint = true;
					}
				}

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Add Action"))
			{
				for (int i = 0; i < static_cast<int>(std::size(s_actionTypeNames)); ++i)
				{
					if (ImGui::MenuItem(s_actionTypeNames[i]))
					{
						// Appended, because that is the only position the list has a name for.
						// Use Move Earlier on the new node to place it.
						auto* newAction = currentEntry.add_actions();
						newAction->set_action(static_cast<uint32>(i));
						newAction->set_target(trigger_action_target::OwningObject);
						m_blueprintPendingSelect = { BlueprintNodeKind::Action, currentEntry.actions_size() - 1 };
						m_relayoutBlueprint = true;
					}
				}

				ImGui::EndMenu();
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Re-layout"))
			{
				m_relayoutBlueprint = true;
			}

			ImGui::EndPopup();
		}

		ax::NodeEditor::Resume();
	}

	void TriggerEditorWindow::DrawBlueprintDetails(proto::TriggerEntry& currentEntry)
	{
		// A node can disappear under the selection - a delete from the context menu, or an edit in
		// the form view - so the index is re-checked here rather than trusted from last frame.
		BlueprintNodeRef selection = m_blueprintSelection;
		if ((selection.kind == BlueprintNodeKind::Event && selection.index >= currentEntry.newevents_size()) ||
			(selection.kind == BlueprintNodeKind::Action && selection.index >= currentEntry.actions_size()))
		{
			selection = { BlueprintNodeKind::None, -1 };
			m_blueprintSelection = selection;
		}

		switch (selection.kind)
		{
		case BlueprintNodeKind::Event:
			DrawSectionHeader("Event Properties");
			ImGui::PushID(selection.index);
			DrawTriggerEvent(*currentEntry.mutable_newevents(selection.index), selection.index,
				currentEntry, m_project);
			ImGui::PopID();
			break;

		case BlueprintNodeKind::Action:
			DrawSectionHeader("Action Properties");
			ImGui::PushID(selection.index);
			DrawTriggerAction(*currentEntry.mutable_actions(selection.index), selection.index,
				currentEntry, m_project);
			ImGui::PopID();
			break;

		default:
			// Nothing selected: the trigger's own properties are not nodes, so this is where they
			// live.
			DrawSectionHeader("Trigger Properties");

			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputText("##BlueprintTriggerName", currentEntry.mutable_name());
			ImGui::TextDisabled("Name (id %u)", currentEntry.id());

			ImGui::Spacing();

			uint32 probability = currentEntry.probability();
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::InputScalar("##BlueprintProbability", ImGuiDataType_U32, &probability))
			{
				currentEntry.set_probability(std::min<uint32>(probability, 100u));
			}
			ImGui::SameLine();
			ImGui::Text("Probability (%%)");

			ImGui::Spacing();
			DrawSectionHeader("Flags");

			const struct { const char* label; uint32 bit; const char* help; } flagRows[] = {
				{ "Cancel On Owner Death", trigger_flags::AbortOnOwnerDeath, "Stop the trigger as soon as the owner dies." },
				{ "Only In Combat", trigger_flags::OnlyInCombat, "Only run while the owner is in combat; aborts when combat ends." },
				{ "Only One Instance", trigger_flags::OnlyOneInstance, "Refuse to start while a run of this trigger is still going." },
				{ "Player Trigger", trigger_flags::PlayerTrigger, "Evaluate for every player. Players carry no trigger list, so this is what makes a trigger global to them - needed for player events such as On Level Up." },
			};

			for (const auto& row : flagRows)
			{
				bool set = (currentEntry.flags() & row.bit) != 0;
				if (ImGui::Checkbox(row.label, &set))
				{
					currentEntry.set_flags(set
						? (currentEntry.flags() | row.bit)
						: (currentEntry.flags() & ~row.bit));
				}

				ImGui::SameLine();
				DrawHelpMarker(row.help);
			}

			ImGui::Spacing();
			DrawSectionHeader("Condition");
			ImGui::TextWrapped("Checked after the probability roll and before the first action.");

			bool hasCondition = currentEntry.has_condition();
			if (ImGui::Checkbox("Enable Condition##BlueprintCond", &hasCondition))
			{
				if (hasCondition)
				{
					currentEntry.mutable_condition()->set_operator_(proto::Equal);
				}
				else
				{
					currentEntry.clear_condition();
				}
			}

			if (hasCondition)
			{
				ImGui::Spacing();
				DrawTriggerCondition(*currentEntry.mutable_condition(), 0);
			}
			break;
		}
	}

	void TriggerEditorWindow::DrawBlueprintView(proto::TriggerEntry& currentEntry)
	{
		// Lazy-create the editor context on first use.
		if (!m_nodeEditorCtx)
		{
			ax::NodeEditor::Config config;
			config.SettingsFile = nullptr; // Do not persist layout to file.
			m_nodeEditorCtx = ax::NodeEditor::CreateEditor(&config);
		}

		// The graph's shape is a function of the trigger and its counts, so a relayout is due
		// whenever any of those change. Between relayouts a dragged node stays where it was put.
		const BlueprintShape shape{ currentEntry.id(), currentEntry.newevents_size(), currentEntry.actions_size() };
		if (!(shape == m_blueprintShape))
		{
			m_blueprintShape = shape;
			m_relayoutBlueprint = true;
		}

		if (currentEntry.newevents_size() == 0)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
				"No events: this trigger only runs when another trigger's Trigger action calls it.");
		}

		if (currentEntry.actions_size() == 0)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
				"No actions: this trigger does nothing. Right-click the canvas to add one.");
		}

		ImGui::TextDisabled("Right-click the canvas to add nodes, a node to reorder or delete it. "
			"Links follow the data and cannot be drawn by hand.");

		const float detailsWidth = std::min(s_blueprintDetailsWidth,
			std::max(300.0f, ImGui::GetContentRegionAvail().x - 320.0f));
		const float canvasWidth = ImGui::GetContentRegionAvail().x - detailsWidth - ImGui::GetStyle().ItemSpacing.x;

		ax::NodeEditor::SetCurrentEditor(m_nodeEditorCtx);
		ax::NodeEditor::Begin("TriggerBlueprint", ImVec2(canvasWidth, 0.0f));

		DrawBlueprintCanvas(currentEntry);

		// A node added through the context menu does not exist in the editor until the frame after
		// the edit, so selecting it has to wait until here rather than happening at the click.
		if (m_blueprintPendingSelect.kind != BlueprintNodeKind::None)
		{
			const BlueprintNodeRef pending = m_blueprintPendingSelect;
			m_blueprintPendingSelect = { BlueprintNodeKind::None, -1 };

			const bool stillThere =
				(pending.kind == BlueprintNodeKind::Event && pending.index < currentEntry.newevents_size()) ||
				(pending.kind == BlueprintNodeKind::Action && pending.index < currentEntry.actions_size());

			if (stillThere)
			{
				ax::NodeEditor::ClearSelection();
				ax::NodeEditor::SelectNode(pending.kind == BlueprintNodeKind::Event
					? EventNodeId(pending.index)
					: ActionNodeId(pending.index));
				m_blueprintSelection = pending;
			}
		}

		// Positions are applied after the nodes exist so the editor has measured them.
		if (m_relayoutBlueprint)
		{
			LayoutBlueprint(currentEntry);
			ax::NodeEditor::NavigateToContent(0.0f);
			m_relayoutBlueprint = false;
		}

		// Link editing is refused rather than ignored: every link here is implied by the data, so
		// there is nothing a dragged link could store. Saying so beats a canvas that silently
		// swallows the gesture.
		if (ax::NodeEditor::BeginCreate())
		{
			ax::NodeEditor::PinId startPin, endPin;
			if (ax::NodeEditor::QueryNewLink(&startPin, &endPin))
			{
				ax::NodeEditor::RejectNewItem(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), 2.0f);

				ax::NodeEditor::Suspend();
				ImGui::SetTooltip("Links cannot be drawn: every event starts the trigger at action 0,\n"
					"and actions always run in list order. Right-click a node to reorder it.");
				ax::NodeEditor::Resume();
			}
		}
		ax::NodeEditor::EndCreate();

		// Deleting a link is likewise meaningless; deleting a node is a real edit, and is applied
		// after the canvas closes so the repeated field is not resized mid-frame.
		BlueprintNodeRef pendingDelete{ BlueprintNodeKind::None, -1 };
		if (ax::NodeEditor::BeginDelete())
		{
			ax::NodeEditor::LinkId deletedLink;
			while (ax::NodeEditor::QueryDeletedLink(&deletedLink))
			{
				ax::NodeEditor::RejectDeletedItem();
			}

			ax::NodeEditor::NodeId deletedNode;
			while (ax::NodeEditor::QueryDeletedNode(&deletedNode))
			{
				if (ax::NodeEditor::AcceptDeletedItem())
				{
					pendingDelete = DecodeNodeId(deletedNode);
				}
			}
		}
		ax::NodeEditor::EndDelete();

		// Selection drives the details panel.
		{
			ax::NodeEditor::NodeId selectedNode;
			if (ax::NodeEditor::GetSelectedNodes(&selectedNode, 1) > 0)
			{
				m_blueprintSelection = DecodeNodeId(selectedNode);
			}
			else if (ax::NodeEditor::GetSelectedObjectCount() == 0)
			{
				// Clicking empty canvas deselects, which is what puts the trigger's own
				// properties back in the details panel.
				m_blueprintSelection = { BlueprintNodeKind::None, -1 };
			}
		}

		// Double-clicking a Trigger action follows the chain to the trigger it calls.
		if (const ax::NodeEditor::NodeId doubleClicked = ax::NodeEditor::GetDoubleClickedNode())
		{
			const BlueprintNodeRef node = DecodeNodeId(doubleClicked);
			if (node.kind == BlueprintNodeKind::Action && node.index < currentEntry.actions_size())
			{
				const auto& action = currentEntry.actions(node.index);
				if (action.action() == trigger_actions::Trigger)
				{
					const uint32 targetId = static_cast<uint32>(GetActionDataValue(action, 0));
					if (targetId != 0 && m_manager.getById(targetId) != nullptr)
					{
						m_jumpToTriggerId = targetId;
					}
				}
			}
		}

		DrawBlueprintContextMenus(currentEntry);

		ax::NodeEditor::End();
		ax::NodeEditor::SetCurrentEditor(nullptr);

		// Applied out here: DeleteSubrange invalidates the indices the canvas was drawn from.
		if (pendingDelete.kind == BlueprintNodeKind::Event && pendingDelete.index < currentEntry.newevents_size())
		{
			currentEntry.mutable_newevents()->DeleteSubrange(pendingDelete.index, 1);
			m_blueprintSelection = { BlueprintNodeKind::None, -1 };
			m_relayoutBlueprint = true;
		}
		else if (pendingDelete.kind == BlueprintNodeKind::Action && pendingDelete.index < currentEntry.actions_size())
		{
			currentEntry.mutable_actions()->DeleteSubrange(pendingDelete.index, 1);
			m_blueprintSelection = { BlueprintNodeKind::None, -1 };
			m_relayoutBlueprint = true;
		}

		ImGui::SameLine();

		ImGui::BeginChild("BlueprintDetails", ImVec2(detailsWidth, 0.0f), true);
		DrawBlueprintDetails(currentEntry);
		ImGui::EndChild();
	}
}
