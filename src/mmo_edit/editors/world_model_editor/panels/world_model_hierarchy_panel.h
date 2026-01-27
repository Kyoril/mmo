// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"
#include "scene_graph/world_model.h"

#include <functional>
#include <vector>
#include <set>

namespace mmo
{
	class WorldModel;
	struct GroupVisualization;

	/// @brief Enumeration of selectable item types in the hierarchy.
	enum class HierarchyItemType
	{
		None,
		Group,
		MeshRef,
		Light,
		Portal
	};

	/// @brief State for the hierarchy panel.
	struct HierarchyPanelState
	{
		/// @brief The currently selected item type.
		HierarchyItemType selectedType { HierarchyItemType::None };

		/// @brief Selected group index (-1 for none).
		int32 selectedGroupIndex { -1 };

		/// @brief Selected mesh ref index within the group (-1 for none).
		int32 selectedMeshRefIndex { -1 };

		/// @brief Set of selected mesh ref indices for multi-selection.
		std::set<size_t> selectedMeshRefIndices;

		/// @brief Selected light index (-1 for none).
		int32 selectedLightIndex { -1 };

		/// @brief Selected portal index (-1 for none).
		int32 selectedPortalIndex { -1 };

		/// @brief Which groups are expanded in the tree view.
		std::set<int32> expandedGroups;

		/// @brief Whether the lights section is expanded.
		bool lightsExpanded { true };

		/// @brief Whether the portals section is expanded.
		bool portalsExpanded { true };

		/// @brief Search filter for the hierarchy.
		char searchFilter[256] { "" };

		/// @brief New group dialog state.
		bool showNewGroupDialog { false };
		char newGroupName[256] { "" };
	};

	/// @brief Callbacks for the hierarchy panel.
	struct HierarchyPanelCallbacks
	{
		/// @brief Called when a group is selected.
		std::function<void(int32)> onSelectGroup;

		/// @brief Called when a mesh ref is selected.
		std::function<void(int32, int32)> onSelectMeshRef;

		/// @brief Called when a light is selected.
		std::function<void(int32)> onSelectLight;

		/// @brief Called when a portal is selected.
		std::function<void(int32)> onSelectPortal;

		/// @brief Called when a group should be removed.
		std::function<void(size_t)> onRemoveGroup;

		/// @brief Called when a mesh ref should be removed.
		std::function<void(int32, size_t)> onRemoveMeshRef;

		/// @brief Called when a light should be removed.
		std::function<void(int32)> onRemoveLight;

		/// @brief Called when a portal should be removed.
		std::function<void(int32)> onRemovePortal;

		/// @brief Called when group visibility changes.
		std::function<void(size_t, bool)> onSetGroupVisibility;

		/// @brief Called when mesh ref visibility changes.
		std::function<void(int32, size_t, bool)> onSetMeshRefVisibility;

		/// @brief Called when a new group should be created.
		std::function<void(const String&)> onCreateGroup;

		/// @brief Called when a new light should be created.
		std::function<void(WorldModelLight::LightType)> onCreateLight;

		/// @brief Called when a portal creation should start.
		std::function<void()> onStartPortalCreation;

		/// @brief Called when the camera should focus on a position.
		std::function<void(const Vector3&)> onFocusPosition;

		/// @brief Called when a mesh should be added to a group.
		std::function<void(int32)> onAddMeshToGroup;

		/// @brief Called when a light should be duplicated.
		std::function<void(size_t)> onDuplicateLight;

		/// @brief Called when a mesh ref should be duplicated.
		std::function<void(int32, size_t)> onDuplicateMeshRef;
	};

	/// @brief Draws the unified hierarchy panel UI.
	/// @param worldModel The world model being edited.
	/// @param groupVisualizations The group visualizations for visibility state.
	/// @param state The panel state (modified by the function).
	/// @param callbacks Callbacks for editor interaction.
	void DrawHierarchyPanel(
		WorldModel* worldModel,
		std::vector<GroupVisualization>& groupVisualizations,
		HierarchyPanelState& state,
		const HierarchyPanelCallbacks& callbacks);

}
