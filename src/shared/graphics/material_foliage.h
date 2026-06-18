// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// @brief A single data-driven foliage definition carried by a terrain material.
	/// @details Each entry is bound to one of the material's four terrain layer slots and
	///          describes which mesh to scatter and how. At runtime the procedural foliage
	///          system scatters this mesh wherever the bound layer's coverage is high enough.
	struct MaterialFoliageEntry
	{
		/// @brief The terrain layer index (0-3) this foliage grows on.
		uint8 layerIndex = 0;

		/// @brief Asset path of the mesh to scatter (.hmsh).
		String meshPath;

		/// @brief Target instances per square world unit at full layer coverage.
		float density = 1.0f;

		/// @brief Minimum layer coverage required before foliage is placed (0-1).
		float minCoverage = 0.3f;

		/// @brief Minimum random scale factor.
		float minScale = 0.8f;

		/// @brief Maximum random scale factor.
		float maxScale = 1.2f;

		/// @brief Maximum terrain slope (degrees) foliage will be placed on.
		float maxSlopeAngle = 45.0f;

		/// @brief Minimum world height at which foliage can be placed.
		float minHeight = -10000.0f;

		/// @brief Maximum world height at which foliage can be placed.
		float maxHeight = 10000.0f;

		/// @brief Distance at which foliage starts to fade out.
		float fadeStartDistance = 50.0f;

		/// @brief Distance at which foliage is fully culled.
		float fadeEndDistance = 100.0f;

		/// @brief Whether instances get a random yaw rotation.
		bool randomYaw = true;

		/// @brief Whether instances are aligned to the terrain surface normal.
		bool alignToNormal = false;

		/// @brief Whether instances cast shadows.
		bool castShadows = false;
	};
}
