// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/ai/creature_spell_cooldown.h"

#include <algorithm>

namespace mmo
{
	CreatureSpellCooldownRange ResolveCreatureSpellCooldown(
		const int32 entryMinCooldownMs,
		const int32 entryMaxCooldownMs,
		const uint32 spellCooldownMs,
		const bool castFailed)
	{
		CreatureSpellCooldownRange range;

		// Negative means "not authored" (units.proto defaults both fields to -1), so clamp before
		// widening: assigning a negative int32 into the unsigned GameTime would otherwise produce a
		// cooldown of roughly fifty days.
		const GameTime authoredMin = entryMinCooldownMs > 0 ? static_cast<GameTime>(entryMinCooldownMs) : 0;
		const GameTime authoredMax = entryMaxCooldownMs > 0 ? static_cast<GameTime>(entryMaxCooldownMs) : 0;

		if (authoredMin > 0 || authoredMax > 0)
		{
			range.min = authoredMin;
			range.max = authoredMax;
		}
		else
		{
			range.min = spellCooldownMs;
			range.max = spellCooldownMs;
		}

		// maxcooldown is optional and routinely left unauthored next to a set mincooldown, so an
		// inverted range is normal input rather than bad data.
		if (range.max < range.min)
		{
			range.max = range.min;
		}

		if (castFailed)
		{
			range.min = std::max<GameTime>(range.min, FailedCastRetryCooldownMs);
			range.max = std::max<GameTime>(range.max, FailedCastRetryCooldownMs);
		}

		return range;
	}
}
