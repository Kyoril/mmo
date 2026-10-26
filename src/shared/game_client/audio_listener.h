// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "math/vector3.h"

namespace mmo::audio_listener
{
	/// @brief Projects a camera direction onto the horizontal plane and normalizes it, so that
	///        looking up or down does not tilt the audio listener.
	/// @param cameraDirection The camera's forward direction.
	/// @return The flattened, normalized direction, or (0, 0, -1) if the camera looks (almost)
	///         straight up or down and no horizontal direction remains.
	inline Vector3 FlattenedForward(const Vector3& cameraDirection)
	{
		const Vector3 flat(cameraDirection.x, 0.0f, cameraDirection.z);
		if (flat.GetLength() < 1e-4f)
		{
			return Vector3(0.0f, 0.0f, -1.0f);
		}

		return flat.NormalizedCopy();
	}
}
