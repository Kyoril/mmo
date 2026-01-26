// Copyright (c) 2019 - 2025. Mmo-Dev. All rights reserved.
// Licensed under the MIT License. See LICENSE file for full license information.

#include "world_model_lights_panel.h"

#include "imgui.h"

namespace mmo
{
	void DrawLightsPanel(
		WorldModel* worldModel,
		LightsPanelState& state,
		const LightsPanelCallbacks& callbacks)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 6));

		if (!worldModel)
		{
			ImGui::TextDisabled("No world model loaded");
			ImGui::PopStyleVar(2);
			return;
		}

		// Header with count
		auto& lights = worldModel->GetLights();
		ImGui::Text("Lights");
		ImGui::SameLine();
		ImGui::TextDisabled("(%zu)", lights.size());

		ImGui::Spacing();

		// Add light buttons with styled colors
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.3f, 0.8f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.4f, 0.9f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.8f, 0.5f, 1.0f));
		if (ImGui::Button("+ Point Light", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 3, 0)))
		{
			if (callbacks.onCreateLight)
			{
				callbacks.onCreateLight(-1, Vector3::Zero, 0xFFFFFFFF, 1.0f, WorldModelLight::LightType::Omni);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("+ Spot Light", ImVec2(-1, 0)))
		{
			if (callbacks.onCreateLight)
			{
				callbacks.onCreateLight(-1, Vector3::Zero, 0xFFFFFFFF, 1.0f, WorldModelLight::LightType::Spot);
			}
		}
		ImGui::PopStyleColor(3);

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Lights list with collapsible header
		if (ImGui::CollapsingHeader("Light List", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();

			if (lights.empty())
			{
				ImGui::TextDisabled("No lights defined");
			}
			else
			{
				int visibleCount = 0;
				for (size_t i = 0; i < lights.size(); ++i)
				{
					visibleCount++;
					ImGui::PushID(static_cast<int>(i));

					bool isSelected = state.selectedLightIndex == static_cast<int32>(i);

					// Alternating row colors
					if (visibleCount % 2 == 0)
					{
						ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.18f, 0.2f, 1.0f));
					}
					else
					{
						ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.15f, 0.15f, 0.17f, 1.0f));
					}

					// Light type icon and label
					const char* typeStr = (lights[i].type == WorldModelLight::LightType::Spot) ? "Spot" : "Point";
					String label = String(typeStr) + " Light " + std::to_string(i);

					if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns))
					{
						state.selectedLightIndex = static_cast<int32>(i);
						if (callbacks.onSelectLight)
						{
							callbacks.onSelectLight(state.selectedLightIndex);
						}
					}

					ImGui::PopStyleColor();

					// Context menu
					if (ImGui::BeginPopupContextItem())
					{
						if (ImGui::MenuItem("Focus (F)"))
						{
							if (callbacks.onFocusPosition)
							{
								callbacks.onFocusPosition(lights[i].position);
							}
						}
						ImGui::Separator();
						if (ImGui::MenuItem("Duplicate"))
						{
							if (callbacks.onDuplicateLight)
							{
								callbacks.onDuplicateLight(i);
							}
						}
						ImGui::Separator();
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
						if (ImGui::MenuItem("Delete"))
						{
							if (callbacks.onRemoveLight)
							{
								callbacks.onRemoveLight(static_cast<int32>(i));
							}
							ImGui::PopStyleColor();
							ImGui::EndPopup();
							ImGui::PopID();
							break;
						}
						ImGui::PopStyleColor();
						ImGui::EndPopup();
					}

					// Tooltip
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Type: %s", typeStr);
						ImGui::Text("Position: %.2f, %.2f, %.2f", lights[i].position.x, lights[i].position.y, lights[i].position.z);
						ImGui::Text("Intensity: %.2f", lights[i].intensity);
						ImGui::EndTooltip();
					}

					ImGui::PopID();
				}
			}

			ImGui::Unindent();
		}

		// Selected light properties
		if (state.selectedLightIndex >= 0 && state.selectedLightIndex < static_cast<int32>(lights.size()))
		{
			ImGui::Spacing();

			if (ImGui::CollapsingHeader("Selected Light Properties", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Indent();

				auto& light = lights[state.selectedLightIndex];

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
					if (callbacks.onLightChanged)
					{
						callbacks.onLightChanged();
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
					if (callbacks.onLightChanged)
					{
						callbacks.onLightChanged();
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
						if (callbacks.onLightChanged)
						{
							callbacks.onLightChanged();
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
					if (callbacks.onLightChanged)
					{
						callbacks.onLightChanged();
					}
				}

				// Intensity
				if (ImGui::DragFloat("Intensity##light", &light.intensity, 0.1f, 0.0f, 100.0f, "%.2f"))
				{
					if (callbacks.onLightChanged)
					{
						callbacks.onLightChanged();
					}
				}

				// Range
				if (ImGui::DragFloat("Range##light", &light.attenuationEnd, 0.5f, 0.1f, 1000.0f, "%.1f"))
				{
					if (light.attenuationEnd < 0.1f)
					{
						light.attenuationEnd = 0.1f;
					}
					if (callbacks.onLightChanged)
					{
						callbacks.onLightChanged();
					}
				}

				ImGui::Spacing();

				// Delete button
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 0.8f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 0.9f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
				if (ImGui::Button("Delete Selected", ImVec2(-1, 0)))
				{
					if (callbacks.onRemoveLight)
					{
						callbacks.onRemoveLight(state.selectedLightIndex);
					}
				}
				ImGui::PopStyleColor(3);

				ImGui::Unindent();
			}
		}

		ImGui::PopStyleVar(2);
	}

}
