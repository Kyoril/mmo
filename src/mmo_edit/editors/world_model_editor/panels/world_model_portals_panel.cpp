// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_portals_panel.h"

#include "scene_graph/world_model.h"

#include <imgui.h>

namespace mmo
{
	void DrawPortalsPanel(
		WorldModel* worldModel,
		int32& selectedPortalIndex,
		PortalCreationState& creationState,
		const PortalsPanelCallbacks& callbacks)
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
		const auto& portals = worldModel->GetPortals();
		ImGui::Text("Portals");
		ImGui::SameLine();
		ImGui::TextDisabled("(%zu)", portals.size());

		ImGui::Spacing();

		// Portal creation UI
		if (!creationState.creatingPortal)
		{
			if (worldModel->GetGroupCount() < 2)
			{
				ImGui::TextDisabled("Need at least 2 groups to create a portal");
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.3f, 0.8f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.4f, 0.9f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.8f, 0.5f, 1.0f));
				if (ImGui::Button("+ Create Portal...", ImVec2(-1, 0)))
				{
					creationState.creatingPortal = true;
					creationState.sourceGroup = -1;
					creationState.targetGroup = -1;
				}
				ImGui::PopStyleColor(3);
			}
		}
		else
		{
			// Portal creation wizard
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.15f, 0.18f, 1.0f));
			ImGui::BeginChild("PortalCreation", ImVec2(-1, 130), true);

			ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Creating New Portal");
			ImGui::Separator();
			ImGui::Spacing();

			// Source group selection
			ImGui::Text("Source Group:");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1);

			String sourcePreview = "Select...";
			if (creationState.sourceGroup >= 0 && creationState.sourceGroup < static_cast<int32>(worldModel->GetGroupCount()))
			{
				const auto* srcGroup = worldModel->GetGroup(creationState.sourceGroup);
				if (srcGroup)
				{
					sourcePreview = srcGroup->GetName().empty() ? "(unnamed)" : srcGroup->GetName();
				}
			}

			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
			if (ImGui::BeginCombo("##SourceGroup", sourcePreview.c_str()))
			{
				for (size_t i = 0; i < worldModel->GetGroupCount(); ++i)
				{
					const auto* group = worldModel->GetGroup(i);
					if (group && static_cast<int32>(i) != creationState.targetGroup)
					{
						bool isSelected = creationState.sourceGroup == static_cast<int32>(i);
						String itemLabel = group->GetName().empty() ? "(unnamed)" : group->GetName();
						itemLabel += "##src" + std::to_string(i);
						if (ImGui::Selectable(itemLabel.c_str(), isSelected))
						{
							creationState.sourceGroup = static_cast<int32>(i);
						}
					}
				}
				ImGui::EndCombo();
			}
			ImGui::PopStyleColor();

			// Target group selection
			ImGui::Text("Target Group:");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1);

			String targetPreview = "Select...";
			if (creationState.targetGroup >= 0 && creationState.targetGroup < static_cast<int32>(worldModel->GetGroupCount()))
			{
				const auto* tgtGroup = worldModel->GetGroup(creationState.targetGroup);
				if (tgtGroup)
				{
					targetPreview = tgtGroup->GetName().empty() ? "(unnamed)" : tgtGroup->GetName();
				}
			}

			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
			if (ImGui::BeginCombo("##TargetGroup", targetPreview.c_str()))
			{
				for (size_t i = 0; i < worldModel->GetGroupCount(); ++i)
				{
					const auto* group = worldModel->GetGroup(i);
					if (group && static_cast<int32>(i) != creationState.sourceGroup)
					{
						bool isSelected = creationState.targetGroup == static_cast<int32>(i);
						String itemLabel = group->GetName().empty() ? "(unnamed)" : group->GetName();
						itemLabel += "##tgt" + std::to_string(i);
						if (ImGui::Selectable(itemLabel.c_str(), isSelected))
						{
							creationState.targetGroup = static_cast<int32>(i);
						}
					}
				}
				ImGui::EndCombo();
			}
			ImGui::PopStyleColor();

			ImGui::Spacing();

			// Buttons
			bool canCreate = creationState.sourceGroup >= 0 && creationState.targetGroup >= 0;
			if (!canCreate)
			{
				ImGui::BeginDisabled();
			}

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.3f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.4f, 0.9f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.8f, 0.5f, 1.0f));
			if (ImGui::Button("Create", ImVec2(100, 0)))
			{
				const auto* srcGroup = worldModel->GetGroup(creationState.sourceGroup);
				const auto* tgtGroup = worldModel->GetGroup(creationState.targetGroup);

				if (srcGroup && tgtGroup && callbacks.onCreatePortal)
				{
					Vector3 srcCenter = srcGroup->GetBoundingBox().GetCenter();
					Vector3 tgtCenter = tgtGroup->GetBoundingBox().GetCenter();
					Vector3 portalCenter = (srcCenter + tgtCenter) * 0.5f;

					Vector3 direction = (tgtCenter - srcCenter).NormalizedCopy();
					Vector3 up = Vector3::UnitY;
					Vector3 right = direction.Cross(up).NormalizedCopy();
					if (right.GetSquaredLength() < 0.001f)
					{
						right = Vector3::UnitX;
					}
					up = right.Cross(direction).NormalizedCopy();

					float halfWidth = 1.0f;
					float halfHeight = 1.0f;

					std::vector<Vector3> vertices;
					vertices.push_back(portalCenter - right * halfWidth - up * halfHeight);
					vertices.push_back(portalCenter + right * halfWidth - up * halfHeight);
					vertices.push_back(portalCenter + right * halfWidth + up * halfHeight);
					vertices.push_back(portalCenter - right * halfWidth + up * halfHeight);

					callbacks.onCreatePortal(creationState.sourceGroup, creationState.targetGroup, vertices);
				}

				creationState.creatingPortal = false;
				creationState.sourceGroup = -1;
				creationState.targetGroup = -1;
			}
			ImGui::PopStyleColor(3);

			if (!canCreate)
			{
				ImGui::EndDisabled();
			}

			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(100, 0)))
			{
				creationState.creatingPortal = false;
				creationState.sourceGroup = -1;
				creationState.targetGroup = -1;
			}

			ImGui::EndChild();
			ImGui::PopStyleColor();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Portal list with collapsible header
		if (ImGui::CollapsingHeader("Portal List", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();

			if (portals.empty())
			{
				ImGui::TextDisabled("No portals defined");
			}
			else
			{
				int visibleCount = 0;
				for (size_t i = 0; i < portals.size(); ++i)
				{
					visibleCount++;
					ImGui::PushID(static_cast<int>(i));

					bool isSelected = selectedPortalIndex == static_cast<int32>(i);

					// Alternating row colors
					if (visibleCount % 2 == 0)
					{
						ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.18f, 0.2f, 1.0f));
					}
					else
					{
						ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.15f, 0.15f, 0.17f, 1.0f));
					}

					String label = "Portal " + std::to_string(i);
					if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns))
					{
						selectedPortalIndex = static_cast<int32>(i);
						if (callbacks.onSelectPortal)
						{
							callbacks.onSelectPortal(selectedPortalIndex);
						}
					}

					ImGui::PopStyleColor();

					// Context menu
					if (ImGui::BeginPopupContextItem())
					{
						if (portals[i] && ImGui::MenuItem("Focus (F)"))
						{
							if (callbacks.onFocusPosition)
							{
								callbacks.onFocusPosition(portals[i]->GetPosition());
							}
						}
						ImGui::Separator();
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
						if (ImGui::MenuItem("Delete"))
						{
							if (callbacks.onRemovePortal)
							{
								callbacks.onRemovePortal(static_cast<int32>(i));
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
					if (ImGui::IsItemHovered() && portals[i])
					{
						ImGui::BeginTooltip();
						Vector3 pos = portals[i]->GetPosition();
						ImGui::Text("Position: %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);
						ImGui::Text("Size: %.2fx%.2f", portals[i]->GetWidth(), portals[i]->GetHeight());
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
