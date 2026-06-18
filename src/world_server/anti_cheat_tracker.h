// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once
#include <deque>
#include "base/typedefs.h" // GameTime = uint64 ms

namespace mmo
{
	struct AntiCheatTracker
	{
		static constexpr int   kKickThreshold = 5;
		static constexpr int64 kWindowMs      = 10'000;

		std::deque<GameTime> m_violations;

		void RecordViolation(GameTime nowMs)
		{
			Prune(nowMs);
			m_violations.push_back(nowMs);
		}

		bool ShouldKick(GameTime nowMs)
		{
			Prune(nowMs);
			return static_cast<int>(m_violations.size()) >= kKickThreshold;
		}

	private:
		void Prune(GameTime nowMs)
		{
			while (!m_violations.empty() && static_cast<int64>(nowMs) - static_cast<int64>(m_violations.front()) > kWindowMs)
				m_violations.pop_front();
		}
	};
}
