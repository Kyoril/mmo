// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"
#include "math/quaternion.h"
#include "scene_graph/world_model.h"

#include <functional>
#include <set>

namespace mmo
{
	class WorldModel;
	class WorldModelGroup;
	struct GroupVisualization;

	/// @brief Enumeration of possible selection types in the properties panel.
	enum class PropertiesSelectionType
	{
		None,
		Group,
		Light,
		Portal,
		MeshRef
	};

	/// @brief State information about the current selection for the properties panel.
	struct PropertiesSelectionState
	{
		PropertiesSelectionType type { PropertiesSelectionType::None };
		int32 selectedGroupIndex { -1 };
		int32 selectedLightIndex { -1 };
		int32 selectedPortalIndex { -1 };
		int32 selectedMeshRefIndex { -1 };
		std::set<size_t> selectedMeshRefIndices;
	};

	/// @brief Callbacks for the properties panel to interact with the editor.
	struct PropertiesPanelCallbacks
	{
		/// @brief Called when the save button is clicked.
		std::function<void()> onSave;

		/// @brief Called when group visualizations need to be updated.
		std::function<void()> onUpdateGroupVisualizations;

		/// @brief Called when light visualizations need to be updated.
		std::function<void()> onUpdateLightVisualizations;

		/// @brief Called when portal visualizations need to be updated.
		std::function<void()> onUpdatePortalVisualizations;

		/// @brief Called when mesh ref visualizations need to be updated.
		/// @param groupIndex The group index containing the mesh ref.
		std::function<void(int32)> onUpdateMeshRefVisualizations;

		/// @brief Called when a light is removed.
		/// @param lightIndex The index of the light to remove.
		std::function<void(int32)> onRemoveLight;

		/// @brief Called when a portal is removed.
		/// @param portalIndex The index of the portal to remove.
		std::function<void(int32)> onRemovePortal;

		/// @brief Called when a group is removed.
		/// @param groupIndex The index of the group to remove.
		std::function<void(int32)> onRemoveGroup;

		/// @brief Called when a mesh ref is removed.
		/// @param groupIndex The group index.
		/// @param meshRefIndex The mesh ref index.
		std::function<void(int32, size_t)> onRemoveMeshRef;

		/// @brief Called to update portal group connections.
		/// @param portalIndex The portal index.
		/// @param oldGroupA The old group A index.
		/// @param newGroupA The new group A index.
		/// @param newGroupB The new group B index.
		std::function<void(int32, int32, int32, int32)> onUpdatePortalGroupConnection;

		/// @brief Called to update a mesh ref's scene node position.
		/// @param groupIndex The group index.
		/// @param meshRefIndex The mesh ref index.
		/// @param position The new position.
		std::function<void(int32, int32, const Vector3&)> onUpdateMeshRefPosition;

		/// @brief Called to update a mesh ref's scene node scale.
		/// @param groupIndex The group index.
		/// @param meshRefIndex The mesh ref index.
		/// @param scale The new scale.
		std::function<void(int32, int32, const Vector3&)> onUpdateMeshRefScale;

		/// @brief Called to update a mesh ref's material override.
		/// @param groupIndex The group index.
		/// @param meshRefIndex The mesh ref index.
		/// @param material The new material path.
		std::function<void(int32, int32, const String&)> onUpdateMeshRefMaterial;

		/// @brief Called to update a light's scene node.
		/// @param lightIndex The light index.
		std::function<void(int32)> onUpdateLightSceneNode;

		/// @brief Called to calculate the combined bounding box of all meshes in a group.
		/// @param groupIndex The group index.
		/// @return The combined AABB of all mesh refs in the group.
		std::function<AABB(int32)> onCalculateCombinedMeshBounds;

		/// @brief Called to update a single group's bounding box visualization.
		/// @param groupIndex The group index to update.
		std::function<void(int32)> onUpdateGroupBoundingBox;
	};

	/// @brief Draws the properties panel UI.
	/// @param worldModel The world model being edited.
	/// @param selectionState The current selection state.
	/// @param callbacks Callbacks for editor interaction.
	void DrawPropertiesPanel(
		WorldModel* worldModel,
		const PropertiesSelectionState& selectionState,
		const PropertiesPanelCallbacks& callbacks);

}
