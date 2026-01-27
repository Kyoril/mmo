// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_properties_panel.h"

#include "scene_graph/world_model.h"
#include "scene_graph/material_manager.h"

#include <imgui.h>
#include <cstring>

namespace mmo
{
	/// @brief Draws the group properties section.
	static void DrawGroupProperties(WorldModel* worldModel, int32 groupIndex, const PropertiesPanelCallbacks& callbacks)
	{
		auto* group = worldModel->GetGroup(groupIndex);
		if (!group)
		{
			return;
		}

		ImGui::Text("Group: %s", group->GetName().empty() ? "(unnamed)" : group->GetName().c_str());
		ImGui::Separator();
		ImGui::Spacing();

		// Group name editing
		char nameBuffer[256];
		std::strncpy(nameBuffer, group->GetName().c_str(), sizeof(nameBuffer) - 1);
		nameBuffer[sizeof(nameBuffer) - 1] = '\0';
		ImGui::Text("Name:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
		if (ImGui::InputText("##groupName", nameBuffer, sizeof(nameBuffer)))
		{
			group->SetName(nameBuffer);
		}
		ImGui::PopStyleColor();

		ImGui::Spacing();

		// Group flags
		ImGui::Text("Flags");
		ImGui::Separator();

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

		ImGui::Spacing();

		// Bounding box
		ImGui::Text("Bounding Box");
		ImGui::Separator();

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
			if (callbacks.onUpdateGroupBoundingBox)
			{
				callbacks.onUpdateGroupBoundingBox(groupIndex);
			}
		}

		ImGui::Spacing();

		// Button to set bounding box from mesh refs
		if (callbacks.onCalculateCombinedMeshBounds)
		{
			if (ImGui::Button("Set from Meshes"))
			{
				AABB combinedBounds = callbacks.onCalculateCombinedMeshBounds(groupIndex);
				if (!combinedBounds.IsNull())
				{
					group->SetBoundingBox(combinedBounds);
					if (callbacks.onUpdateGroupBoundingBox)
					{
						callbacks.onUpdateGroupBoundingBox(groupIndex);
					}
				}
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Calculate bounding box from the combined bounds of all assigned meshes");
			}
		}

		ImGui::Spacing();

		// Containment Volumes section
		ImGui::Text("Containment Volumes");
		ImGui::Separator();
		
		auto& volumes = group->GetContainmentVolumes();
		
		if (volumes.empty())
		{
			ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No containment volumes defined.");
			ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Using AABB for containment testing.");
		}
		else
		{
			ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "%zu volume(s) defined", volumes.size());
		}
		
		ImGui::Spacing();
		
		// Button to add a containment volume from current bounding box
		if (ImGui::Button("Add Volume from AABB"))
		{
			ContainmentVolume newVolume = ContainmentVolume::FromAABB(
				group->GetBoundingBox(),
				"Volume " + std::to_string(volumes.size() + 1)
			);
			group->AddContainmentVolume(newVolume);
			if (callbacks.onUpdateContainmentVolumes)
			{
				callbacks.onUpdateContainmentVolumes(groupIndex);
			}
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Create a box-shaped containment volume matching the current bounding box.\nUseful as a starting point for defining interior spaces.");
		}
		
		// List existing volumes with editing
		static int selectedVolumeIndex = -1;
		
		for (size_t i = 0; i < volumes.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));
			
			auto& volume = volumes[i];
			
			bool isSelected = (selectedVolumeIndex == static_cast<int>(i));
			String volumeLabel = volume.name.empty() ? "Volume " + std::to_string(i + 1) : volume.name;
			volumeLabel += " (" + std::to_string(volume.planes.size()) + " planes)";
			
			if (ImGui::CollapsingHeader(volumeLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				// Volume name
				char nameBuffer[256];
				std::strncpy(nameBuffer, volume.name.c_str(), sizeof(nameBuffer) - 1);
				nameBuffer[sizeof(nameBuffer) - 1] = '\0';
				ImGui::Text("Name:");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-1);
				if (ImGui::InputText("##volumeName", nameBuffer, sizeof(nameBuffer)))
				{
					volume.name = nameBuffer;
				}
				
				// Volume bounding box (read-only display)
				ImGui::Text("Bounds: (%.1f, %.1f, %.1f) - (%.1f, %.1f, %.1f)",
					volume.boundingBox.min.x, volume.boundingBox.min.y, volume.boundingBox.min.z,
					volume.boundingBox.max.x, volume.boundingBox.max.y, volume.boundingBox.max.z);
				
				// Plane count
				ImGui::Text("Planes: %zu", volume.planes.size());
				
				// Edit volume bounds
				ImGui::Spacing();
				AABB volumeBounds = volume.boundingBox;
				bool volumeBoundsChanged = false;
				
				if (ImGui::InputFloat3("Vol Min", volumeBounds.min.Ptr()))
				{
					volumeBoundsChanged = true;
				}
				if (ImGui::InputFloat3("Vol Max", volumeBounds.max.Ptr()))
				{
					volumeBoundsChanged = true;
				}
				
				if (volumeBoundsChanged)
				{
					// Recreate the volume with new bounds
					String oldName = volume.name;
					volume = ContainmentVolume::FromAABB(volumeBounds, oldName);
					if (callbacks.onUpdateContainmentVolumes)
					{
						callbacks.onUpdateContainmentVolumes(groupIndex);
					}
				}
				
				// Delete volume button
				ImGui::Spacing();
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 0.8f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.3f, 0.3f, 0.9f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
				if (ImGui::Button("Remove Volume"))
				{
					group->RemoveContainmentVolume(i);
					if (callbacks.onUpdateContainmentVolumes)
					{
						callbacks.onUpdateContainmentVolumes(groupIndex);
					}
					ImGui::PopStyleColor(3);
					ImGui::PopID();
					break; // Exit loop as indices have changed
				}
				ImGui::PopStyleColor(3);
			}
			
			ImGui::PopID();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Delete button
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 0.8f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 0.9f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
		if (ImGui::Button("Delete Group", ImVec2(-1, 0)))
		{
			if (callbacks.onRemoveGroup)
			{
				callbacks.onRemoveGroup(groupIndex);
			}
		}
		ImGui::PopStyleColor(3);
	}

	/// @brief Draws the light properties section.
	static void DrawLightProperties(WorldModel* worldModel, int32 lightIndex, const PropertiesPanelCallbacks& callbacks)
	{
		auto& lights = worldModel->GetLights();
		if (lightIndex < 0 || lightIndex >= static_cast<int32>(lights.size()))
		{
			return;
		}

		auto& light = lights[lightIndex];

		// Light type icon and label
		const char* typeStr = (light.type == WorldModelLight::LightType::Spot) ? "Spot" : "Point";
		ImGui::Text("%s Light %d", typeStr, lightIndex);
		ImGui::Separator();
		ImGui::Spacing();

		// Light type combo
		ImGui::Text("Type:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1);
		int typeInt = (light.type == WorldModelLight::LightType::Spot) ? 1 : 0;
		const char* typeNames[] = { "Point", "Spot" };
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
		if (ImGui::Combo("##lightType", &typeInt, typeNames, 2))
		{
			light.type = (typeInt == 1) ? WorldModelLight::LightType::Spot : WorldModelLight::LightType::Omni;
			if (callbacks.onUpdateLightVisualizations)
			{
				callbacks.onUpdateLightVisualizations();
			}
		}
		ImGui::PopStyleColor();

		ImGui::Spacing();

		// Transform section
		ImGui::Text("Transform");
		ImGui::Separator();

		// Position
		float posArr[3] = { light.position.x, light.position.y, light.position.z };
		if (ImGui::DragFloat3("Position##light", posArr, 0.1f))
		{
			light.position = Vector3(posArr[0], posArr[1], posArr[2]);
			if (callbacks.onUpdateLightSceneNode)
			{
				callbacks.onUpdateLightSceneNode(lightIndex);
			}
			if (callbacks.onUpdateLightVisualizations)
			{
				callbacks.onUpdateLightVisualizations();
			}
		}

		// Direction for spot lights
		if (light.type == WorldModelLight::LightType::Spot)
		{
			Matrix3 rotMat;
			light.rotation.ToRotationMatrix(rotMat);
			Radian yaw, pitch, roll;
			rotMat.ToEulerAnglesYXZ(yaw, pitch, roll);

			float yawDeg = Degree(yaw).GetValueDegrees();
			float pitchDeg = Degree(pitch).GetValueDegrees();

			bool rotChanged = false;
			if (ImGui::DragFloat("Yaw##lightdir", &yawDeg, 1.0f, -180.0f, 180.0f, "%.1f"))
			{
				rotChanged = true;
			}
			if (ImGui::DragFloat("Pitch##lightdir", &pitchDeg, 1.0f, -90.0f, 90.0f, "%.1f"))
			{
				rotChanged = true;
			}

			if (rotChanged)
			{
				Matrix3 newRotMat;
				newRotMat.FromEulerAnglesYXZ(Degree(yawDeg), Degree(pitchDeg), Radian(0));
				light.rotation.FromRotationMatrix(newRotMat);
				if (callbacks.onUpdateLightVisualizations)
				{
					callbacks.onUpdateLightVisualizations();
				}
			}
		}

		ImGui::Spacing();

		// Appearance section
		ImGui::Text("Appearance");
		ImGui::Separator();

		// Color picker
		float colorArr[3] = {
			((light.color >> 16) & 0xFF) / 255.0f,
			((light.color >> 8) & 0xFF) / 255.0f,
			(light.color & 0xFF) / 255.0f
		};
		if (ImGui::ColorEdit3("Color##light", colorArr))
		{
			uint32 r = static_cast<uint32>(colorArr[0] * 255.0f) & 0xFF;
			uint32 g = static_cast<uint32>(colorArr[1] * 255.0f) & 0xFF;
			uint32 b = static_cast<uint32>(colorArr[2] * 255.0f) & 0xFF;
			light.color = 0xFF000000 | (r << 16) | (g << 8) | b;
			if (callbacks.onUpdateLightVisualizations)
			{
				callbacks.onUpdateLightVisualizations();
			}
		}

		// Intensity
		if (ImGui::DragFloat("Intensity##light", &light.intensity, 0.1f, 0.0f, 100.0f, "%.2f"))
		{
			if (callbacks.onUpdateLightVisualizations)
			{
				callbacks.onUpdateLightVisualizations();
			}
		}

		// Range
		if (ImGui::DragFloat("Range##light", &light.attenuationEnd, 0.5f, 0.1f, 1000.0f, "%.1f"))
		{
			if (light.attenuationEnd < 0.1f)
			{
				light.attenuationEnd = 0.1f;
			}
			if (callbacks.onUpdateLightVisualizations)
			{
				callbacks.onUpdateLightVisualizations();
			}
		}

		ImGui::Spacing();

		// Delete button
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 0.8f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 0.9f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
		if (ImGui::Button("Delete Light", ImVec2(-1, 0)))
		{
			if (callbacks.onRemoveLight)
			{
				callbacks.onRemoveLight(lightIndex);
			}
		}
		ImGui::PopStyleColor(3);
	}

	/// @brief Draws the portal properties section.
	static void DrawPortalProperties(WorldModel* worldModel, int32 portalIndex, const PropertiesPanelCallbacks& callbacks)
	{
		const auto& portals = worldModel->GetPortals();
		if (portalIndex < 0 || portalIndex >= static_cast<int32>(portals.size()))
		{
			return;
		}

		auto& portal = portals[portalIndex];
		if (!portal)
		{
			return;
		}

		ImGui::Text("Portal %d", portalIndex);
		ImGui::Separator();
		ImGui::Spacing();

		// Transform section
		ImGui::Text("Transform");
		ImGui::Separator();

		// Position
		Vector3 pos = portal->GetPosition();
		float posArr[3] = { pos.x, pos.y, pos.z };
		if (ImGui::DragFloat3("Position##portal", posArr, 0.1f))
		{
			portal->SetTransform(Vector3(posArr[0], posArr[1], posArr[2]), portal->GetRotation(), portal->GetScale());
			if (callbacks.onUpdatePortalVisualizations)
			{
				callbacks.onUpdatePortalVisualizations();
			}
		}

		// Rotation
		Quaternion rot = portal->GetRotation();
		Matrix3 rotMat;
		rot.ToRotationMatrix(rotMat);
		Radian yaw, pitch, roll;
		rotMat.ToEulerAnglesYXZ(yaw, pitch, roll);

		float yawDeg = Degree(yaw).GetValueDegrees();
		float pitchDeg = Degree(pitch).GetValueDegrees();
		float rollDeg = Degree(roll).GetValueDegrees();

		bool rotChanged = false;
		if (ImGui::DragFloat("Yaw##portal", &yawDeg, 1.0f, -180.0f, 180.0f, "%.1f"))
		{
			rotChanged = true;
		}
		if (ImGui::DragFloat("Pitch##portal", &pitchDeg, 1.0f, -90.0f, 90.0f, "%.1f"))
		{
			rotChanged = true;
		}
		if (ImGui::DragFloat("Roll##portal", &rollDeg, 1.0f, -180.0f, 180.0f, "%.1f"))
		{
			rotChanged = true;
		}

		if (rotChanged)
		{
			Matrix3 newRotMat;
			newRotMat.FromEulerAnglesYXZ(Degree(yawDeg), Degree(pitchDeg), Degree(rollDeg));
			Quaternion newRot;
			newRot.FromRotationMatrix(newRotMat);
			portal->SetTransform(portal->GetPosition(), newRot, portal->GetScale());
			if (callbacks.onUpdatePortalVisualizations)
			{
				callbacks.onUpdatePortalVisualizations();
			}
		}

		ImGui::Spacing();

		// Dimensions section
		ImGui::Text("Dimensions");
		ImGui::Separator();

		float width = portal->GetWidth();
		float height = portal->GetHeight();
		if (ImGui::DragFloat("Width##portal", &width, 0.1f, 0.1f, 100.0f, "%.2f"))
		{
			if (width > 0.0f)
			{
				portal->SetDimensions(width, height);
				if (callbacks.onUpdatePortalVisualizations)
				{
					callbacks.onUpdatePortalVisualizations();
				}
			}
		}
		if (ImGui::DragFloat("Height##portal", &height, 0.1f, 0.1f, 100.0f, "%.2f"))
		{
			if (height > 0.0f)
			{
				portal->SetDimensions(width, height);
				if (callbacks.onUpdatePortalVisualizations)
				{
					callbacks.onUpdatePortalVisualizations();
				}
			}
		}

		ImGui::Spacing();

		// Settings section
		ImGui::Text("Settings");
		ImGui::Separator();

		// Portal type
		PortalType ptype = portal->GetPortalType();
		int ptypeInt = static_cast<int>(ptype);
		const char* typeNames[] = { "One-Way", "Two-Way" };
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
		if (ImGui::Combo("Type##portal", &ptypeInt, typeNames, 2))
		{
			portal->SetPortalType(static_cast<PortalType>(ptypeInt));
		}
		ImGui::PopStyleColor();

		// Active state
		bool active = portal->IsActive();
		if (ImGui::Checkbox("Active##portal", &active))
		{
			portal->SetActive(active);
		}

		ImGui::Spacing();

		// Connected groups section
		ImGui::Text("Connected Groups");
		ImGui::Separator();

		// Find which groups reference this portal
		int32 groupA = -1, groupB = -1;
		for (size_t gi = 0; gi < worldModel->GetGroupCount(); ++gi)
		{
			auto* grp = worldModel->GetGroup(gi);
			if (grp)
			{
				for (const auto& ref : grp->GetPortalRefs())
				{
					if (ref.portalIndex == static_cast<uint16>(portalIndex))
					{
						if (groupA < 0)
						{
							groupA = static_cast<int32>(gi);
						}
						else
						{
							groupB = static_cast<int32>(gi);
						}
						break;
					}
				}
			}
		}

		// Group A selection
		String groupAPreview = "(none)";
		if (groupA >= 0 && groupA < static_cast<int32>(worldModel->GetGroupCount()))
		{
			const auto* grpA = worldModel->GetGroup(groupA);
			if (grpA)
			{
				groupAPreview = grpA->GetName().empty() ? "Group " + std::to_string(groupA) : grpA->GetName();
			}
		}

		ImGui::Text("Group A:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
		if (ImGui::BeginCombo("##portalGrpA", groupAPreview.c_str()))
		{
			for (size_t gi = 0; gi < worldModel->GetGroupCount(); ++gi)
			{
				const auto* grp = worldModel->GetGroup(gi);
				if (grp && static_cast<int32>(gi) != groupB)
				{
					String itemLabel = grp->GetName().empty() ? "Group " + std::to_string(gi) : grp->GetName();
					itemLabel += "##grpA" + std::to_string(gi);

					if (ImGui::Selectable(itemLabel.c_str(), groupA == static_cast<int32>(gi)))
					{
						if (callbacks.onUpdatePortalGroupConnection)
						{
							callbacks.onUpdatePortalGroupConnection(portalIndex, groupA, static_cast<int32>(gi), groupB);
						}
					}
				}
			}
			ImGui::EndCombo();
		}
		ImGui::PopStyleColor();

		// Group B selection
		String groupBPreview = "(none)";
		if (groupB >= 0 && groupB < static_cast<int32>(worldModel->GetGroupCount()))
		{
			const auto* grpB = worldModel->GetGroup(groupB);
			if (grpB)
			{
				groupBPreview = grpB->GetName().empty() ? "Group " + std::to_string(groupB) : grpB->GetName();
			}
		}

		ImGui::Text("Group B:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
		if (ImGui::BeginCombo("##portalGrpB", groupBPreview.c_str()))
		{
			for (size_t gi = 0; gi < worldModel->GetGroupCount(); ++gi)
			{
				const auto* grp = worldModel->GetGroup(gi);
				if (grp && static_cast<int32>(gi) != groupA)
				{
					String itemLabel = grp->GetName().empty() ? "Group " + std::to_string(gi) : grp->GetName();
					itemLabel += "##grpB" + std::to_string(gi);

					if (ImGui::Selectable(itemLabel.c_str(), groupB == static_cast<int32>(gi)))
					{
						if (callbacks.onUpdatePortalGroupConnection)
						{
							callbacks.onUpdatePortalGroupConnection(portalIndex, groupA, groupA, static_cast<int32>(gi));
						}
					}
				}
			}
			ImGui::EndCombo();
		}
		ImGui::PopStyleColor();

		ImGui::Spacing();

		// Delete button
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 0.8f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 0.9f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
		if (ImGui::Button("Delete Portal", ImVec2(-1, 0)))
		{
			if (callbacks.onRemovePortal)
			{
				callbacks.onRemovePortal(portalIndex);
			}
		}
		ImGui::PopStyleColor(3);
	}

	/// @brief Draws the mesh ref properties section.
	static void DrawMeshRefProperties(WorldModel* worldModel, int32 groupIndex, int32 meshRefIndex, const PropertiesPanelCallbacks& callbacks)
	{
		if (groupIndex < 0 || groupIndex >= static_cast<int32>(worldModel->GetGroupCount()))
		{
			return;
		}

		auto* group = worldModel->GetGroup(groupIndex);
		if (!group)
		{
			return;
		}

		const auto& meshRefs = group->GetMeshRefs();
		if (meshRefIndex < 0 || meshRefIndex >= static_cast<int32>(meshRefs.size()))
		{
			return;
		}

		auto* selectedRef = group->GetMeshRef(meshRefIndex);
		if (!selectedRef)
		{
			return;
		}

		// Get display label
		String label = selectedRef->name;
		if (label.empty())
		{
			label = selectedRef->meshPath;
			size_t lastSlash = label.find_last_of("/\\");
			if (lastSlash != String::npos)
			{
				label = label.substr(lastSlash + 1);
			}
		}

		ImGui::Text("Mesh: %s", label.c_str());
		ImGui::Separator();
		ImGui::Spacing();

		// Name field
		ImGui::Text("Name:");
		ImGui::SameLine();
		char nameBuffer[256];
		std::strncpy(nameBuffer, selectedRef->name.c_str(), sizeof(nameBuffer) - 1);
		nameBuffer[sizeof(nameBuffer) - 1] = '\0';
		ImGui::SetNextItemWidth(-1);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
		if (ImGui::InputText("##meshrefName", nameBuffer, sizeof(nameBuffer)))
		{
			selectedRef->name = nameBuffer;
		}
		ImGui::PopStyleColor();

		ImGui::Spacing();

		// Transform section
		ImGui::Text("Transform");
		ImGui::Separator();

		// Position
		float pos[3] = { selectedRef->position.x, selectedRef->position.y, selectedRef->position.z };
		if (ImGui::DragFloat3("Position##meshref", pos, 0.1f))
		{
			selectedRef->position = Vector3(pos[0], pos[1], pos[2]);
			if (callbacks.onUpdateMeshRefPosition)
			{
				callbacks.onUpdateMeshRefPosition(groupIndex, meshRefIndex, selectedRef->position);
			}
		}

		// Scale
		float scl[3] = { selectedRef->scale.x, selectedRef->scale.y, selectedRef->scale.z };
		if (ImGui::DragFloat3("Scale##meshref", scl, 0.01f, 0.01f, 100.0f))
		{
			selectedRef->scale = Vector3(scl[0], scl[1], scl[2]);
			if (callbacks.onUpdateMeshRefScale)
			{
				callbacks.onUpdateMeshRefScale(groupIndex, meshRefIndex, selectedRef->scale);
			}
		}

		ImGui::Spacing();

		// Material section
		ImGui::Text("Material Override");
		ImGui::Separator();
		char matBuffer[512];
		std::strncpy(matBuffer, selectedRef->materialOverride.c_str(), sizeof(matBuffer) - 1);
		matBuffer[sizeof(matBuffer) - 1] = '\0';
		ImGui::SetNextItemWidth(-1);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
		if (ImGui::InputText("##meshrefMat", matBuffer, sizeof(matBuffer)))
		{
			selectedRef->materialOverride = matBuffer;
			if (callbacks.onUpdateMeshRefMaterial)
			{
				callbacks.onUpdateMeshRefMaterial(groupIndex, meshRefIndex, selectedRef->materialOverride);
			}
		}
		ImGui::PopStyleColor();

		ImGui::Spacing();

		// Source mesh (read-only)
		ImGui::TextDisabled("Source: %s", selectedRef->meshPath.c_str());

		ImGui::Spacing();

		// Delete button
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 0.8f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 0.9f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
		if (ImGui::Button("Delete Mesh", ImVec2(-1, 0)))
		{
			if (callbacks.onRemoveMeshRef)
			{
				callbacks.onRemoveMeshRef(groupIndex, static_cast<size_t>(meshRefIndex));
			}
		}
		ImGui::PopStyleColor(3);
	}

	void DrawPropertiesPanel(
		WorldModel* worldModel,
		const PropertiesSelectionState& selectionState,
		const PropertiesPanelCallbacks& callbacks)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 6));

		if (ImGui::Button("Save", ImVec2(-1, 0)))
		{
			if (callbacks.onSave)
			{
				callbacks.onSave();
			}
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (!worldModel)
		{
			ImGui::TextDisabled("No world model loaded");
			ImGui::PopStyleVar(2);
			return;
		}

		switch (selectionState.type)
		{
		case PropertiesSelectionType::Group:
			DrawGroupProperties(worldModel, selectionState.selectedGroupIndex, callbacks);
			break;

		case PropertiesSelectionType::Light:
			DrawLightProperties(worldModel, selectionState.selectedLightIndex, callbacks);
			break;

		case PropertiesSelectionType::Portal:
			DrawPortalProperties(worldModel, selectionState.selectedPortalIndex, callbacks);
			break;

		case PropertiesSelectionType::MeshRef:
			// Only show for single selection
			if (selectionState.selectedMeshRefIndices.size() == 1)
			{
				DrawMeshRefProperties(worldModel, selectionState.selectedGroupIndex, selectionState.selectedMeshRefIndex, callbacks);
			}
			else if (selectionState.selectedMeshRefIndices.size() > 1)
			{
				ImGui::TextDisabled("%zu meshes selected", selectionState.selectedMeshRefIndices.size());
				ImGui::TextDisabled("Select a single mesh to view properties");
			}
			else
			{
				ImGui::TextDisabled("No mesh selected");
			}
			break;

		case PropertiesSelectionType::None:
		default:
			ImGui::TextDisabled("No selection");
			ImGui::TextDisabled("Select a group, light, portal, or mesh");
			ImGui::TextDisabled("to view its properties.");
			break;
		}

		ImGui::PopStyleVar(2);
	}

}
