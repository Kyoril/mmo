// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_personality.h"

#include <cmath>

namespace mmo
{
	namespace
	{
		/// Splitmix-style mixing. Bot seeds are derived from consecutive indices, so a weak hash
		/// would give neighbouring bots neighbouring traits - which is the opposite of the point.
		uint32 mix(uint32 value)
		{
			value ^= value >> 16;
			value *= 0x7feb352du;
			value ^= value >> 15;
			value *= 0x846ca68bu;
			value ^= value >> 16;
			return value;
		}

		/// Maps a hashed value onto [minValue, maxValue].
		float spread(const uint32 hashed, const float minValue, const float maxValue)
		{
			const float t = static_cast<float>(hashed % 10000u) / 9999.0f;
			return minValue + (maxValue - minValue) * t;
		}
	}

	BotPersonality BotPersonality::FromSeed(const uint32 seed)
	{
		BotPersonality personality;

		// A separate hash per trait, so that two bots which happen to agree on one of them are
		// no more likely to agree on the rest.
		personality.searchRadiusScale = spread(mix(seed ^ 0x1u), 0.5f, 2.0f);
		personality.candidateChoices = 3u + mix(seed ^ 0x2u) % 8u;
		personality.approachAngle = spread(mix(seed ^ 0x3u), 0.0f, 6.283185f);
		personality.spotOffset = spread(mix(seed ^ 0x4u), 2.0f, 10.0f);
		personality.combatOffset = spread(mix(seed ^ 0x5u), 1.0f, 1.8f);

		personality.restHealthFraction = spread(mix(seed ^ 0x6u), 0.25f, 0.55f);

		// Always a real gap above the break-off point, or the bot would rejoin the fight on the
		// hit that drove it out of it.
		personality.restedHealthFraction = personality.restHealthFraction
			+ spread(mix(seed ^ 0x7u), 0.25f, 0.5f);

		if (personality.restedHealthFraction > 0.95f)
		{
			personality.restedHealthFraction = 0.95f;
		}

		return personality;
	}

	Vector3 OffsetAround(const Vector3& center, const float angleRadians, const float radius)
	{
		return Vector3(
			center.x + std::cos(angleRadians) * radius,
			center.y,
			center.z + std::sin(angleRadians) * radius);
	}
}
