// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <functional>
#include <vector>

namespace mmo
{
	class WorldModel;

	/// @brief State for portal creation wizard.
	struct PortalCreationState
	{
		bool creatingPortal { false };
		int32 sourceGroup { -1 };
		int32 targetGroup { -1 };
	};

	/// @brief Callbacks for the portals panel to interact with the editor.
	struct PortalsPanelCallbacks
	{
		/// @brief Called when a portal is created.
		std::function<void(int32, int32, const std::vector<Vector3>&)> onCreatePortal;

		/// @brief Called when a portal is selected.
		std::function<void(int32)> onSelectPortal;

		/// @brief Called when a portal is deleted.
		std::function<void(int32)> onRemovePortal;

		/// @brief Called when portal properties change.
		std::function<void()> onPortalChanged;

		/// @brief Called when the camera should focus on a position.
		std::function<void(const Vector3&)> onFocusPosition;

		/// @brief Called when portal group connections change.
		std::function<void(int32, int32, int32, int32)> onUpdatePortalGroupConnection;
	};

	/// @brief Draws the portals panel UI.
	/// @param worldModel The world model being edited.
	/// @param selectedPortalIndex Reference to the selected portal index.
	/// @param creationState State for portal creation wizard.
	/// @param callbacks Callbacks for editor interaction.
	void DrawPortalsPanel(
		WorldModel* worldModel,
		int32& selectedPortalIndex,
		PortalCreationState& creationState,
		const PortalsPanelCallbacks& callbacks);

}
