// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "item_class_editor_window.h"
#include "editor_imgui_helpers.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "log/default_log_levels.h"

namespace mmo
{
	ItemClassEditorWindow::ItemClassEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.itemClasses, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Item Classes";
	}

	void ItemClassEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
	{
		if (const auto section = ScopedEditorSection("Basic", ImGuiTreeNodeFlags_DefaultOpen))
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
		}
	}

	void ItemClassEditorWindow::OnNewEntry(proto::TemplateManager<proto::ItemClasses, proto::ItemClassEntry>::EntryType& entry)
	{
		entry.set_name("New Item Class");
	}
}
