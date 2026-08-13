// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// Minimum time a creature waits before re-attempting a spell whose cast failed.
	///
	/// A creature ability that cannot be cast — wrong facing, out of line of sight, a caster
	/// state that blocks it — must never be retried without a pause. Neither the spell's own
	/// cooldown nor the creature's authored cooldown is guaranteed to be non-zero, so this floor
	/// is what actually bounds the retry rate.
	constexpr uint32 FailedCastRetryCooldownMs = 1000;

	/// The window a creature spell is unavailable for, before a concrete value is rolled from it.
	struct CreatureSpellCooldownRange
	{
		/// Lower bound in milliseconds.
		GameTime min = 0;

		/// Upper bound in milliseconds. Never less than @ref min.
		GameTime max = 0;
	};

	/// Resolves how long a creature must wait before attempting a spell again.
	///
	/// @param entryMinCooldownMs `mincooldown` from the creature's `creaturespells` entry, 0 if unset.
	/// @param entryMaxCooldownMs `maxcooldown` from the same entry, 0 if unset.
	/// @param spellCooldownMs The spell's own cooldown, used when the creature entry specifies none.
	/// @param castFailed True when the cast attempt did not succeed, which applies the retry floor.
	/// @returns The range to roll the actual cooldown from.
	CreatureSpellCooldownRange ResolveCreatureSpellCooldown(
		uint32 entryMinCooldownMs,
		uint32 entryMaxCooldownMs,
		uint32 spellCooldownMs,
		bool castFailed);
}
