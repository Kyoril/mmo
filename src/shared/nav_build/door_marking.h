// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "nav_mesh/door_volume.h"

#include <vector>

class rcContext;
struct rcCompactHeightfield;

namespace mmo
{
	/// @brief Marks the walkable spans under every door volume with the door area, so Recast
	/// cuts a separate poly strip there. Call after erosion and before building regions.
	/// @details The area keeps its other bits (Ground, Entity, ...) and gains poly_flags::Door.
	/// Doors outside the heightfield are ignored.
	void MarkDoorAreas(rcContext& ctx, rcCompactHeightfield& chf, const std::vector<nav::DoorVolume>& doors);
}
