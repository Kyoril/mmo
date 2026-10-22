// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "math/vector3.h"

#include <vector>

namespace mmo
{
	/// A camera position and the point it looks at.
	struct GlueCameraKey
	{
		Vector3 position;
		Vector3 lookAt;
	};

	/// Evaluates a Catmull-Rom path through the keys. t in [0,1] is clamped and eased with
	/// smoothstep. An empty list returns a default key, a single key returns that key.
	GlueCameraKey EvaluateGluePath(const std::vector<GlueCameraKey>& keys, float t);
}
