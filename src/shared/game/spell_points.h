// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <cmath>
#include <limits>

namespace mmo
{
	/// Scales a spell effect's flat points by its per-level factor:
	/// truncate(points + levelDelta * perLevel), towards zero.
	///
	/// The product is taken in float before truncating, so fractional per-level factors (0.6,
	/// 1.5, ...) accumulate over levels instead of being dropped. Shared by the server's base
	/// point roll and the client's spell tooltip so both always agree.
	///
	/// @param points The effect's flat points (basepoints or diesides).
	/// @param perLevel The effect's per-level factor (pointsperlevel or diceperlevel).
	/// @param levelDelta Caster level clamped to the spell's level range minus its spell level.
	/// @return The scaled points, saturated to the int32 range; 0 for NaN.
	inline int32 ScaleSpellPointsByLevel(const int32 points, const float perLevel, const int64 levelDelta)
	{
		const float value = static_cast<float>(points) + static_cast<float>(levelDelta) * perLevel;
		if (std::isnan(value))
		{
			return 0;
		}

		// float(int32 max) rounds up to 2^31, so >= catches every value that does not fit.
		if (value >= static_cast<float>(std::numeric_limits<int32>::max()))
		{
			return std::numeric_limits<int32>::max();
		}

		if (value <= static_cast<float>(std::numeric_limits<int32>::min()))
		{
			return std::numeric_limits<int32>::min();
		}

		return static_cast<int32>(value);
	}
}
