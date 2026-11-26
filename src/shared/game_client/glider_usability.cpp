// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glider_usability.h"

namespace mmo
{
	glider_usability::Type EvaluateGliderUsability(const GliderUsabilityInput& input)
	{
		if (!input.knowsGlider || !input.alive || input.rooted || input.serverControlled)
		{
			return glider_usability::Unavailable;
		}

		if (!input.hasGliderEquipped)
		{
			return glider_usability::NoGliderEquipped;
		}

		if (input.predictedVigor <= 0.0f)
		{
			return glider_usability::Unavailable;
		}

		return glider_usability::Usable;
	}
}
