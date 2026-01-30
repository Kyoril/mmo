// Copyright (c) 2019 - 2025. Mmo-Dev. All rights reserved.
// Licensed under the MIT License. See LICENSE file for full license information.

#pragma once

#include "scene_graph/world_model.h"

#include <functional>

namespace mmo
{
	/// @brief Callbacks for the lights panel to interact with the editor.
	struct LightsPanelCallbacks
	{
		/// @brief Called when the user wants to create a new light.
		/// @param groupIndex The group to add the light to.
		/// @param position The world position for the light.
		/// @param color The light color.
		/// @param intensity The light intensity.
		/// @param type The light type (Omni/Spot).
		std::function<void(int32, const Vector3&, uint32, float, WorldModelLight::LightType)> onCreateLight;

		/// @brief Called when a light is selected.
		/// @param lightIndex The index of the selected light, or -1 to deselect.
		std::function<void(int32)> onSelectLight;

		/// @brief Called when a light is deleted.
		/// @param lightIndex The index of the light to delete.
		std::function<void(int32)> onRemoveLight;

		/// @brief Called when a light property changes.
		std::function<void()> onLightChanged;

		/// @brief Called when the camera should focus on a position.
		/// @param position The position to focus on.
		std::function<void(const Vector3&)> onFocusPosition;

		/// @brief Called to duplicate a light.
		/// @param lightIndex The index of the light to duplicate.
		std::function<void(size_t)> onDuplicateLight;
	};

	/// @brief State data for the lights panel.
	struct LightsPanelState
	{
		/// @brief The currently selected light index, or -1 for none.
		int32 selectedLightIndex = -1;
	};

	/// @brief Draws the lights panel UI.
	/// @param worldModel The world model being edited.
	/// @param state The panel state.
	/// @param callbacks The callbacks for editor interaction.
	void DrawLightsPanel(
		WorldModel* worldModel,
		LightsPanelState& state,
		const LightsPanelCallbacks& callbacks);

}
