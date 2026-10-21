// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "grind_spot_claims.h"

namespace mmo
{
	void GrindSpotClaims::Claim(const uint32 botIndex, const std::size_t spotIndex)
	{
		Release(botIndex);
		m_spotByBot[botIndex] = spotIndex;
		++m_countBySpot[spotIndex];
	}

	void GrindSpotClaims::Release(const uint32 botIndex)
	{
		const auto it = m_spotByBot.find(botIndex);
		if (it == m_spotByBot.end())
		{
			return;
		}

		const auto count = m_countBySpot.find(it->second);
		if (count != m_countBySpot.end() && --count->second == 0)
		{
			m_countBySpot.erase(count);
		}

		m_spotByBot.erase(it);
	}

	uint32 GrindSpotClaims::GetCount(const std::size_t spotIndex) const
	{
		const auto it = m_countBySpot.find(spotIndex);
		return it != m_countBySpot.end() ? it->second : 0;
	}
}
