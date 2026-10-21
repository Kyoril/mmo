// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"
#include "terrain/constants.h"

#include <cmath>
#include <cstdlib>

namespace mmo
{
	/// Number of terrain pages which are kept loaded around the player in each direction.
	/// Mirrors the radius the world state hands to its LoadedPageSection: only the page the
	/// player stands on and its direct neighbors have terrain tiles and world geometry.
	constexpr int32 LoadedPageRadius = 1;

	/// Index of the terrain page a world coordinate falls into. The page grid is centered on
	/// page 32, which is where the world origin sits.
	/// @param worldCoordinate A world space x or z coordinate.
	/// @return The page index along that axis.
	inline int32 GetTerrainPageIndex(const float worldCoordinate)
	{
		return static_cast<int32>(
			std::floor(worldCoordinate / static_cast<float>(terrain::constants::PageSize))) + 32;
	}

	/// Decides whether a teleport on the same map has to load the world around its destination
	/// before the character may resume playing there.
	///
	/// Only the pages around the player are streamed in, and the server flags the character as
	/// falling after a teleport. A destination outside that square therefore has no terrain and
	/// no world geometry on the client yet: dropped there right away, the character falls
	/// through the world while the pages stream in asynchronously.
	///
	/// @param from Position the character is teleported away from.
	/// @param to Position the character is teleported to.
	/// @return true if the world around the destination has to be loaded first.
	inline bool TeleportNeedsWorldLoad(const Vector3& from, const Vector3& to)
	{
		const int32 pageDistanceX = std::abs(GetTerrainPageIndex(to.x) - GetTerrainPageIndex(from.x));
		const int32 pageDistanceZ = std::abs(GetTerrainPageIndex(to.z) - GetTerrainPageIndex(from.z));

		return pageDistanceX > LoadedPageRadius || pageDistanceZ > LoadedPageRadius;
	}
}
