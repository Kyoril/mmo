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
		/// resolve to nothing and are written back verbatim.
		std::function<const proto_client::SpellEntry*(uint32 spellId)> findSpell;

		/// Resolves a duration format key (FORMAT_DURATION_SECONDS, _MINUTES or _HOURS, with a
		/// _PRECISE suffix for the lower case tokens) to its localized template, for example
		/// "%.0f seconds". Returning null, or leaving this empty, writes the key itself.
		std::function<const std::string*(const std::string& formatKey)> findDurationFormat;

		/// Returns the duration a spell's auras last for the reader, with their duration spell
		/// modifiers (talents like Dreamweaver) applied, in milliseconds. Drives the d / D, t / T
		/// and o / O tokens, matching the server, which also counts ticks over the modified
		/// duration. Not called for spells without duration, which stay infinite. Leaving this
		/// empty uses the spell's own duration.
		std::function<int32(const proto_client::SpellEntry& spell)> getDuration;
	};

	/// @brief Computes the minimum and maximum points of one spell effect at a given level.
	/// @param spell The spell owning the effect.
	/// @param level The caster level, clamped to the spell's base and max level.
	/// @param effectIndex Zero-based effect index. Out of range yields 0 / 0.
	/// @param includeTickCount Multiplies periodic effects by their number of ticks.
	/// @param min Receives the absolute minimum, saturated to the int32 range.
	/// @param max Receives the absolute maximum, saturated to the int32 range.
	void GetSpellEffectPoints(const proto_client::SpellEntry& spell, int32 level, int32 effectIndex, bool includeTickCount, int32& min, int32& max);

	/// @brief Number of ticks a periodic effect performs over the spell's duration, or 0 for
	/// an effect that is not periodic, has no amplitude or belongs to a spell without duration.
	int32 GetSpellEffectTickCount(const proto_client::SpellEntry& spell, int32 effectIndex);

	/// @brief Writes a value into a localized printf-style template without ever handing the
	/// template to printf.
	///
	/// Only the first conversion is replaced. It may carry flags, a width and a precision, and
	/// must be one of f F e E g G (written as a floating point value) or d i u (rounded to a
	/// whole number). "%%" writes a percent sign. Anything else, including a second conversion
	/// or "%s", is written back verbatim, so a translation mistake shows up as text instead of
	/// reading garbage off the stack.
	/// @param out Receives the formatted text (appended).
	/// @param trimTrailingZeros Drops trailing zeros of a fixed point value ("1.50" -> "1.5").
	void AppendFormattedValue(std::string& out, const std::string& format, double value, bool trimTrailingZeros = false);

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
	/// | d / D     | spell duration after the reader's modifiers. d always uses the precise template ("1.50 seconds"); D     |
	/// |           | uses the rounded one for whole values and the precise one without trailing |
	/// |           | zeros otherwise ("2 seconds", "1.5 seconds")                               |
	/// | i / I     | tick interval of an effect, precise / rounded like d / D                   |
	/// | $         | a literal '$'                                                              |
	///
	/// Unknown tokens and references to unknown spells are written back unchanged, so a typo
	/// stays visible in the tooltip instead of silently vanishing. The formatter never reads
	/// out of bounds, never overflows and never hands data-driven text to printf: any text and
	/// any spell data produce some string.
	std::string FormatSpellText(const std::string& text, const proto_client::SpellEntry& spell, const SpellTextContext& context);
}
