// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "creature_spell_cooldown.h"

#include <algorithm>

namespace mmo
{
	CreatureSpellCooldownRange ResolveCreatureSpellCooldown(
		const uint32 entryMinCooldownMs,
		const uint32 entryMaxCooldownMs,
		const uint32 spellCooldownMs,
		const bool castFailed)
	{
		CreatureSpellCooldownRange range;

		if (entryMinCooldownMs > 0 || entryMaxCooldownMs > 0)
		{
			range.min = entryMinCooldownMs;
			range.max = entryMaxCooldownMs;
		}
		else
		{
			range.min = spellCooldownMs;
			range.max = spellCooldownMs;
		}

		// maxcooldown is optional and routinely left at 0 next to a set mincooldown, so an
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
