// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <functional>

#include <imgui.h>

#include "base/typedefs.h"
#include "proto_data/project.h"

namespace mmo
{
	/// @brief Draws a filtered combo box to pick a sound entry (0 = none).
	/// @param sounds The sound entry manager to list entries from.
	/// @param label The combo label.
	/// @param currentSoundId The currently assigned sound entry id (0 = none).
	/// @param filter Persistent text filter backing the popup's search box.
	/// @param setter Invoked with the picked sound entry id (0 when cleared).
	/// @param noneLabel Display label of the "no sound" option.
	inline void DrawSoundEntryCombo(const proto::SoundManager& sounds, const char* label, const uint32 currentSoundId, ImGuiTextFilter& filter, const std::function<void(uint32)>& setter, const char* noneLabel = "(None)")
	{
		const proto::SoundEntry* currentSound = nullptr;
		if (currentSoundId != 0)
		{
			currentSound = sounds.getById(currentSoundId);
		}

		if (ImGui::BeginCombo(label, currentSound ? currentSound->name().c_str() : noneLabel, ImGuiComboFlags_HeightLargest))
		{
			if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0))
			{
				ImGui::SetKeyboardFocusHere(0);
			}

			filter.Draw("##sound_filter", -1.0f);

			if (ImGui::Selectable(noneLabel))
			{
				setter(0);
				filter.Clear();
				ImGui::CloseCurrentPopup();
			}

			if (ImGui::BeginChild("##sound_scroll_area", ImVec2(0, 400)))
			{
				for (const auto& sound : sounds.getTemplates().entry())
				{
					// Skipped due to filter
					if (filter.IsActive() && !filter.PassFilter(sound.name().c_str()))
					{
						continue;
					}

					ImGui::PushID(sound.id());
					if (ImGui::Selectable(sound.name().c_str()))
					{
						setter(sound.id());

						filter.Clear();
						ImGui::CloseCurrentPopup();
					}
					ImGui::PopID();
				}
			}
			ImGui::EndChild();

			ImGui::EndCombo();
		}
	}
}
