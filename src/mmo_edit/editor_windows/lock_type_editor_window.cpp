// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "lock_type_editor_window.h"
#include "editor_imgui_helpers.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace mmo
{
	LockTypeEditorWindow::LockTypeEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.lockTypes, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Lock Types";
	}

	void LockTypeEditorWindow::OnNewEntry(EntryType& entry)
	{
		entry.set_name("New Lock Type");
		entry.set_verb("Unlock");
	}

	void LockTypeEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
	{
		if (const auto section = ScopedEditorSection("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("##lock_type_basic", 2, ImGuiTableFlags_None))
			{
				// Name
				ImGui::TableNextColumn();
				ImGui::InputText("Name", currentEntry.mutable_name());

				// ID (read-only)
				ImGui::TableNextColumn();
				{
					ImGui::BeginDisabled(true);
					String idString = std::to_string(currentEntry.id());
					ImGui::InputText("ID", &idString);
					ImGui::EndDisabled();
				}

				ImGui::EndTable();
			}
		}

		if (const auto section = ScopedEditorSection("Details", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("##lock_type_details", 2, ImGuiTableFlags_None))
			{
				// Verb — the action text shown to the player (e.g. "Unlock", "Open", "Pick")
				ImGui::TableNextColumn();
				ImGui::InputText("Verb", currentEntry.mutable_verb());

				// Resource Name — internal asset/sound identifier used for this lock type
				ImGui::TableNextColumn();
				ImGui::InputText("Resource Name", currentEntry.mutable_resource_name());

				ImGui::EndTable();
			}
		}
	}
}
