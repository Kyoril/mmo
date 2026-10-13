// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game/trade_skill.h"

#include <map>
#include <vector>

namespace mmo
{
	/// Holds the trade skills (professions) a character has learned, with value and cap.
	class TradeSkillBook final
	{
	public:
		/// Outcome of learning a trade skill.
		enum class LearnResult
		{
			/// The skill was not known and has been added at value 1.
			Learned,
			/// The skill was known and its cap has been raised.
			CapRaised,
			/// Nothing changed.
			Unchanged
		};

	public:
		/// Learns a skill at value 1, or raises the cap of a known one. Never lowers anything.
		/// @param skillId The skill id.
		/// @param max The cap granted by the learned rank.
		LearnResult LearnOrRaise(uint32 skillId, uint16 max);

		/// Returns true if the skill is known.
		bool Has(uint32 skillId) const;

		/// Returns the skill data or nullptr if the skill is unknown.
		const TradeSkillData* Get(uint32 skillId) const;

		/// Rolls a skill-up for a successful craft.
		/// @param skillId The skill id.
		/// @param thresholds Normalized thresholds of the crafted recipe.
		/// @param roll Random number in [0,1).
		/// @return True if the value rose.
		bool TrySkillUp(uint32 skillId, const TradeSkillThresholds& thresholds, float roll);

		/// Replaces the content of the book.
		void SetAll(const std::vector<TradeSkillData>& skills);

		/// Returns all known skills ordered by skill id.
		std::vector<TradeSkillData> GetAll() const;

	private:
		std::map<uint32, TradeSkillData> m_skills;
	};
}
