// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "item_subclass_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "log/default_log_levels.h"

namespace mmo
{
	ItemSubclassEditorWindow::ItemSubclassEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.itemSubclasses, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Item Subclasses";
	}

	void ItemSubclassEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
	{
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

				if (ImGui::TableNextColumn())
				{
					uint32 itemClass = currentEntry.itemclass();
					const auto* itemClassEntry = m_project.itemClasses.getById(itemClass);
					if (ImGui::BeginCombo("Item Class", itemClassEntry != nullptr ? itemClassEntry->name().c_str() : "None", ImGuiComboFlags_None))
					{
						for (int i = 0; i < m_project.itemClasses.count(); i++)
						{
							ImGui::PushID(i);
							const bool item_selected = m_project.itemClasses.getTemplates().entry(i).id() == itemClass;
							const char* item_text = m_project.itemClasses.getTemplates().entry(i).name().c_str();
							if (ImGui::Selectable(item_text, item_selected))
							{
								currentEntry.set_itemclass(m_project.itemClasses.getTemplates().entry(i).id());
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
					uint32 proficiency = currentEntry.has_requiredproficiency() ? currentEntry.requiredproficiency() : 0;
					const auto* proficiencyEntry = m_project.proficiencies.getById(proficiency);
					if (ImGui::BeginCombo("Required Proficiency", proficiencyEntry != nullptr ? proficiencyEntry->name().c_str() : "None", ImGuiComboFlags_None))
					{
						// Add "None" option
						if (ImGui::Selectable("None", proficiency == 0))
						{
							currentEntry.clear_requiredproficiency();
						}
						
						for (int i = 0; i < m_project.proficiencies.count(); i++)
						{
							ImGui::PushID(i);
							const bool item_selected = m_project.proficiencies.getTemplates().entry(i).id() == proficiency;
							const char* item_text = m_project.proficiencies.getTemplates().entry(i).name().c_str();
							if (ImGui::Selectable(item_text, item_selected))
							{
								currentEntry.set_requiredproficiency(m_project.proficiencies.getTemplates().entry(i).id());
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

				ImGui::EndTable();
			}
		}
	}

	void ItemSubclassEditorWindow::OnNewEntry(proto::TemplateManager<proto::ItemSubclasses, proto::ItemSubclassEntry>::EntryType& entry)
	{
		entry.set_name("New Item Subclass");
		entry.set_itemclass(0);
	}
}
