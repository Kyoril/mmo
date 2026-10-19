// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "proto_data/project.h"

#include <algorithm>
#include <vector>

namespace mmo
{
	/// Counts the units a mixed spawn can spawn besides its unitentry (the one the editor previews).
	/// @param spawn The unit spawn point.
	/// @return 0 for a plain spawn, otherwise the number of other distinct entries with weight > 0.
	inline uint32 CountOtherMixedSpawnEntries(const proto::UnitSpawnEntry& spawn)
	{
		std::vector<uint32> others;
		for (const auto& candidate : spawn.unitentries())
		{
			if (candidate.weight() > 0 && candidate.unitentry() != spawn.unitentry() &&
				std::find(others.begin(), others.end(), candidate.unitentry()) == others.end())
			{
				others.push_back(candidate.unitentry());
			}
		}

		return static_cast<uint32>(others.size());
	}

	/// Label suffix marking a mixed spawn in editor lists, e.g. " +1".
	/// @param spawn The unit spawn point.
	/// @return An empty string for a plain spawn.
	inline String MixedSpawnLabelSuffix(const proto::UnitSpawnEntry& spawn)
	{
		const uint32 others = CountOtherMixedSpawnEntries(spawn);
		return others > 0 ? " +" + std::to_string(others) : String();
	}
}
