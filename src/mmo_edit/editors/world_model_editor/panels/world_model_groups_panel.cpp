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

		// Selected group properties
		if (selectedGroupIndex >= 0 && selectedGroupIndex < static_cast<int32>(worldModel->GetGroupCount()))
		{
			ImGui::Separator();
			ImGui::Text("Selected Group Properties:");

			auto* selectedGroup = worldModel->GetGroup(selectedGroupIndex);
			if (selectedGroup)
			{
				// Group name editing
				char nameBuffer[256];
				std::strncpy(nameBuffer, selectedGroup->GetName().c_str(), sizeof(nameBuffer) - 1);
				nameBuffer[sizeof(nameBuffer) - 1] = '\0';
				if (ImGui::InputText("Name##group", nameBuffer, sizeof(nameBuffer)))
				{
					selectedGroup->SetName(nameBuffer);
				}

				// Group flags
				bool isInterior = selectedGroup->IsInterior();
				if (ImGui::Checkbox("Interior##group", &isInterior))
				{
					uint32 flags = selectedGroup->GetFlags();
					if (isInterior)
					{
						flags |= 0x2000; // Interior flag
						flags &= ~0x8; // Clear exterior flag
					}
					else
					{
						flags &= ~0x2000;
					}
					selectedGroup->SetFlags(flags);
				}

				bool isExterior = selectedGroup->IsExterior();
				if (ImGui::Checkbox("Exterior##group", &isExterior))
				{
					uint32 flags = selectedGroup->GetFlags();
					if (isExterior)
					{
						flags |= 0x8; // Exterior flag
						flags &= ~0x2000; // Clear interior flag
					}
					else
					{
						flags &= ~0x8;
					}
					selectedGroup->SetFlags(flags);
				}
			}
		}
	}

}
