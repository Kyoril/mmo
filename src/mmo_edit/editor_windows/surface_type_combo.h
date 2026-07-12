// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <functional>

#include <imgui.h>

#include "base/typedefs.h"
#include "proto_data/project.h"

namespace mmo
{
	/// @brief Draws a filtered combo box to pick a surface type (0 = none).
	/// @param surfaceTypes The surface type manager to list entries from.
	/// @param label The combo label.
	/// @param currentSurfaceTypeId The currently assigned surface type id (0 = none).
	/// @param filter Persistent text filter backing the popup's search box.
	/// @param setter Invoked with the picked surface type id (0 when cleared).
	/// @param noneLabel Display label of the "no surface type" option.
	inline void DrawSurfaceTypeCombo(const proto::SurfaceTypeManager& surfaceTypes, const char* label, const uint32 currentSurfaceTypeId, ImGuiTextFilter& filter, const std::function<void(uint32)>& setter, const char* noneLabel = "(None)")
	{
		const proto::SurfaceType* currentType = nullptr;
		if (currentSurfaceTypeId != 0)
		{
			currentType = surfaceTypes.getById(currentSurfaceTypeId);
		}

		if (ImGui::BeginCombo(label, currentType ? currentType->name().c_str() : noneLabel, ImGuiComboFlags_HeightLargest))
		{
			if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0))
			{
				ImGui::SetKeyboardFocusHere(0);
			}

			filter.Draw("##surface_type_filter", -1.0f);

			if (ImGui::Selectable(noneLabel))
			{
				setter(0);
				filter.Clear();
				ImGui::CloseCurrentPopup();
			}

			if (ImGui::BeginChild("##surface_type_scroll_area", ImVec2(0, 400)))
			{
				for (const auto& surfaceType : surfaceTypes.getTemplates().entry())
				{
					// Skipped due to filter
					if (filter.IsActive() && !filter.PassFilter(surfaceType.name().c_str()))
					{
						continue;
					}

					ImGui::PushID(surfaceType.id());
					if (ImGui::Selectable(surfaceType.name().c_str()))
					{
						setter(surfaceType.id());

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
