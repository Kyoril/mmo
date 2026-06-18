// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#pragma once
#include "base/non_copyable.h"
#include "base/typedefs.h"

namespace mmo
{
	class SpawnEditMode;

	/// @brief Dockable panel that lists every spawn (units and objects) already placed in the map.
	/// The user can filter the list, click an entry to select it and press "F" (or double-click) to
	/// focus the camera on it. The list is only populated while the Spawn Edit mode is active, because
	/// spawns are only instantiated in the scene during that mode.
	class SpawnListPanel final : public NonCopyable
	{
	public:
		SpawnListPanel() = default;
		~SpawnListPanel() override = default;

		/// @brief Draw the panel window. Must be called every frame.
		/// @param id The unique ImGui window id/title.
		/// @param mode The active spawn edit mode, or nullptr when spawn editing is not the active mode.
		void Draw(const String& id, SpawnEditMode* mode);
	};
}
