// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "trigger_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "game/aura.h"
#include "game/spell.h"
#include "game/zone.h"
#include "game_server/spell_cast.h"
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
					}
				}

				ImGui::EndTable();
			}
			ImGui::EndChild();

		}
	}

	TriggerEditorWindow::TriggerEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.triggers, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = true;
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
		}

		ImGui::Separator();

		// Draw three tree nodes for: Events, Conditions, Actions

		if (ImGui::CollapsingHeader("Trigger Events"))
		{
			DrawTriggerEvents(currentEntry);

			ImGui::Separator();

			if (ImGui::Button("Add Event"))
			{
				ImGui::OpenPopup("Event Details");
			}
		}

		if (ImGui::CollapsingHeader("Trigger Actions"))
		{

			ImGui::Separator();

			if (ImGui::Button("Add Action"))
			{

			}
		}

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
			"Unit - On Spell Cast"
		};

		static_assert(std::size(s_eventTypeNames) == trigger_event::Count_ , "s_eventTypeNames size mismatch");

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
