// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "math/radian.h"
#include "shared/proto_data/races.pb.h"

namespace mmo
{
	/// Returns the facing a newly created character of the given race starts with.
	/// RaceEntry::startrotation is stored in radians (the race editor edits it in [0, 2 * Pi)),
	/// so it must not go through Angle, whose unit defaults to degrees.
	/// @param race The race whose start location is used.
	/// @return The start facing.
	inline Radian GetRaceStartFacing(const proto::RaceEntry& race)
	{
		return Radian(race.startrotation());
	}
}
