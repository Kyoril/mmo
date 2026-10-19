// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "trade_skill_book.h"

namespace mmo
{
	TradeSkillBook::LearnResult TradeSkillBook::LearnOrRaise(const uint32 skillId, const uint16 max)
	{
		const auto it = m_skills.find(skillId);
		if (it == m_skills.end())
		{
			m_skills[skillId] = TradeSkillData { skillId, 1, max };
			return LearnResult::Learned;
		}

		if (max > it->second.max)
		{
			it->second.max = max;
			return LearnResult::CapRaised;
		}

		return LearnResult::Unchanged;
	}

	bool TradeSkillBook::Has(const uint32 skillId) const
	{
		return m_skills.contains(skillId);
	}

	const TradeSkillData* TradeSkillBook::Get(const uint32 skillId) const
	{
		const auto it = m_skills.find(skillId);
		return it == m_skills.end() ? nullptr : &it->second;
	}

	bool TradeSkillBook::TrySkillUp(const uint32 skillId, const TradeSkillThresholds& thresholds, const float roll)
	{
		const auto it = m_skills.find(skillId);
		if (it == m_skills.end() || it->second.value >= it->second.max)
		{
			return false;
		}

		if (roll < GetTradeSkillUpChance(it->second.value, thresholds))
		{
			++it->second.value;
			return true;
		}

		return false;
	}

	void TradeSkillBook::SetAll(const std::vector<TradeSkillData>& skills)
	{
		m_skills.clear();
		for (const TradeSkillData& skill : skills)
		{
			m_skills[skill.skillId] = skill;
		}
	}

	std::vector<TradeSkillData> TradeSkillBook::GetAll() const
	{
		std::vector<TradeSkillData> result;
		result.reserve(m_skills.size());
		for (const auto& [id, skill] : m_skills)
		{
			result.push_back(skill);
		}

		return result;
	}
}
