// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <map>
#include <set>

namespace mmo
{
	/// Percentage points that auras add to one unit multiplier (healing taken, cast time).
	///	Beneficial contributions add. Harmful contributions of the same spell do not stack: when
	///	several casters put the same debuff on a unit, only its strongest instance counts, while
	///	harmful contributions of different spells still add. The total is cached and recomputed
	///	whenever a contribution is applied or removed.
	class AuraPctBonus final
	{
	public:
		/// Initializes an empty bonus.
		///	@param harmfulSign +1 when positive percentage points are harmful (cast time), -1 when
		///	negative ones are (healing taken).
		explicit AuraPctBonus(const float harmfulSign)
			: m_harmfulSign(harmfulSign)
		{
		}

	public:
		/// Adds or removes one aura's contribution.
		///	@param spellId The spell of the aura, which groups harmful contributions.
		///	@param amount The contribution in percentage points.
		///	@param apply true to add the contribution, false to remove it again.
		void Modify(const uint32 spellId, const float amount, const bool apply)
		{
			const float harm = amount * m_harmfulSign;
			if (harm <= 0.0f)
			{
				m_beneficial += apply ? amount : -amount;
			}
			else if (apply)
			{
				m_harmful[spellId].insert(harm);
			}
			else
			{
				const auto spellIt = m_harmful.find(spellId);
				if (spellIt != m_harmful.end())
				{
					const auto valueIt = spellIt->second.find(harm);
					if (valueIt != spellIt->second.end())
					{
						spellIt->second.erase(valueIt);
					}
					if (spellIt->second.empty())
					{
						m_harmful.erase(spellIt);
					}
				}
			}

			Recompute();
		}

		/// Gets the summed percentage points of all counted contributions.
		[[nodiscard]] float GetTotal() const { return m_total; }

	private:
		void Recompute()
		{
			float harm = 0.0f;
			for (const auto& [spellId, values] : m_harmful)
			{
				// The multiset is ordered ascending, so its last value is the strongest instance.
				harm += *values.rbegin();
			}

			m_total = m_beneficial + harm * m_harmfulSign;
		}

	private:
		float m_harmfulSign;
		float m_beneficial = 0.0f;
		std::map<uint32, std::multiset<float>> m_harmful;
		float m_total = 0.0f;
	};
}
