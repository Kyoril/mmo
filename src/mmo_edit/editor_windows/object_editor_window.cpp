// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "object_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "game/object_type_id.h"
#include "log/default_log_levels.h"

namespace mmo
{
	ObjectEditorWindow::ObjectEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.objects, name)
		, m_host(host)
		, m_project(project)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = true;
		m_toolbarButtonText = "Objects";
	}

	void ObjectEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
	{
		if (ImGui::Button("Duplicate"))
		{
			proto::ObjectEntry* copied = m_project.objects.add();
			const uint32 newId = copied->id();
			copied->CopyFrom(currentEntry);
			copied->set_id(newId);
		}

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
#define SLIDER_FLOAT_PROP(name, label, min, max) \
	{ \
		float value = currentEntry.name(); \
		if (ImGui::InputFloat(label, &value)) \
		{ \
			if (value >= min && value <= max) \
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
#define SLIDER_UINT32_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 32, min, max)
#define SLIDER_UINT64_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 64, min, max)

		if (ImGui::CollapsingHeader("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("table", 4, ImGuiTableFlags_None))
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

			static const char* s_objectLootEntry = "<None>";

			uint32 lootEntryId = currentEntry.objectlootentry();

			const auto* lootEntry = m_project.unitLoot.getById(lootEntryId);
			if (ImGui::BeginCombo("Object Loot Entry", lootEntry != nullptr ? lootEntry->name().c_str() : s_objectLootEntry, ImGuiComboFlags_None))
			{
				ImGui::PushID(-1);
				if (ImGui::Selectable(s_objectLootEntry, lootEntry == nullptr))
				{
					currentEntry.set_objectlootentry(-1);
				}
				ImGui::PopID();

				for (int i = 0; i < m_project.unitLoot.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.unitLoot.getTemplates().entry(i).id() == lootEntryId;
					const char* item_text = m_project.unitLoot.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_objectlootentry(m_project.unitLoot.getTemplates().entry(i).id());
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

		static const char* s_noneEntryString = "<None>";

		if (ImGui::CollapsingHeader("Factions", ImGuiTreeNodeFlags_None))
		{
			int32 factionTemplate = currentEntry.factionid();

			const auto* factionEntry = m_project.factionTemplates.getById(factionTemplate);
			if (ImGui::BeginCombo("Faction Template", factionEntry != nullptr ? factionEntry->name().c_str() : s_noneEntryString, ImGuiComboFlags_None))
			{
				for (int i = 0; i < m_project.factionTemplates.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.factionTemplates.getTemplates().entry(i).id() == factionTemplate;
					const char* item_text = m_project.factionTemplates.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_factionid(m_project.factionTemplates.getTemplates().entry(i).id());
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

		if (ImGui::CollapsingHeader("Quests", ImGuiTreeNodeFlags_None))
		{
			ImGui::Text("Offers Quests");

			// Add button
			if (ImGui::Button("Add Offered Quest", ImVec2(-1, 0)))
			{
				currentEntry.add_quests(0);
			}

			if (ImGui::BeginTable("offeredQuests", 1, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Quest", ImGuiTableColumnFlags_DefaultSort);
				ImGui::TableHeadersRow();

				int index = 0;
				for (auto& quest : currentEntry.quests())
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();

					ImGui::PushID(index);

					const auto* questEntry = m_project.quests.getById(quest);
					if (ImGui::BeginCombo("##quest", questEntry != nullptr ? questEntry->name().c_str() : "None", ImGuiComboFlags_None))
					{
						for (int i = 0; i < m_project.quests.count(); i++)
						{
							ImGui::PushID(i);
							const bool item_selected = m_project.quests.getTemplates().entry(i).id() == quest;
							const char* item_text = m_project.quests.getTemplates().entry(i).name().c_str();
							if (ImGui::Selectable(item_text, item_selected))
							{
								currentEntry.mutable_quests()->Set(index, m_project.quests.getTemplates().entry(i).id());
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
						currentEntry.mutable_quests()->erase(currentEntry.mutable_quests()->begin() + index);
						index--;
					}

					ImGui::PopID();
					index++;
				}

				ImGui::EndTable();
			}

			ImGui::Text("Completes Quests");

			// Add button
			if (ImGui::Button("Add Completed Quest", ImVec2(-1, 0)))
			{
				currentEntry.add_end_quests(0);
			}

			if (ImGui::BeginTable("endedQuests", 1, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Quest", ImGuiTableColumnFlags_DefaultSort);
				ImGui::TableHeadersRow();

				int index = 0;
				for (auto& quest : currentEntry.end_quests())
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();

					ImGui::PushID(index);

					const auto* questEntry = m_project.quests.getById(quest);
					if (ImGui::BeginCombo("##end_quests", questEntry != nullptr ? questEntry->name().c_str() : "None", ImGuiComboFlags_None))
					{
						for (int i = 0; i < m_project.quests.count(); i++)
						{
							ImGui::PushID(i);
							const bool item_selected = m_project.quests.getTemplates().entry(i).id() == quest;
							const char* item_text = m_project.quests.getTemplates().entry(i).name().c_str();
							if (ImGui::Selectable(item_text, item_selected))
							{
								currentEntry.mutable_end_quests()->Set(index, m_project.quests.getTemplates().entry(i).id());
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
						currentEntry.mutable_end_quests()->erase(currentEntry.mutable_end_quests()->begin() + index);
						index--;
					}

					ImGui::PopID();
					index++;
				}

				ImGui::EndTable();
			}
		}

		if (ImGui::CollapsingHeader("Visuals", ImGuiTreeNodeFlags_None))
		{
			int32 displayId = currentEntry.displayid();

			const auto* displayIdEntry = m_project.objectDisplays.getById(displayId);
			if (ImGui::BeginCombo("Model", displayIdEntry != nullptr ? displayIdEntry->name().c_str() : s_noneEntryString, ImGuiComboFlags_None))
			{
				for (int i = 0; i < m_project.objectDisplays.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.objectDisplays.getTemplates().entry(i).id() == displayId;
					const char* item_text = m_project.objectDisplays.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_displayid(m_project.objectDisplays.getTemplates().entry(i).id());
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
	}

	void ObjectEditorWindow::OnNewEntry(proto::TemplateManager<proto::Objects, proto::ObjectEntry>::EntryType& entry)
	{
		entry.set_factionid(0);
		entry.set_displayid(0);
		entry.set_type(0);
	}
}
