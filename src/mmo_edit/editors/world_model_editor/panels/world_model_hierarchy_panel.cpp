// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_hierarchy_panel.h"
#include "../world_model_editor_instance.h"

#include <imgui.h>
#include <algorithm>
#include <cstring>

namespace mmo
{
	/// @brief Helper to check if a string matches the search filter (case-insensitive).
	static bool MatchesFilter(const String& text, const String& filter)
	{
		if (filter.empty())
		{
			return true;
		}

		String textLower = text;
		String filterLower = filter;
		std::transform(textLower.begin(), textLower.end(), textLower.begin(), ::tolower);
		std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
		return textLower.find(filterLower) != String::npos;
	}

	/// @brief Draws the groups section with mesh refs as children.
	static void DrawGroupsSection(
		WorldModel* worldModel,
		std::vector<GroupVisualization>& groupVisualizations,
		HierarchyPanelState& state,
		const HierarchyPanelCallbacks& callbacks,
		const String& filter)
	{
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.4f, 0.6f, 0.6f));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.3f, 0.5f, 0.7f, 0.7f));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.4f, 0.6f, 0.8f, 0.8f));

		bool groupsSectionOpen = ImGui::CollapsingHeader("Groups", ImGuiTreeNodeFlags_DefaultOpen);
		
		// Context menu for Groups header
		if (ImGui::BeginPopupContextItem("GroupsHeaderContext"))
		{
			if (ImGui::MenuItem("New Group"))
			{
				state.showNewGroupDialog = true;
				std::memset(state.newGroupName, 0, sizeof(state.newGroupName));
			}
			ImGui::EndPopup();
		}

		ImGui::PopStyleColor(3);

		if (groupsSectionOpen)
		{
			ImGui::Indent();

			if (worldModel->GetGroupCount() == 0)
			{
				ImGui::TextDisabled("No groups");
			}

			for (size_t gi = 0; gi < worldModel->GetGroupCount(); ++gi)
			{
				auto* group = worldModel->GetGroup(gi);
				if (!group)
				{
					continue;
				}

				String groupName = group->GetName();
				if (groupName.empty())
				{
					groupName = "Group " + std::to_string(gi);
				}

				// Check if this group or any of its meshes match the filter
				bool groupMatchesFilter = MatchesFilter(groupName, filter);
				bool anyMeshMatchesFilter = false;

				const auto& meshRefs = group->GetMeshRefs();
				for (size_t mi = 0; mi < meshRefs.size() && !anyMeshMatchesFilter; ++mi)
				{
					String meshName = meshRefs[mi].name.empty() ? meshRefs[mi].meshPath : meshRefs[mi].name;
					if (MatchesFilter(meshName, filter))
					{
						anyMeshMatchesFilter = true;
					}
				}

				if (!groupMatchesFilter && !anyMeshMatchesFilter)
				{
					continue;
				}

				ImGui::PushID(static_cast<int>(gi));

				bool isGroupSelected = (state.selectedType == HierarchyItemType::Group && 
					state.selectedGroupIndex == static_cast<int32>(gi));
				bool isExpanded = state.expandedGroups.count(static_cast<int32>(gi)) > 0;

				// Build node flags
				ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | 
					ImGuiTreeNodeFlags_OpenOnDoubleClick |
					ImGuiTreeNodeFlags_SpanAvailWidth;

				if (isGroupSelected)
				{
					nodeFlags |= ImGuiTreeNodeFlags_Selected;
				}

				if (meshRefs.empty())
				{
					nodeFlags |= ImGuiTreeNodeFlags_Leaf;
				}

				if (isExpanded || anyMeshMatchesFilter)
				{
					nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
				}

				// Group label with mesh count
				String label = groupName;
				if (group->IsInterior())
				{
					label += " [Int]";
				}
				else if (group->IsExterior())
				{
					label += " [Ext]";
				}
				label += " (" + std::to_string(meshRefs.size()) + ")";

				bool nodeOpen = ImGui::TreeNodeEx(label.c_str(), nodeFlags);

				// Track expansion state
				if (nodeOpen && !isExpanded)
				{
					state.expandedGroups.insert(static_cast<int32>(gi));
				}
				else if (!nodeOpen && isExpanded)
				{
					state.expandedGroups.erase(static_cast<int32>(gi));
				}

				// Handle selection on click (not arrow)
				if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
				{
					state.selectedType = HierarchyItemType::Group;
					state.selectedGroupIndex = static_cast<int32>(gi);
					state.selectedMeshRefIndex = -1;
					state.selectedMeshRefIndices.clear();
					state.selectedLightIndex = -1;
					state.selectedPortalIndex = -1;

					if (callbacks.onSelectGroup)
					{
						callbacks.onSelectGroup(state.selectedGroupIndex);
					}
				}

				// Visibility toggle on same line
				ImGui::SameLine(ImGui::GetWindowWidth() - 40);
				if (gi < groupVisualizations.size())
				{
					bool visible = groupVisualizations[gi].visible;
					if (ImGui::Checkbox("##gvis", &visible))
					{
						groupVisualizations[gi].visible = visible;
						if (callbacks.onSetGroupVisibility)
						{
							callbacks.onSetGroupVisibility(gi, visible);
						}
					}
				}

				// Context menu for group
				if (ImGui::BeginPopupContextItem())
				{
					if (ImGui::MenuItem("Add Mesh..."))
					{
						if (callbacks.onAddMeshToGroup)
						{
							callbacks.onAddMeshToGroup(static_cast<int32>(gi));
						}
					}
					ImGui::Separator();
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
					if (ImGui::MenuItem("Delete Group"))
					{
						if (callbacks.onRemoveGroup)
						{
							callbacks.onRemoveGroup(gi);
						}
					}
					ImGui::PopStyleColor();
					ImGui::EndPopup();
				}

				// Draw mesh children if node is open
				if (nodeOpen)
				{
					for (size_t mi = 0; mi < meshRefs.size(); ++mi)
					{
						const auto& meshRef = meshRefs[mi];

						String meshName = meshRef.name;
						if (meshName.empty())
						{
							meshName = meshRef.meshPath;
							size_t lastSlash = meshName.find_last_of("/\\");
							if (lastSlash != String::npos)
							{
								meshName = meshName.substr(lastSlash + 1);
							}
						}

						// Filter check
						if (!filter.empty() && !MatchesFilter(meshName, filter) && !groupMatchesFilter)
						{
							continue;
						}

						ImGui::PushID(static_cast<int>(mi));

						bool isMeshSelected = (state.selectedType == HierarchyItemType::MeshRef &&
							state.selectedGroupIndex == static_cast<int32>(gi) &&
							state.selectedMeshRefIndices.count(mi) > 0);

						ImGuiTreeNodeFlags meshFlags = ImGuiTreeNodeFlags_Leaf | 
							ImGuiTreeNodeFlags_NoTreePushOnOpen |
							ImGuiTreeNodeFlags_SpanAvailWidth;

						if (isMeshSelected)
						{
							meshFlags |= ImGuiTreeNodeFlags_Selected;
						}

						ImGui::TreeNodeEx(meshName.c_str(), meshFlags);

						// Handle mesh selection
						if (ImGui::IsItemClicked())
						{
							const bool ctrlPressed = ImGui::GetIO().KeyCtrl;

							if (ctrlPressed && state.selectedType == HierarchyItemType::MeshRef &&
								state.selectedGroupIndex == static_cast<int32>(gi))
							{
								// Toggle selection
								if (state.selectedMeshRefIndices.count(mi) > 0)
								{
									state.selectedMeshRefIndices.erase(mi);
									if (state.selectedMeshRefIndices.empty())
									{
										state.selectedMeshRefIndex = -1;
									}
								}
								else
								{
									state.selectedMeshRefIndices.insert(mi);
									state.selectedMeshRefIndex = static_cast<int32>(mi);
								}
							}
							else
							{
								// Single selection
								state.selectedType = HierarchyItemType::MeshRef;
								state.selectedGroupIndex = static_cast<int32>(gi);
								state.selectedMeshRefIndex = static_cast<int32>(mi);
								state.selectedMeshRefIndices.clear();
								state.selectedMeshRefIndices.insert(mi);
								state.selectedLightIndex = -1;
								state.selectedPortalIndex = -1;
							}

							if (callbacks.onSelectMeshRef)
							{
								callbacks.onSelectMeshRef(state.selectedGroupIndex, state.selectedMeshRefIndex);
							}
						}

						// Visibility toggle
						ImGui::SameLine(ImGui::GetWindowWidth() - 40);
						if (gi < groupVisualizations.size() && mi < groupVisualizations[gi].meshRefVisualizations.size())
						{
							bool visible = groupVisualizations[gi].meshRefVisualizations[mi].visible;
							if (ImGui::Checkbox("##mvis", &visible))
							{
								groupVisualizations[gi].meshRefVisualizations[mi].visible = visible;
								if (callbacks.onSetMeshRefVisibility)
								{
									callbacks.onSetMeshRefVisibility(static_cast<int32>(gi), mi, visible);
								}
							}
						}

						// Context menu for mesh
						if (ImGui::BeginPopupContextItem())
						{
							if (ImGui::MenuItem("Focus"))
							{
								if (callbacks.onFocusPosition)
								{
									callbacks.onFocusPosition(meshRef.position);
								}
							}
							ImGui::Separator();
							if (ImGui::MenuItem("Duplicate"))
							{
								if (callbacks.onDuplicateMeshRef)
								{
									callbacks.onDuplicateMeshRef(static_cast<int32>(gi), mi);
								}
							}
							ImGui::Separator();
							ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
							if (ImGui::MenuItem("Delete"))
							{
								if (callbacks.onRemoveMeshRef)
								{
									callbacks.onRemoveMeshRef(static_cast<int32>(gi), mi);
								}
							}
							ImGui::PopStyleColor();
							ImGui::EndPopup();
						}

						// Tooltip
						if (ImGui::IsItemHovered())
						{
							ImGui::BeginTooltip();
							ImGui::Text("Path: %s", meshRef.meshPath.c_str());
							ImGui::Text("Position: %.2f, %.2f, %.2f", meshRef.position.x, meshRef.position.y, meshRef.position.z);
							ImGui::EndTooltip();
						}

						ImGui::PopID();
					}

					ImGui::TreePop();
				}

				ImGui::PopID();
			}

			ImGui::Unindent();
		}
	}

	/// @brief Draws the lights section.
	static void DrawLightsSection(
		WorldModel* worldModel,
		HierarchyPanelState& state,
		const HierarchyPanelCallbacks& callbacks,
		const String& filter)
	{
		auto& lights = worldModel->GetLights();

		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.6f, 0.5f, 0.2f, 0.6f));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.7f, 0.6f, 0.3f, 0.7f));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.8f, 0.7f, 0.4f, 0.8f));

		String lightsHeader = "Lights (" + std::to_string(lights.size()) + ")";
		bool lightsSectionOpen = ImGui::CollapsingHeader(lightsHeader.c_str(), 
			state.lightsExpanded ? ImGuiTreeNodeFlags_DefaultOpen : 0);
		state.lightsExpanded = lightsSectionOpen;

		// Context menu for Lights header
		if (ImGui::BeginPopupContextItem("LightsHeaderContext"))
		{
			if (ImGui::MenuItem("Add Point Light"))
			{
				if (callbacks.onCreateLight)
				{
					callbacks.onCreateLight(WorldModelLight::LightType::Omni);
				}
			}
			if (ImGui::MenuItem("Add Spot Light"))
			{
				if (callbacks.onCreateLight)
				{
					callbacks.onCreateLight(WorldModelLight::LightType::Spot);
				}
			}
			ImGui::EndPopup();
		}

		ImGui::PopStyleColor(3);

		if (lightsSectionOpen)
		{
			ImGui::Indent();

			if (lights.empty())
			{
				ImGui::TextDisabled("No lights");
			}

			for (size_t i = 0; i < lights.size(); ++i)
			{
				const auto& light = lights[i];
				const char* typeStr = (light.type == WorldModelLight::LightType::Spot) ? "Spot" : "Point";
				String label = String(typeStr) + " Light " + std::to_string(i);

				if (!MatchesFilter(label, filter))
				{
					continue;
				}

				ImGui::PushID(static_cast<int>(i));

				bool isSelected = (state.selectedType == HierarchyItemType::Light &&
					state.selectedLightIndex == static_cast<int32>(i));

				ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | 
					ImGuiTreeNodeFlags_NoTreePushOnOpen |
					ImGuiTreeNodeFlags_SpanAvailWidth;

				if (isSelected)
				{
					flags |= ImGuiTreeNodeFlags_Selected;
				}

				ImGui::TreeNodeEx(label.c_str(), flags);

				if (ImGui::IsItemClicked())
				{
					state.selectedType = HierarchyItemType::Light;
					state.selectedLightIndex = static_cast<int32>(i);
					state.selectedGroupIndex = -1;
					state.selectedMeshRefIndex = -1;
					state.selectedMeshRefIndices.clear();
					state.selectedPortalIndex = -1;

					if (callbacks.onSelectLight)
					{
						callbacks.onSelectLight(state.selectedLightIndex);
					}
				}

				// Context menu
				if (ImGui::BeginPopupContextItem())
				{
					if (ImGui::MenuItem("Focus"))
					{
						if (callbacks.onFocusPosition)
						{
							callbacks.onFocusPosition(light.position);
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
					}
					ImGui::PopStyleColor();
					ImGui::EndPopup();
				}

				// Tooltip
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::Text("Type: %s", typeStr);
					ImGui::Text("Position: %.2f, %.2f, %.2f", light.position.x, light.position.y, light.position.z);
					ImGui::Text("Intensity: %.2f", light.intensity);
					ImGui::EndTooltip();
				}

				ImGui::PopID();
			}

			ImGui::Unindent();
		}
	}

	/// @brief Draws the portals section.
	static void DrawPortalsSection(
		WorldModel* worldModel,
		HierarchyPanelState& state,
		const HierarchyPanelCallbacks& callbacks,
		const String& filter)
	{
		const auto& portals = worldModel->GetPortals();

		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.5f, 0.2f, 0.5f, 0.6f));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.6f, 0.3f, 0.6f, 0.7f));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.7f, 0.4f, 0.7f, 0.8f));

		String portalsHeader = "Portals (" + std::to_string(portals.size()) + ")";
		bool portalsSectionOpen = ImGui::CollapsingHeader(portalsHeader.c_str(),
			state.portalsExpanded ? ImGuiTreeNodeFlags_DefaultOpen : 0);
		state.portalsExpanded = portalsSectionOpen;

		// Context menu for Portals header
		if (ImGui::BeginPopupContextItem("PortalsHeaderContext"))
		{
			if (worldModel->GetGroupCount() >= 2)
			{
				if (ImGui::MenuItem("Create Portal..."))
				{
					if (callbacks.onStartPortalCreation)
					{
						callbacks.onStartPortalCreation();
					}
				}
			}
			else
			{
				ImGui::TextDisabled("Need 2+ groups for portals");
			}
			ImGui::EndPopup();
		}

		ImGui::PopStyleColor(3);

		if (portalsSectionOpen)
		{
			ImGui::Indent();

			if (portals.empty())
			{
				ImGui::TextDisabled("No portals");
			}

			for (size_t i = 0; i < portals.size(); ++i)
			{
				String label = "Portal " + std::to_string(i);

				if (!MatchesFilter(label, filter))
				{
					continue;
				}

				ImGui::PushID(static_cast<int>(i));

				bool isSelected = (state.selectedType == HierarchyItemType::Portal &&
					state.selectedPortalIndex == static_cast<int32>(i));

				ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | 
					ImGuiTreeNodeFlags_NoTreePushOnOpen |
					ImGuiTreeNodeFlags_SpanAvailWidth;

				if (isSelected)
				{
					flags |= ImGuiTreeNodeFlags_Selected;
				}

				ImGui::TreeNodeEx(label.c_str(), flags);

				if (ImGui::IsItemClicked())
				{
					state.selectedType = HierarchyItemType::Portal;
					state.selectedPortalIndex = static_cast<int32>(i);
					state.selectedGroupIndex = -1;
					state.selectedMeshRefIndex = -1;
					state.selectedMeshRefIndices.clear();
					state.selectedLightIndex = -1;

					if (callbacks.onSelectPortal)
					{
						callbacks.onSelectPortal(state.selectedPortalIndex);
					}
				}

				// Context menu
				if (ImGui::BeginPopupContextItem())
				{
					if (portals[i] && ImGui::MenuItem("Focus"))
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

			ImGui::Unindent();
		}
	}

	void DrawHierarchyPanel(
		WorldModel* worldModel,
		std::vector<GroupVisualization>& groupVisualizations,
		HierarchyPanelState& state,
		const HierarchyPanelCallbacks& callbacks)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));

		if (!worldModel)
		{
			ImGui::TextDisabled("No world model loaded");
			ImGui::PopStyleVar(2);
			return;
		}

		// Search filter
		ImGui::SetNextItemWidth(-1);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
		ImGui::InputTextWithHint("##hierarchySearch", "Search...", state.searchFilter, sizeof(state.searchFilter));
		ImGui::PopStyleColor();

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		String filter = state.searchFilter;

		// Draw sections
		DrawGroupsSection(worldModel, groupVisualizations, state, callbacks, filter);
		ImGui::Spacing();
		DrawLightsSection(worldModel, state, callbacks, filter);
		ImGui::Spacing();
		DrawPortalsSection(worldModel, state, callbacks, filter);

		ImGui::PopStyleVar(2);

		// New group dialog
		if (state.showNewGroupDialog)
		{
			ImGui::OpenPopup("New Group");
		}

		if (ImGui::BeginPopupModal("New Group", &state.showNewGroupDialog, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Group Name:");
			ImGui::SetNextItemWidth(250);
			ImGui::InputText("##newGroupName", state.newGroupName, sizeof(state.newGroupName));

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.3f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.4f, 0.9f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.8f, 0.5f, 1.0f));
			if (ImGui::Button("Create", ImVec2(100, 0)))
			{
				if (callbacks.onCreateGroup)
				{
					callbacks.onCreateGroup(state.newGroupName);
				}
				state.showNewGroupDialog = false;
			}
			ImGui::PopStyleColor(3);

			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(100, 0)))
			{
				state.showNewGroupDialog = false;
			}

			ImGui::EndPopup();
		}
	}

}
