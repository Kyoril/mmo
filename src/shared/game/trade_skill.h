// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <algorithm>
#include <limits>

namespace mmo
{
	namespace skill_category
	{
		/// SkillEntry.category value of a profession.
		constexpr int32 Profession = 1;
	}

	namespace trade_skill_difficulty
	{
		/// Difficulty colour of a recipe relative to the crafter's skill value.
		enum Type
		{
			/// Skill is below the recipe's requirement; the recipe cannot be crafted.
			Impossible,
			/// Guaranteed skill-up.
			Orange,
			/// Likely skill-up.
			Yellow,
			/// Unlikely skill-up.
			Green,
			/// No skill-up.
			Grey
		};
	}

	typedef trade_skill_difficulty::Type TradeSkillDifficulty;

	/// Skill values at which a recipe changes colour.
	struct TradeSkillThresholds
	{
		uint32 required = 0;
		uint32 yellow = 0;
		uint32 green = 0;
		uint32 grey = 0;
	};

	/// Orders the thresholds (required <= yellow <= green <= grey), fills a missing green with the
	/// midpoint of yellow and grey and treats a grey of 0 as "never trivial".
	inline TradeSkillThresholds NormalizeTradeSkillThresholds(TradeSkillThresholds t)
	{
		constexpr uint32 never = std::numeric_limits<uint32>::max();
		if (t.grey == 0)
		{
			t.yellow = never;
			t.green = never;
			t.grey = never;
			return t;
		}

		t.yellow = std::max(t.yellow, t.required);
		t.grey = std::max(t.grey, t.yellow);
		if (t.green == 0)
		{
			t.green = t.yellow + (t.grey - t.yellow) / 2;
		}

		t.green = std::clamp(t.green, t.yellow, t.grey);
		return t;
	}

	/// Reads the thresholds of a recipe spell. Works for server and client spell entries.
	template <typename SpellT>
	TradeSkillThresholds TradeSkillThresholdsOf(const SpellT& spell)
	{
		return NormalizeTradeSkillThresholds({ spell.skillrequired(), spell.trivialskilllow(), spell.skillgreen(), spell.trivialskillhigh() });
	}

	/// Returns the colour of a recipe for a crafter with the given skill value.
	inline TradeSkillDifficulty GetTradeSkillDifficulty(const uint32 value, const TradeSkillThresholds& t)
	{
		if (value < t.required)
		{
			return trade_skill_difficulty::Impossible;
		}
		if (value >= t.grey)
		{
			return trade_skill_difficulty::Grey;
		}
		if (value >= t.green)
		{
			return trade_skill_difficulty::Green;
		}
		if (value >= t.yellow)
		{
			return trade_skill_difficulty::Yellow;
		}

		return trade_skill_difficulty::Orange;
	}

	/// Returns the chance (0..1) that crafting the recipe raises the skill: 1 up to yellow, then
	/// falling linearly to 0 at grey; 0 when the recipe cannot be crafted.
	inline float GetTradeSkillUpChance(const uint32 value, const TradeSkillThresholds& t)
	{
		if (value < t.required || value >= t.grey)
		{
			return 0.0f;
		}
		if (value <= t.yellow || t.grey == std::numeric_limits<uint32>::max())
		{
			return 1.0f;
		}

		return static_cast<float>(t.grey - value) / static_cast<float>(t.grey - t.yellow);
	}
}
