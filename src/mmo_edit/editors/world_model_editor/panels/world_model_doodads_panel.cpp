// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_doodads_panel.h"

#include "scene_graph/world_model.h"

#include <imgui.h>

namespace mmo
{
	void DrawDoodadsPanel(
		WorldModel* worldModel,
		int32& selectedDoodadSetIndex,
		const DoodadsPanelCallbacks& callbacks)
	{
		if (ImGui::Button("New Doodad Set"))
		{
			if (callbacks.onCreateDoodadSet)
			{
				callbacks.onCreateDoodadSet("DoodadSet_" + std::to_string(worldModel ? worldModel->GetDoodadSets().size() : 0));
			}
		}

		ImGui::Separator();

		if (!worldModel)
		{
			ImGui::Text("No world model loaded");
			return;
		}

		const auto& sets = worldModel->GetDoodadSets();
		for (size_t i = 0; i < sets.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));

			bool isSelected = selectedDoodadSetIndex == static_cast<int32>(i);
			String label = sets[i].name + " (" + std::to_string(sets[i].count) + " doodads)";

			if (ImGui::Selectable(label.c_str(), isSelected))
			{
				selectedDoodadSetIndex = static_cast<int32>(i);
			}

			ImGui::PopID();
		}

		ImGui::Separator();

		if (selectedDoodadSetIndex >= 0 && selectedDoodadSetIndex < static_cast<int32>(sets.size()))
		{
			ImGui::Text("Doodads in set '%s':", sets[selectedDoodadSetIndex].name.c_str());

			const auto& doodads = worldModel->GetDoodads();
			const auto& doodadNames = worldModel->GetDoodadNames();
			const auto& set = sets[selectedDoodadSetIndex];

			for (uint32 i = 0; i < set.count; ++i)
			{
				uint32 doodadIndex = set.startIndex + i;
				if (doodadIndex >= doodads.size())
				{
					break;
				}

				const auto& doodad = doodads[doodadIndex];
				String name = doodad.nameIndex < doodadNames.size() ? doodadNames[doodad.nameIndex] : "Unknown";
				
				ImGui::BulletText("%s", name.c_str());
			}
		}
	}

}
