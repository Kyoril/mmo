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

		ImGui::PopStyleVar(2);
	}

}
