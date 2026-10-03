// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <deque>
#include <unordered_map>

namespace mmo
{
	/// Limits how often a character may file bug reports: at most one per minimum interval and at
	/// most a number per window. Kept in memory per realm process, keyed by character guid so a
	/// relog does not reset it.
	class BugReportRateLimiter final
	{
	public:
		explicit BugReportRateLimiter(const GameTime minIntervalMs = 30 * 1000, const uint32 maxPerWindow = 20, const GameTime windowMs = 60 * 60 * 1000)
			: m_minIntervalMs(minIntervalMs)
			, m_maxPerWindow(maxPerWindow)
			, m_windowMs(windowMs)
		{
		}

		/// Records a report attempt.
		/// @returns true if the character may file the report now, false if it is rate limited.
		bool TryConsume(const uint64 characterGuid, const GameTime now)
		{
			if (m_history.size() > 4096)
			{
				Sweep(now);
			}

			auto& history = m_history[characterGuid];
			Prune(history, now);

			if (!history.empty() && now < history.back() + m_minIntervalMs)
			{
				return false;
			}
			if (history.size() >= m_maxPerWindow)
			{
				return false;
			}

			history.push_back(now);
			return true;
		}

	private:
		void Prune(std::deque<GameTime>& history, const GameTime now) const
		{
			while (!history.empty() && history.front() + m_windowMs <= now)
			{
				history.pop_front();
			}
		}

		void Sweep(const GameTime now)
		{
			for (auto it = m_history.begin(); it != m_history.end();)
			{
				Prune(it->second, now);
				it = it->second.empty() ? m_history.erase(it) : std::next(it);
			}
		}

	private:
		GameTime m_minIntervalMs;
		uint32 m_maxPerWindow;
		GameTime m_windowMs;
		std::unordered_map<uint64, std::deque<GameTime>> m_history;
	};
}
