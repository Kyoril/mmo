// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "faction_template_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "log/default_log_levels.h"

namespace mmo
{
	FactionTemplateEditorWindow::FactionTemplateEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.factionTemplates, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = true;
		m_toolbarButtonText = "Faction Templates";
	}

	void FactionTemplateEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
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
#define CHECKBOX_ATTR_PROP(index, label, flags) \
	{ \
		bool value = (currentEntry.attributes(index) & static_cast<uint32>(flags)) != 0; \
		if (ImGui::Checkbox(label, &value)) \
		{ \
			if (value) \
				currentEntry.mutable_attributes()->Set(index, currentEntry.attributes(index) | static_cast<uint32>(flags)); \
			else \
				currentEntry.mutable_attributes()->Set(index, currentEntry.attributes(index) & ~static_cast<uint32>(flags)); \
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

		static const char* s_factionNone = "<None>";

		if (ImGui::CollapsingHeader("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("table", 3, ImGuiTableFlags_None))
			{
				if (ImGui::TableNextColumn())
				{
					int32 faction = currentEntry.faction();

					const auto* factionEntry = m_project.factions.getById(faction);
					if (ImGui::BeginCombo("Faction", factionEntry != nullptr ? factionEntry->name().c_str() : s_factionNone, ImGuiComboFlags_None))
					{
						for (int i = 0; i < m_project.factions.count(); i++)
						{
							ImGui::PushID(i);
							const bool item_selected = m_project.factions.getTemplates().entry(i).id() == faction;
							const char* item_text = m_project.factions.getTemplates().entry(i).name().c_str();
							if (ImGui::Selectable(item_text, item_selected))
							{
								currentEntry.set_faction(m_project.factions.getTemplates().entry(i).id());
							}
							if (item_selected)
							{
								ImGui::SetItemDefaultFocus();
							}
							ImGui::PopID();
						}

						ImGui::EndCombo();
					}
				}

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
		}

		static const char* s_none = "<None>";

		if (ImGui::CollapsingHeader("Friends", ImGuiTreeNodeFlags_None))
		{
			// Add button
			if (ImGui::Button("Add Friend", ImVec2(-1, 0)))
			{
				currentEntry.add_friends(0);
			}

			if (ImGui::BeginTable("friends", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_DefaultSort);
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (int index = 0; index < currentEntry.friends_size(); ++index)
				{
					uint32 currentFriend = currentEntry.friends(index);

					ImGui::PushID(index);
					ImGui::TableNextRow();

					ImGui::TableNextColumn();

					ImGui::Text("%d", index);
					ImGui::TableNextColumn();

					const auto* friendEntry = m_project.factions.getById(currentEntry.friends(index));
					if (ImGui::BeginCombo("##friend", friendEntry != nullptr ? friendEntry->name().c_str() : s_none, ImGuiComboFlags_None))
					{
						for (int i = 0; i < m_project.factions.count(); i++)
						{
							ImGui::PushID(i);
							const bool item_selected = m_project.factions.getTemplates().entry(i).id() == currentEntry.friends(index);
							const char* item_text = m_project.factions.getTemplates().entry(i).name().c_str();
							if (ImGui::Selectable(item_text, item_selected))
							{
								currentEntry.mutable_friends()->Set(index, m_project.factions.getTemplates().entry(i).id());
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

					if (ImGui::Button("Remove"))
					{
						currentEntry.mutable_friends()->erase(currentEntry.mutable_friends()->begin() + index);
						index--;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

		if (ImGui::CollapsingHeader("Enemies", ImGuiTreeNodeFlags_None))
		{
			// Add button
			if (ImGui::Button("Add Enemy", ImVec2(-1, 0)))
			{
				currentEntry.add_enemies(0);
			}

			if (ImGui::BeginTable("enemies", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_DefaultSort);
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (int index = 0; index < currentEntry.enemies_size(); ++index)
				{
					uint32 currentEnemy = currentEntry.enemies(index);

					ImGui::PushID(index);
					ImGui::TableNextRow();

					ImGui::TableNextColumn();

					ImGui::Text("%d", index);
					ImGui::TableNextColumn();

					const auto* enemyEntry = m_project.factions.getById(currentEnemy);
					if (ImGui::BeginCombo("##enemy", enemyEntry != nullptr ? enemyEntry->name().c_str() : s_none, ImGuiComboFlags_None))
					{
						for (int i = 0; i < m_project.factions.count(); i++)
						{
							ImGui::PushID(i);
							const bool item_selected = m_project.factions.getTemplates().entry(i).id() == currentEntry.enemies(index);
							const char* item_text = m_project.factions.getTemplates().entry(i).name().c_str();
							if (ImGui::Selectable(item_text, item_selected))
							{
								currentEntry.mutable_enemies()->Set(index, m_project.factions.getTemplates().entry(i).id());
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

					if (ImGui::Button("Remove"))
					{
						currentEntry.mutable_enemies()->erase(currentEntry.mutable_enemies()->begin() + index);
						index--;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}
	}

	void FactionTemplateEditorWindow::OnNewEntry(proto::TemplateManager<ArrayType, EntryType>::EntryType& entry)
	{
		entry.set_flags(0);
		entry.set_faction(0);
		entry.set_name("New Faction Template");
		entry.set_friendmask(0);
		entry.set_enemymask(0);
		entry.set_selfmask(0);
	}
}
