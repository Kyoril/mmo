// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <functional>

namespace mmo
{
	class WorldModel;

	/// @brief Callbacks for the doodads panel to interact with the editor.
	struct DoodadsPanelCallbacks
	{
		/// @brief Called when a new doodad set is created.
		std::function<void(const String&)> onCreateDoodadSet;
	};

	/// @brief Draws the doodads panel UI.
	/// @param worldModel The world model being edited.
	/// @param selectedDoodadSetIndex Reference to the selected doodad set index.
	/// @param callbacks Callbacks for editor interaction.
	void DrawDoodadsPanel(
		WorldModel* worldModel,
		int32& selectedDoodadSetIndex,
		const DoodadsPanelCallbacks& callbacks);

}
