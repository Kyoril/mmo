// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <functional>
#include <vector>

namespace mmo
{
	class WorldModel;
	class Selection;
	struct GroupVisualization;

	/// @brief Callback functions for the groups panel to interact with the editor.
	struct GroupsPanelCallbacks
	{
		std::function<void(size_t)> onRemoveGroup;
		std::function<void(int32)> onSelectGroup;
		std::function<void(size_t, bool)> onSetGroupVisibility;
	};

	/// @brief State for the groups panel UI.
	struct GroupsPanelState
	{
		bool showNewGroupDialog { false };
		char newGroupName[256] { "" };
		uint32 newGroupFlags { 0 };
	};

	/// @brief Draws the groups panel UI.
	/// @param worldModel The world model to display groups for.
	/// @param groupVisualizations The visualizations for the groups.
	/// @param selectedGroupIndex Currently selected group index.
	/// @param state Panel state (dialog flags, input buffers).
	/// @param callbacks Callbacks for editor interactions.
	void DrawGroupsPanel(
		WorldModel* worldModel,
		std::vector<GroupVisualization>& groupVisualizations,
		int32& selectedGroupIndex,
		GroupsPanelState& state,
		const GroupsPanelCallbacks& callbacks);

}
