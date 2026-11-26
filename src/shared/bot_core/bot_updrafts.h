// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/traversal.h"

#include <vector>

namespace mmo
{
	namespace proto
	{
		class AreaTriggers;
	}

	/// @brief Collects the updraft volumes of a map from the area triggers, with the same shape rules
	///	as the world server's BuildUpdraftVolumes: a radius wins, box_x/y/z are the full box size,
	///	box_o its facing, and only triggers with an updraft strength above zero count.
	std::vector<UpdraftVolume> BuildBotUpdraftVolumes(const proto::AreaTriggers& triggers, uint32 mapId);
}
