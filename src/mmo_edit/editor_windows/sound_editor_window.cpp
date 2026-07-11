// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "sound_editor_window.h"
#include "editor_imgui_helpers.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "asset_picker_widget.h"
#include "assets/asset_registry.h"
#include "log/default_log_levels.h"
#include "shared/audio/audio.h"

namespace mmo
{
	namespace
	{
		const char* const s_soundCategoryNames[] = {
			"Sound Effects",
			"Music",
			"Ambience",
			"Interface",
			"Voice"
		};

		const std::set<String> s_soundFileExtensions = { ".wav", ".WAV", ".ogg", ".mp3" };
	}

	SoundEditorWindow::SoundEditorWindow(const String& name, proto::Project& project, EditorHost& host, IAudio* audio)
		: EditorEntryWindowBase(project, project.sounds, name)
		, m_host(host)
		, m_audio(audio)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Sounds";
	}

	void SoundEditorWindow::OnNewEntry(proto::TemplateManager<proto::Sounds, proto::SoundEntry>::EntryType& entry)
	{
		entry.set_name("New Sound");
	}

	void SoundEditorWindow::DrawDetailsImpl(proto::SoundEntry& currentEntry)
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

			int category = static_cast<int>(currentEntry.category());
			if (ImGui::Combo("Category", &category, s_soundCategoryNames, IM_ARRAYSIZE(s_soundCategoryNames)))
			{
				currentEntry.set_category(static_cast<proto::SoundEntryCategory>(category));
			}
		}

		if (const auto section = ScopedEditorSection("Playback", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bool is3d = currentEntry.is_3d();
			if (ImGui::Checkbox("3D (positional)", &is3d))
			{
				currentEntry.set_is_3d(is3d);
			}

			bool looped = currentEntry.looped();
			if (ImGui::Checkbox("Looped", &looped))
			{
				currentEntry.set_looped(looped);
			}

			bool stream = currentEntry.stream();
			if (ImGui::Checkbox("Stream (music / large files)", &stream))
			{
				currentEntry.set_stream(stream);
			}

			float volume = currentEntry.volume();
			if (ImGui::SliderFloat("Volume", &volume, 0.0f, 1.0f, "%.2f"))
			{
				currentEntry.set_volume(volume);
			}

			float pitchRange[2] = { currentEntry.pitch_min(), currentEntry.pitch_max() };
			if (ImGui::InputFloat2("Pitch Min / Max", pitchRange, "%.2f"))
			{
				if (pitchRange[0] > 0.0f && pitchRange[1] >= pitchRange[0])
				{
					currentEntry.set_pitch_min(pitchRange[0]);
					currentEntry.set_pitch_max(pitchRange[1]);
				}
			}

			if (currentEntry.is_3d())
			{
				float distances[2] = { currentEntry.min_distance(), currentEntry.max_distance() };
				if (ImGui::InputFloat2("3D Min / Max Distance", distances, "%.1f"))
				{
					if (distances[0] > 0.0f && distances[1] > distances[0])
					{
						currentEntry.set_min_distance(distances[0]);
						currentEntry.set_max_distance(distances[1]);
					}
				}
			}
		}

		if (const auto section = ScopedEditorSection("Files", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextUnformatted("A random file is picked on each playback.");

			int removeIndex = -1;
			for (int i = 0; i < currentEntry.files_size(); ++i)
			{
				ImGui::PushID(i);

				String file = currentEntry.files(i);
				if (AssetPickerWidget::Draw("##file", file, s_soundFileExtensions, nullptr, m_audio))
				{
					currentEntry.set_files(i, file);
				}

				ImGui::SameLine();
				if (ImGui::Button("Remove"))
				{
					removeIndex = i;
				}

				ImGui::PopID();
			}

			if (removeIndex >= 0)
			{
				currentEntry.mutable_files()->erase(currentEntry.mutable_files()->begin() + removeIndex);
			}

			if (ImGui::Button("Add File"))
			{
				currentEntry.add_files("");
			}
		}
	}
}
