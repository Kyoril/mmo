// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "math/vector3.h"

#include <optional>

namespace mmo
{
	/// @brief Whether the environment should snap instead of fade on this update.
	/// @param snapPending True until the first successful zone lookup after entering a map.
	/// @param lastPosition The controlled unit's position at the previous environment update, if any.
	/// @param position The controlled unit's current position.
	/// @param teleportDistance A move longer than this within one update is a teleport.
	[[nodiscard]] inline bool ShouldSnapEnvironment(bool snapPending, const std::optional<Vector3>& lastPosition, const Vector3& position, float teleportDistance = 200.0f)
	{
		if (snapPending)
		{
			return true;
		}

		if (lastPosition && (position - *lastPosition).GetSquaredLength() > teleportDistance * teleportDistance)
		{
			// A jump this large within one update is a teleport; fading across it would show the
			// previous zone's mood at the destination.
			return true;
		}

		return false;
	}
}
