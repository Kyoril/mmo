// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_properties_panel.h"

#include "scene_graph/world_model.h"

#include <imgui.h>

namespace mmo
{
	void DrawPropertiesPanel(
		WorldModel* worldModel,
		int32 selectedGroupIndex,
		const PropertiesPanelCallbacks& callbacks)
	{
		if (ImGui::Button("Save"))
		{
			if (callbacks.onSave)
			{
				callbacks.onSave();
			}
		}

		ImGui::Separator();

		// Show selected group properties
		if (selectedGroupIndex >= 0 && worldModel && selectedGroupIndex < static_cast<int32>(worldModel->GetGroupCount()))
		{
			auto* group = worldModel->GetGroup(selectedGroupIndex);
			if (group)
			{
				ImGui::Text("Selected Group: %s", group->GetName().c_str());
				ImGui::Separator();

				// Group flags
				uint32 flags = group->GetFlags();
				
				bool isInterior = (flags & static_cast<uint32>(WorldModelGroupFlags::Interior)) != 0;
				if (ImGui::Checkbox("Interior", &isInterior))
				{
					if (isInterior)
					{
						flags |= static_cast<uint32>(WorldModelGroupFlags::Interior);
						flags &= ~static_cast<uint32>(WorldModelGroupFlags::Exterior);
					}
					else
					{
						flags &= ~static_cast<uint32>(WorldModelGroupFlags::Interior);
					}
					group->SetFlags(flags);
				}

				bool isExterior = (flags & static_cast<uint32>(WorldModelGroupFlags::Exterior)) != 0;
				if (ImGui::Checkbox("Exterior", &isExterior))
				{
					if (isExterior)
					{
						flags |= static_cast<uint32>(WorldModelGroupFlags::Exterior);
						flags &= ~static_cast<uint32>(WorldModelGroupFlags::Interior);
					}
					else
					{
						flags &= ~static_cast<uint32>(WorldModelGroupFlags::Exterior);
					}
					group->SetFlags(flags);
				}

				bool showSkybox = (flags & static_cast<uint32>(WorldModelGroupFlags::ShowSkybox)) != 0;
				if (ImGui::Checkbox("Show Skybox", &showSkybox))
				{
					if (showSkybox)
					{
						flags |= static_cast<uint32>(WorldModelGroupFlags::ShowSkybox);
					}
					else
					{
						flags &= ~static_cast<uint32>(WorldModelGroupFlags::ShowSkybox);
					}
					group->SetFlags(flags);
				}

				ImGui::Separator();

				// Bounding box
				AABB bbox = group->GetBoundingBox();
				bool bboxChanged = false;

				if (ImGui::InputFloat3("Min", bbox.min.Ptr()))
				{
					bboxChanged = true;
				}
				if (ImGui::InputFloat3("Max", bbox.max.Ptr()))
				{
					bboxChanged = true;
				}

				if (bboxChanged)
				{
					group->SetBoundingBox(bbox);
					if (callbacks.onUpdateGroupVisualizations)
					{
						callbacks.onUpdateGroupVisualizations();
					}
				}
			}
		}
	}

}
