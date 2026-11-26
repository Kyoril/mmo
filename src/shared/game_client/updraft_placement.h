// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game/traversal.h"
#include "math/vector3.h"

namespace mmo
{
	/// Share of a sphere updraft's radius its particle column covers: the column is a box, and
	/// a box as wide as the sphere would put streaks outside the volume at its corners.
	constexpr float UpdraftSphereFootprint = 0.7f;

	/// @brief Where the particle column of an updraft stands.
	struct UpdraftEmitterPlacement
	{
		/// Bottom centre of the volume, where the streaks start.
		Vector3 base;

		/// Half width of the footprint along the volume's local X axis.
		float halfWidth = 0.0f;

		/// Half depth of the footprint along the volume's local Z axis.
		float halfDepth = 0.0f;

		/// Facing of the footprint around the Y axis, in radians.
		float yaw = 0.0f;

		/// Height of the volume.
		float height = 0.0f;
	};

	/// @brief Gets the particle column placement for an updraft volume.
	[[nodiscard]] UpdraftEmitterPlacement GetUpdraftEmitterPlacement(const UpdraftVolume& volume);
}
