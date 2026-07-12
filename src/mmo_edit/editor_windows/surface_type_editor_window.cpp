// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "surface_type_editor_window.h"
#include "editor_imgui_helpers.h"
#include "sound_entry_combo.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace mmo
{
	SurfaceTypeEditorWindow::SurfaceTypeEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.surfaceTypes, name)
		, m_host(host)
		, m_project(project)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Surface Types";
	}

	void SurfaceTypeEditorWindow::OnNewEntry(proto::TemplateManager<proto::SurfaceTypes, proto::SurfaceType>::EntryType& entry)
	{
		entry.set_name("New Surface Type");
	}

	void SurfaceTypeEditorWindow::DrawDetailsImpl(proto::SurfaceType& currentEntry)
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

		if (const auto section = ScopedEditorSection("Sounds", ImGuiTreeNodeFlags_DefaultOpen))
		{
			DrawSoundEntryCombo(m_project.sounds, "Footstep Sound", currentEntry.footstep_sound(), m_footstepSoundFilter,
				[&currentEntry](const uint32 soundId)
				{
					if (soundId == 0)
					{
						currentEntry.clear_footstep_sound();
					}
					else
					{
						currentEntry.set_footstep_sound(soundId);
					}
				});
		}
	}
}
