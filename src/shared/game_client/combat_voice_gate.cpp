// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "combat_voice_gate.h"

#include <algorithm>

namespace mmo
{
	namespace
	{
		/// Above this many tracked units, expired entries are swept before inserting more.
		constexpr size_t pruneThreshold = 256;
	}

	bool CombatVoiceGate::TryPlay(const ObjectGuid unit, const GameTime now, const GameTime blockDuration)
	{
		const auto it = m_nextAllowed.find(unit);
		if (it != m_nextAllowed.end() && now < it->second)
		{
			return false;
		}

		if (m_nextAllowed.size() >= pruneThreshold)
		{
			Prune(now);
		}

		m_nextAllowed[unit] = now + blockDuration;
		return true;
	}

	void CombatVoiceGate::Silence(const ObjectGuid unit, const GameTime now, const GameTime blockDuration)
	{
		if (m_nextAllowed.size() >= pruneThreshold)
		{
			Prune(now);
		}

		GameTime& nextAllowed = m_nextAllowed[unit];
		nextAllowed = std::max(nextAllowed, now + blockDuration);
	}

	void CombatVoiceGate::Clear()
	{
		m_nextAllowed.clear();
	}

	void CombatVoiceGate::Prune(const GameTime now)
	{
		for (auto it = m_nextAllowed.begin(); it != m_nextAllowed.end();)
		{
			if (it->second <= now)
			{
				it = m_nextAllowed.erase(it);
			}
			else
			{
				++it;
			}
		}
	}
}
