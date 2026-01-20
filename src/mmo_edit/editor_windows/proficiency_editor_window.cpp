// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "proficiency_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "log/default_log_levels.h"

namespace mmo
{
	ProficiencyEditorWindow::ProficiencyEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.proficiencies, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Proficiencies";
	}

	void ProficiencyEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
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
					uint32 flags = currentEntry.has_flags() ? currentEntry.flags() : 0;
					if (ImGui::InputScalar("Flags", ImGuiDataType_U32, &flags))
					{
						currentEntry.set_flags(flags);
					}
				}

				ImGui::EndTable();
			}
		}
	}

	void ProficiencyEditorWindow::OnNewEntry(proto::TemplateManager<proto::Proficiencies, proto::ProficiencyEntry>::EntryType& entry)
	{
		// Generate a unique ID for the new proficiency
		uint32 maxId = 0;
		for (int i = 0; i < m_manager.count(); ++i)
		{
			const uint32 id = m_manager.getTemplates().entry().at(i).id();
			if (id > maxId)
			{
				maxId = id;
			}
		}

		entry.set_id(maxId + 1);
		entry.set_name("New Proficiency");
		entry.set_flags(0);
	}
}
