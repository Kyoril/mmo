// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "client_data/project.h"

#include <functional>
#include <string>

namespace mmo
{
	/// @brief Everything the spell text formatter needs from the outside world.
	struct SpellTextContext
	{
		/// Level of the unit reading the tooltip. Clamped per spell to its base and max level.
		int32 level = 1;

		/// Resolves a spell id to its entry, for placeholders that reference another spell
		/// ("$152s0") and for periodic trigger totals. May be empty, then such references
		/// resolve to nothing.
		std::function<const proto_client::SpellEntry*(uint32 spellId)> findSpell;

		/// Formats a duration in the given unit. The key is one of FORMAT_DURATION_SECONDS,
		/// _MINUTES or _HOURS, with a _PRECISE suffix for the lower case tokens. Empty means
		/// the key itself is written.
		std::function<std::string(const std::string& formatKey, double value)> formatDuration;
	};

	/// @brief Computes the minimum and maximum points of one spell effect at a given level.
	/// @param spell The spell owning the effect.
	/// @param level The caster level, clamped to the spell's base and max level.
	/// @param effectIndex Zero-based effect index. Out of range yields 0 / 0.
	/// @param includeTickCount Multiplies periodic effects by their number of ticks.
	/// @param min Receives the absolute minimum.
	/// @param max Receives the absolute maximum.
	void GetSpellEffectPoints(const proto_client::SpellEntry& spell, int32 level, int32 effectIndex, bool includeTickCount, int32& min, int32& max);

	/// @brief Number of ticks a periodic effect performs over the spell's duration, or 0 for
	/// an effect without an amplitude.
	int32 GetSpellEffectTickCount(const proto_client::SpellEntry& spell, int32 effectIndex);

	/// @brief Replaces the placeholders in a spell description or aura text.
	///
	/// Every placeholder starts with '$', optionally followed by the id of another spell whose
	/// values should be used instead of this one's ("$152s0"), then a token letter and, where
	/// the token takes one, a single zero-based effect index digit. An omitted index reuses the
	/// last one written.
	///
	/// | Token     | Meaning                                                                    |
	/// |-----------|----------------------------------------------------------------------------|
	/// | s / S     | effect points, "min - max" when they differ                                |
	/// | m / M     | minimum / maximum effect points                                            |
	/// | o / O     | total over the duration: points x ticks. A periodic trigger effect totals  |
	/// |           | the damage or healing of the spell it triggers instead                     |
	/// | t / T     | number of ticks of a periodic effect                                       |
	/// | d / D     | spell duration (d = precise, D = rounded)                                  |
	/// | i / I     | tick interval of an effect (i = precise, I = rounded)                      |
	/// | $         | a literal '$'                                                              |
	///
	/// Unknown tokens and references to unknown spells are written back unchanged, so a typo
	/// stays visible in the tooltip instead of silently vanishing.
	std::string FormatSpellText(const std::string& text, const proto_client::SpellEntry& spell, const SpellTextContext& context);
}
