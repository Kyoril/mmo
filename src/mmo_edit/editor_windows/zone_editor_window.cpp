// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "zone_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "game/aura.h"
#include "game/spell.h"
#include "game/zone.h"
#include "game_server/spells/spell_cast.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"

namespace mmo
{
	ZoneEditorWindow::ZoneEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.zones, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Zones";
	}

	void ZoneEditorWindow::DrawDetailsImpl(proto::ZoneEntry& currentEntry)
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

			const proto::ZoneEntry* parentZone = nullptr;
			if (currentEntry.parentzone() != 0)
			{
				parentZone = m_project.zones.getById(currentEntry.parentzone());
			}

			if (ImGui::BeginCombo("Parent Zone", parentZone ? parentZone->name().c_str() : "(None)", ImGuiComboFlags_HeightLargest))
			{
				if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0))
				{
					ImGui::SetKeyboardFocusHere(0);
				}

				m_parentZoneFilter.Draw("##parent_zone_filter", -1.0f);

				if (ImGui::Selectable("(None)"))
				{
					currentEntry.set_parentzone(0);
					m_parentZoneFilter.Clear();
					ImGui::CloseCurrentPopup();
				}

				if (ImGui::BeginChild("##parent_zone_scroll_area", ImVec2(0, 400)))
				{
					for (const auto& zone : m_project.zones.getTemplates().entry())
					{
						// Skipped due to filter
						if (m_parentZoneFilter.IsActive() && !m_parentZoneFilter.PassFilter(zone.name().c_str()))
						{
							continue;
						}

						// A zone can not be its own parent zone
						if (zone.id() == currentEntry.id())
						{
							continue;
						}

						ImGui::PushID(zone.id());
						if (ImGui::Selectable(zone.name().c_str()))
						{
							currentEntry.set_parentzone(zone.id());

							m_parentZoneFilter.Clear();
							ImGui::CloseCurrentPopup();
						}
						ImGui::PopID();
					}
				}
				ImGui::EndChild();

				ImGui::EndCombo();
			}

			const proto::FactionEntry* owningFaction = nullptr;
			if (currentEntry.owning_faction() != 0)
			{
				owningFaction = m_project.factions.getById(currentEntry.owning_faction());
			}

			if (ImGui::BeginCombo("Owning Faction", owningFaction ? owningFaction->name().c_str() : "(None)", ImGuiComboFlags_HeightLargest))
			{
				if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0))
				{
					ImGui::SetKeyboardFocusHere(0);
				}

				m_owningFactionFilter.Draw("##owning_faction_filter", -1.0f);

				if (ImGui::Selectable("(None)"))
				{
					currentEntry.set_owning_faction(0);
					m_owningFactionFilter.Clear();
					ImGui::CloseCurrentPopup();
				}

				if (ImGui::BeginChild("##owning_faction_scroll_area", ImVec2(0, 400)))
				{
					for (const auto& faction : m_project.factions.getTemplates().entry())
					{
						// Skipped due to filter
						if (m_owningFactionFilter.IsActive() && !m_owningFactionFilter.PassFilter(faction.name().c_str()))
						{
							continue;
						}

						ImGui::PushID(faction.id());
						if (ImGui::Selectable(faction.name().c_str()))
						{
							currentEntry.set_owning_faction(faction.id());

							m_owningFactionFilter.Clear();
							ImGui::CloseCurrentPopup();
						}
						ImGui::PopID();
					}
				}
				ImGui::EndChild();

				ImGui::EndCombo();
			}
		}

		if (ImGui::CollapsingHeader("Flags", ImGuiTreeNodeFlags_None))
		{
			CHECKBOX_FLAG_PROP(flags, "Allow Resting", zone_flags::AllowResting);
			CHECKBOX_FLAG_PROP(flags, "Allow Duels", zone_flags::AllowDueling);
			CHECKBOX_FLAG_PROP(flags, "Free For All PvP", zone_flags::FreeForAllPvP);
			CHECKBOX_FLAG_PROP(flags, "Contested", zone_flags::Contested);
		}
	}
}
