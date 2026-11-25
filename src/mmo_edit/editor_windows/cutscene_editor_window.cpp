// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "cutscene_editor_window.h"
#include "editor_imgui_helpers.h"

#include "editors/world_editor/editor_viewpoint.h"

#include "base/localization.h"
#include "game/cutscene.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <functional>

namespace mmo
{
	namespace
	{
		/// Reads a world editor viewpoint ("x,y,z,yaw,pitch[,fov]") from the clipboard into a keyframe.
		/// The time of the keyframe is left alone.
		bool PasteViewpoint(proto::CutsceneKeyframe& keyframe)
		{
			const char* clipboard = ImGui::GetClipboardText();
			if (clipboard == nullptr)
			{
				return false;
			}

			const auto viewpoint = EditorViewpoint::Parse(clipboard);
			if (!viewpoint)
			{
				return false;
			}

			keyframe.set_x(viewpoint->position.x);
			keyframe.set_y(viewpoint->position.y);
			keyframe.set_z(viewpoint->position.z);
			keyframe.set_yaw(viewpoint->yawDegrees);
			keyframe.set_pitch(viewpoint->pitchDegrees);
			return true;
		}

		void CopyViewpoint(const proto::CutsceneKeyframe& keyframe)
		{
			EditorViewpoint viewpoint;
			viewpoint.position = Vector3(keyframe.x(), keyframe.y(), keyframe.z());
			viewpoint.yawDegrees = keyframe.yaw();
			viewpoint.pitchDegrees = keyframe.pitch();
			if (keyframe.fov() > 0.0f)
			{
				viewpoint.fovDegrees = keyframe.fov();
			}

			ImGui::SetClipboardText(viewpoint.ToString().c_str());
		}

		/// Keeps the keyframes of a shot ordered by time, which playback relies on.
		void SortKeyframes(proto::CutsceneShot& shot)
		{
			std::stable_sort(shot.mutable_keyframes()->begin(), shot.mutable_keyframes()->end(),
				[](const proto::CutsceneKeyframe& a, const proto::CutsceneKeyframe& b) { return a.time() < b.time(); });
		}

		/// Spaces the keyframes of a shot in time so that the camera travels at a steady speed.
		void RetimeBySpeed(proto::CutsceneShot& shot, const float unitsPerSecond)
		{
			if (unitsPerSecond <= 0.0f || shot.keyframes_size() == 0)
			{
				return;
			}

			float time = 0.0f;
			shot.mutable_keyframes(0)->set_time(0);
			for (int i = 1; i < shot.keyframes_size(); ++i)
			{
				const auto& previous = shot.keyframes(i - 1);
				auto* keyframe = shot.mutable_keyframes(i);
				const Vector3 delta(keyframe->x() - previous.x(), keyframe->y() - previous.y(), keyframe->z() - previous.z());

				// A camera that only turns on the spot still gets a second for it
				time += std::max(1000.0f, delta.GetLength() / unitsPerSecond * 1000.0f);
				keyframe->set_time(static_cast<uint32>(time));
			}
		}

		template <class Manager>
		void DrawEntryCombo(const char* label, const Manager& manager, const uint32 currentId, const char* noneLabel, const std::function<void(uint32)>& onPicked)
		{
			const auto* current = manager.getById(currentId);
			const String preview = current != nullptr ? current->name() : (currentId == 0 ? String(noneLabel) : "<missing #" + std::to_string(currentId) + ">");

			if (ImGui::BeginCombo(label, preview.c_str()))
			{
				if (noneLabel != nullptr && ImGui::Selectable(noneLabel, currentId == 0))
				{
					onPicked(0);
				}

				for (int i = 0; i < manager.count(); ++i)
				{
					const auto& entry = manager.getTemplates().entry(i);
					ImGui::PushID(i);
					if (ImGui::Selectable(entry.name().c_str(), entry.id() == currentId))
					{
						onPicked(entry.id());
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}
		}
	}

	CutsceneEditorWindow::CutsceneEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.cutscenes, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Cutscenes";
	}

	void CutsceneEditorWindow::OnNewEntry(proto::TemplateManager<proto::Cutscenes, proto::CutsceneEntry>::EntryType& entry)
	{
		entry.set_name("New Cutscene");
		entry.set_map(0);
		entry.set_flags(cutscene_flags::Skippable);
		entry.add_shots()->set_name("Shot 1");
	}

	void CutsceneEditorWindow::DrawDetailsImpl(proto::CutsceneEntry& currentEntry)
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

			DrawEntryCombo("Map", m_project.maps, currentEntry.map(), nullptr, [&currentEntry](const uint32 id) { currentEntry.set_map(id); });
			ImGui::SameLine();
			DrawHelpMarker("A cutscene only starts for a player who is on this map: the client can only show the map it has loaded.");

			bool skippable = (currentEntry.flags() & cutscene_flags::Skippable) != 0;
			if (ImGui::Checkbox("Players may skip it", &skippable))
			{
				currentEntry.set_flags(skippable ? (currentEntry.flags() | cutscene_flags::Skippable) : (currentEntry.flags() & ~cutscene_flags::Skippable));
			}

			int blendIn = static_cast<int>(currentEntry.blend_in());
			if (ImGui::InputInt("Blend in from player camera (ms)", &blendIn, 100, 1000))
			{
				currentEntry.set_blend_in(static_cast<uint32>(std::max(0, blendIn)));
			}

			int blendOut = static_cast<int>(currentEntry.blend_out());
			if (ImGui::InputInt("Blend out to player camera (ms)", &blendOut, 100, 1000))
			{
				currentEntry.set_blend_out(static_cast<uint32>(std::max(0, blendOut)));
			}
			ImGui::TextDisabled("With 0 the cutscene cuts. A cut to or from a place far away shows the loading screen for a moment.");

			DrawEntryCombo("Music", m_project.sounds, currentEntry.music(), "<Zone music>", [&currentEntry](const uint32 id) { currentEntry.set_music(id); });

			const uint32 duration = GetCutsceneDuration(currentEntry);
			ImGui::Text("Length: %.1f s", static_cast<float>(duration) / 1000.0f);
			if (!IsCutscenePlayable(currentEntry))
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.9f, 0.65f, 0.2f, 1.0f), "Nothing to play yet: a shot needs a keyframe with a time above 0.");
			}
		}

		if (const auto section = ScopedEditorSection("Shots", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("Each shot is one camera. Shots play one after another; going to the next shot is a cut.");

			int removeShot = -1;
			for (int i = 0; i < currentEntry.shots_size(); ++i)
			{
				bool remove = false;
				ImGui::PushID(i);
				DrawShot(currentEntry, i, remove);
				ImGui::PopID();
				if (remove)
				{
					removeShot = i;
				}
			}

			if (removeShot >= 0)
			{
				currentEntry.mutable_shots()->erase(currentEntry.mutable_shots()->begin() + removeShot);
			}

			if (DrawPrimaryButton("Add shot"))
			{
				currentEntry.add_shots()->set_name("Shot " + std::to_string(currentEntry.shots_size()));
			}
		}

		if (const auto section = ScopedEditorSection("Sounds", ImGuiTreeNodeFlags_DefaultOpen))
		{
			DrawSounds(currentEntry);
		}

		if (const auto section = ScopedEditorSection("Subtitles", ImGuiTreeNodeFlags_DefaultOpen))
		{
			DrawTexts(currentEntry);
		}
	}

	void CutsceneEditorWindow::DrawShot(proto::CutsceneEntry& cutscene, const int shotIndex, bool& out_remove)
	{
		proto::CutsceneShot& shot = *cutscene.mutable_shots(shotIndex);

		const String header = (shot.name().empty() ? "Shot " + std::to_string(shotIndex + 1) : shot.name()) +
			"  (" + std::to_string(GetCutsceneShotDuration(shot) / 1000) + " s, " + std::to_string(shot.keyframes_size()) + " keyframes)###shot";
		if (!ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		ImGui::InputText("Name", shot.mutable_name());

		int interpolation = static_cast<int>(shot.interpolation());
		static const char* s_interpolationNames[] = { "Smooth curve", "Straight lines" };
		if (ImGui::Combo("Path", &interpolation, s_interpolationNames, 2))
		{
			shot.set_interpolation(static_cast<uint32>(interpolation));
		}

		int fadeIn = static_cast<int>(shot.fade_in());
		if (ImGui::InputInt("Fade in from black (ms)", &fadeIn, 100, 1000))
		{
			shot.set_fade_in(static_cast<uint32>(std::max(0, fadeIn)));
		}

		int fadeOut = static_cast<int>(shot.fade_out());
		if (ImGui::InputInt("Fade out to black (ms)", &fadeOut, 100, 1000))
		{
			shot.set_fade_out(static_cast<uint32>(std::max(0, fadeOut)));
		}

		int removeKeyframe = -1;
		bool timesChanged = false;
		if (ImGui::BeginTable("keyframes", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Time (ms)");
			ImGui::TableSetupColumn("X");
			ImGui::TableSetupColumn("Y");
			ImGui::TableSetupColumn("Z");
			ImGui::TableSetupColumn("Yaw");
			ImGui::TableSetupColumn("Pitch");
			ImGui::TableSetupColumn("FOV (0 = player's)");
			ImGui::TableSetupColumn("Viewpoint");
			ImGui::TableSetupColumn("");
			ImGui::TableHeadersRow();

			for (int i = 0; i < shot.keyframes_size(); ++i)
			{
				proto::CutsceneKeyframe& keyframe = *shot.mutable_keyframes(i);
				ImGui::PushID(i);
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				int time = static_cast<int>(keyframe.time());
				ImGui::SetNextItemWidth(-FLT_MIN);
				ImGui::InputInt("##time", &time, 0, 0);
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					timesChanged = true;
				}
				keyframe.set_time(static_cast<uint32>(std::max(0, time)));

				const auto floatCell = [](const char* id, const float value, const std::function<void(float)>& setter)
				{
					ImGui::TableNextColumn();
					float edited = value;
					ImGui::SetNextItemWidth(-FLT_MIN);
					if (ImGui::InputFloat(id, &edited, 0.0f, 0.0f, "%.2f"))
					{
						setter(edited);
					}
				};

				floatCell("##x", keyframe.x(), [&keyframe](const float v) { keyframe.set_x(v); });
				floatCell("##y", keyframe.y(), [&keyframe](const float v) { keyframe.set_y(v); });
				floatCell("##z", keyframe.z(), [&keyframe](const float v) { keyframe.set_z(v); });
				floatCell("##yaw", keyframe.yaw(), [&keyframe](const float v) { keyframe.set_yaw(v); });
				floatCell("##pitch", keyframe.pitch(), [&keyframe](const float v) { keyframe.set_pitch(std::clamp(v, -89.0f, 89.0f)); });
				floatCell("##fov", keyframe.fov(), [&keyframe](const float v) { keyframe.set_fov(std::clamp(v, 0.0f, 150.0f)); });

				ImGui::TableNextColumn();
				if (ImGui::SmallButton("Paste"))
				{
					PasteViewpoint(keyframe);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Takes position and direction from a viewpoint copied in the world editor (Copy Viewpoint).");
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Copy"))
				{
					CopyViewpoint(keyframe);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Copies this keyframe as a viewpoint the world editor can jump to.");
				}

				ImGui::TableNextColumn();
				if (DrawDangerSmallButton("Remove"))
				{
					removeKeyframe = i;
				}

				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		if (removeKeyframe >= 0)
		{
			shot.mutable_keyframes()->erase(shot.mutable_keyframes()->begin() + removeKeyframe);
		}

		if (timesChanged)
		{
			SortKeyframes(shot);
		}

		if (ImGui::Button("Add keyframe from clipboard viewpoint"))
		{
			proto::CutsceneKeyframe keyframe;
			keyframe.set_time(shot.keyframes_size() == 0 ? 0 : GetCutsceneShotDuration(shot) + 3000);
			keyframe.set_x(0.0f);
			keyframe.set_y(0.0f);
			keyframe.set_z(0.0f);
			keyframe.set_yaw(0.0f);
			keyframe.set_pitch(0.0f);
			if (PasteViewpoint(keyframe))
			{
				*shot.add_keyframes() = keyframe;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Add empty keyframe"))
		{
			auto* keyframe = shot.add_keyframes();
			if (shot.keyframes_size() > 1)
			{
				*keyframe = shot.keyframes(shot.keyframes_size() - 2);
			}
			else
			{
				keyframe->set_x(0.0f);
				keyframe->set_y(0.0f);
				keyframe->set_z(0.0f);
				keyframe->set_yaw(0.0f);
				keyframe->set_pitch(0.0f);
			}
			keyframe->set_time(shot.keyframes_size() == 1 ? 0 : GetCutsceneShotDuration(shot) + 3000);
		}

		ImGui::SetNextItemWidth(120.0f);
		ImGui::InputFloat("##speed", &m_retimeSpeed, 0.0f, 0.0f, "%.1f units/s");
		ImGui::SameLine();
		if (ImGui::Button("Retime by speed"))
		{
			RetimeBySpeed(shot, m_retimeSpeed);
		}
		ImGui::SameLine();
		DrawHelpMarker("Sets the times of all keyframes of this shot so that the camera flies at this speed. A running character does about 7 units per second.");

		ImGui::SameLine();
		if (DrawDangerButton("Remove shot"))
		{
			out_remove = true;
		}

		ImGui::TreePop();
	}

	void CutsceneEditorWindow::DrawSounds(proto::CutsceneEntry& cutscene)
	{
		ImGui::TextDisabled("Sound entries started on the timeline: narration, effects, a musical sting. Times are from the start of the cutscene.");

		int removeIndex = -1;
		for (int i = 0; i < cutscene.sounds_size(); ++i)
		{
			proto::CutsceneSound& sound = *cutscene.mutable_sounds(i);
			ImGui::PushID(i);

			int time = static_cast<int>(sound.time());
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::InputInt("ms", &time, 0, 0))
			{
				sound.set_time(static_cast<uint32>(std::max(0, time)));
			}

			ImGui::SameLine();
			ImGui::SetNextItemWidth(360.0f);
			DrawEntryCombo("##sound", m_project.sounds, sound.sound(), "<None>", [&sound](const uint32 id) { sound.set_sound(id); });

			ImGui::SameLine();
			if (DrawDangerSmallButton("Remove"))
			{
				removeIndex = i;
			}

			ImGui::PopID();
		}

		if (removeIndex >= 0)
		{
			cutscene.mutable_sounds()->erase(cutscene.mutable_sounds()->begin() + removeIndex);
		}

		if (ImGui::Button("Add sound"))
		{
			auto* sound = cutscene.add_sounds();
			sound->set_time(0);
			sound->set_sound(0);
		}
	}

	void CutsceneEditorWindow::DrawTexts(proto::CutsceneEntry& cutscene)
	{
		ImGui::TextDisabled("Lines shown at the bottom of the screen. Empty translations fall back to the base text.");

		const ImVec2 textBoxSize(-FLT_MIN, ImGui::GetTextLineHeight() * 3.0f);

		int removeIndex = -1;
		for (int i = 0; i < cutscene.texts_size(); ++i)
		{
			proto::CutsceneText& text = *cutscene.mutable_texts(i);
			ImGui::PushID(i);

			const String header = std::to_string(text.time()) + " ms: " + text.text().substr(0, 60) + "###text";
			if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				int time = static_cast<int>(text.time());
				ImGui::SetNextItemWidth(120.0f);
				if (ImGui::InputInt("Start (ms)", &time, 0, 0))
				{
					text.set_time(static_cast<uint32>(std::max(0, time)));
				}

				ImGui::SameLine();
				int duration = static_cast<int>(text.duration());
				ImGui::SetNextItemWidth(120.0f);
				if (ImGui::InputInt("Stays (ms)", &duration, 0, 0))
				{
					text.set_duration(static_cast<uint32>(std::max(0, duration)));
				}

				ImGui::SameLine();
				if (DrawDangerSmallButton("Remove"))
				{
					removeIndex = i;
				}

				ImGui::TextUnformatted("enUS (base)");
				ImGui::InputTextMultiline("##text_enUS", text.mutable_text(), textBoxSize);

				for (uint32 localeIndex = 1; localeIndex < static_cast<uint32>(LocaleIndex::Count); ++localeIndex)
				{
					const LocaleIndex locale = static_cast<LocaleIndex>(localeIndex);

					// Edit a copy so that merely viewing a locale does not add an empty override.
					String value = GetLocalizedString(String(), text.text_loc(), locale);

					ImGui::PushID(static_cast<int>(localeIndex));
					ImGui::TextUnformatted(LocaleCode(locale));
					if (ImGui::InputTextMultiline("##text_loc", &value, textBoxSize))
					{
						FindOrAddLoc(text.mutable_text_loc(), locale)->set_value(value);
					}
					ImGui::PopID();
				}

				ImGui::TreePop();
			}

			ImGui::PopID();
		}

		if (removeIndex >= 0)
		{
			cutscene.mutable_texts()->erase(cutscene.mutable_texts()->begin() + removeIndex);
		}

		if (ImGui::Button("Add subtitle"))
		{
			auto* text = cutscene.add_texts();
			text->set_time(0);
			text->set_duration(4000);
		}
	}
}
