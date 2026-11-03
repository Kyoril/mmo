// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/movement_ability.h"
#include "math/vector3.h"

#include <functional>
#include <vector>

namespace mmo
{
	/// @brief Queries the water surface height at a world position.
	/// @return True when there is water at the given position, false otherwise.
	typedef std::function<bool(float x, float z, float& outSurfaceY)> WaterSurfaceQuery;

	/// @brief Whether a position stands in water deep enough to swim in rather than wade through.
	/// @param position World position of the unit's feet (or of a navigation mesh corner).
	/// @param waterQuery Water surface query. An empty query reports no water anywhere.
	[[nodiscard]] bool IsSwimmableWaterAt(const Vector3& position, const WaterSurfaceQuery& waterQuery);

	/// @brief Describes what the water along a movement path does to the moving unit.
	struct WaterPathResult
	{
		/// @brief The path leads through water which is deep enough to swim in.
		bool swimming = false;

		/// @brief Nothing is left of the path that the unit could move along, so the movement
		///	has to be refused.
		bool blocked = false;

		/// @brief The path was cut short in front of water the unit is not able to swim through.
		bool truncated = false;

		/// @brief The destination lies in water which is deep enough to swim in.
		bool destinationSwimming = false;
	};

	/// @brief Adjusts a navigation path for the water it leads through.
	///
	///	The navigation mesh is baked on the ground, so a path through a lake leads along its
	///	bottom. A unit which is able to swim has to float at the surface instead, so every path
	///	point in water deep enough to swim in is lifted into the surface band. For a unit which
	///	can not swim, water that deep is impassable: its paths are routed around deep water by
	///	DeepWaterQueryFilter, and whatever poly-granular filtering still lets through is cut off
	///	here, so the unit stops in front of the water instead of walking into it.
	///
	/// @param path The path points, adjusted in place.
	/// @param movementAbilities The moving unit's movement_ability flags.
	/// @param waterQuery Water surface query, usually backed by the map's water data. May be
	///	empty, in which case the path is left unchanged (no water data available for the map).
	/// @return What the water does to this movement.
	WaterPathResult ApplyWaterToPath(std::vector<Vector3>& path, MovementAbilityFlags movementAbilities,
		const WaterSurfaceQuery& waterQuery);
}
