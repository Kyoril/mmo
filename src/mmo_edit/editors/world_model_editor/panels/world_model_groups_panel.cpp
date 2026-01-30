// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_groups_panel.h"

#include "scene_graph/world_model.h"
#include "../world_model_editor_instance.h"

#include <imgui.h>
#include <cstring>

namespace mmo
{
	void DrawGroupsPanel(
		WorldModel* worldModel,
		std::vector<GroupVisualization>& groupVisualizations,
		int32& selectedGroupIndex,
		GroupsPanelState& state,
		const GroupsPanelCallbacks& callbacks)
	{
		if (ImGui::Button("New Group"))
		{
			state.showNewGroupDialog = true;
		}

		ImGui::SameLine();
		
		if (ImGui::Button("Delete") && selectedGroupIndex >= 0)
		{
			if (callbacks.onRemoveGroup)
			{
				callbacks.onRemoveGroup(selectedGroupIndex);
			}
		}

		ImGui::Separator();

		if (!worldModel)
		{
			ImGui::Text("No world model loaded");
			return;
		}

		for (size_t i = 0; i < worldModel->GetGroupCount(); ++i)
		{
			const auto* group = worldModel->GetGroup(i);
			if (!group)
			{
				continue;
			}

			ImGui::PushID(static_cast<int>(i));

			bool isSelected = selectedGroupIndex == static_cast<int32>(i);
			String label = group->GetName();
			if (label.empty())
			{
				label = "(unnamed)";
			}
			
			if (group->IsInterior())
			{
				label += " [Interior]";
			}
			else if (group->IsExterior())
			{
				label += " [Exterior]";
			}

			if (ImGui::Selectable(label.c_str(), isSelected))
			{
				selectedGroupIndex = static_cast<int32>(i);
				if (callbacks.onSelectGroup)
				{
					callbacks.onSelectGroup(selectedGroupIndex);
				}
			}

			// Visibility toggle
			if (i < groupVisualizations.size())
			{
				ImGui::SameLine();
				bool visible = groupVisualizations[i].visible;
				if (ImGui::Checkbox("##vis", &visible))
				{
					groupVisualizations[i].visible = visible;
					if (callbacks.onSetGroupVisibility)
					{
						callbacks.onSetGroupVisibility(i, visible);
					}
				}
			}

			ImGui::PopID();
		}
	}

}
