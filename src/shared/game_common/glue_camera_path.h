// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "math/vector3.h"

#include <vector>

namespace mmo
{
	/// Camera motion constants shared by the client's glue camera and the glue bake's pose
	/// sampler, so the bake covers exactly the views the client can show.
	namespace glue_camera
	{
		/// Half the width of the idle sideways drift of the camera position at the gate, in metres.
		/// The look-at point does not drift.
		constexpr float GateDriftAmplitude = 0.3f;

		/// Share of the way from the plaza camera position to the push-in target covered by a
		/// finished push-in. The camera keeps looking at the plaza look-at point.
		constexpr float PushInFraction = 0.4f;

		/// Height above a character's feet the push-in moves towards, in metres (about chest height).
		constexpr float PushInTargetHeight = 1.4f;
	}

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
