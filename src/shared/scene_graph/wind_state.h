// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "math/vector3.h"

namespace mmo
{
	/// @brief The wind of the current frame, as consumed by the renderer.
	struct WindState
	{
		/// @brief Unit direction the wind blows toward, including gust wobble (y = 0).
		Vector3 direction{ 0.70710678f, 0.0f, 0.70710678f };

		/// @brief Current speed in m/s, including gusts.
		float speed = 0.0f;

		/// @brief Accumulated noise scroll along x, in noise tiles, wrapped to [0, 1).
		float noiseOffsetX = 0.0f;

		/// @brief Accumulated noise scroll along z, in noise tiles, wrapped to [0, 1).
		float noiseOffsetZ = 0.0f;

		/// @brief Metres per repeat of the fog noise.
		float noiseSize = 60.0f;

		/// @brief Fog patchiness: 0 smooth, 1 very patchy.
		float noiseAmount = 0.5f;
	};
}
