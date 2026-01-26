// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <functional>

namespace mmo
{
	class WorldModel;

	/// @brief Callbacks for the properties panel to interact with the editor.
	struct PropertiesPanelCallbacks
	{
		/// @brief Called when the save button is clicked.
		std::function<void()> onSave;

		/// @brief Called when group visualizations need to be updated.
		std::function<void()> onUpdateGroupVisualizations;
	};

	/// @brief Draws the properties panel UI.
	/// @param worldModel The world model being edited.
	/// @param selectedGroupIndex The currently selected group index.
	/// @param callbacks Callbacks for editor interaction.
	void DrawPropertiesPanel(
		WorldModel* worldModel,
		int32 selectedGroupIndex,
		const PropertiesPanelCallbacks& callbacks);

}
