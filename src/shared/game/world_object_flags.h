// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// @brief Object flags for world objects (stored in ObjectFlags field). These are
	/// authored on the ObjectEntry and replicated to clients via the field map.
	namespace world_object_flags
	{
		enum Type : uint32
		{
			/// No special flags.
			None = 0x00,
			/// Object can only be used when a specific quest is active.
			RequiresQuest = 0x01,
			/// Object is temporarily disabled (e.g., by server script).
			Disabled = 0x02,
			/// Object never becomes interactable for players (e.g. trigger-only boss doors).
			NotInteractable = 0x04,
		};
	}

	/// @brief Per-player dynamic flags computed by server.
	namespace dynamic_world_object_flags
	{
		enum Type : uint32
		{
			/// No special flags.
			None = 0x00,
			/// Object can be interacted with by this player.
			Interactable = 0x01,
		};
	}
}
