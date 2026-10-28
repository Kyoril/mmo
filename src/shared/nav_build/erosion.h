// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

class rcContext;
struct rcCompactHeightfield;

namespace mmo
{
	/// @brief Erodes the walkable area by the agent radius, but building floors by less.
	/// @details Spans whose area carries one of the @p narrowAreas bits keep the clearance of
	///          @p narrowRadius instead of @p radius. Outdoors the full radius keeps paths off
	///          trees and fences; indoors it closed stairwells and doorways narrower than twice
	///          the radius, which a character's collider passes with ease.
	/// @param radius Clearance in voxels for every other span.
	/// @param narrowRadius Clearance in voxels for spans with one of the narrow area bits; at
	///        most @p radius.
	/// @returns False if Recast fails to erode.
	bool ErodeWalkableArea(rcContext& ctx, rcCompactHeightfield& chf, int radius, int narrowRadius, uint8 narrowAreas);
}
