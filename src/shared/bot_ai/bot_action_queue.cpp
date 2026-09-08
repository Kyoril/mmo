// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_action_queue.h"

#include <algorithm>
#include <utility>

namespace mmo
{
	void BotActionQueue::Push(const std::string& action, const float relevance, const GameTime nowMs)
	{
		const auto it = std::find_if(m_baskets.begin(), m_baskets.end(), [&action](const BotActionBasket& basket)
			{
				return basket.action == action;
			});

		if (it != m_baskets.end())
		{
			if (relevance > it->relevance)
			{
				it->relevance = relevance;
				it->pushedMs = nowMs;
			}
			return;
		}

		BotActionBasket basket;
		basket.action = action;
		basket.relevance = relevance;
		basket.pushedMs = nowMs;
		m_baskets.push_back(std::move(basket));
	}

	bool BotActionQueue::Pop(BotActionBasket& out)
	{
		if (m_baskets.empty())
		{
			return false;
		}

		auto best = m_baskets.begin();
		for (auto it = std::next(m_baskets.begin()); it != m_baskets.end(); ++it)
		{
			// Strictly greater keeps ties on the earlier entry, so equally relevant actions run
			// in the order the strategy listed them.
			if (it->relevance > best->relevance)
			{
				best = it;
			}
		}

		out = *best;
		out.effectiveRelevance = out.relevance;
		m_baskets.erase(best);
		return true;
	}

	bool BotActionQueue::PopBest(const ScoreFn& score, BotActionBasket& out)
	{
		// Score everything before touching the container. Erasing while scanning would mean
		// tracking the winner through the shifts, and the queue holds a handful of entries -
		// there is nothing to win by being clever here.
		m_scratchVetoed.clear();

		std::size_t bestIndex = m_baskets.size();
		float bestScore = 0.0f;

		for (std::size_t index = 0; index < m_baskets.size(); ++index)
		{
			const float value = score(m_baskets[index].action, m_baskets[index].relevance);
			if (value < 0.0f)
			{
				// Vetoed entries are dropped rather than left in place: the veto cannot lift until
				// the world changes, and the world is only re-read between ticks, so keeping them
				// would just mean re-scoring them on every iteration of the engine loop.
				m_scratchVetoed.push_back(index);
				continue;
			}

			if (bestIndex == m_baskets.size() || value > bestScore)
			{
				bestIndex = index;
				bestScore = value;
			}
		}

		const bool found = bestIndex < m_baskets.size();
		if (found)
		{
			out = m_baskets[bestIndex];
			out.effectiveRelevance = bestScore;
			m_scratchVetoed.push_back(bestIndex);
		}

		// Back to front, so that each erase leaves the earlier indices valid.
		std::sort(m_scratchVetoed.begin(), m_scratchVetoed.end());
		for (auto it = m_scratchVetoed.rbegin(); it != m_scratchVetoed.rend(); ++it)
		{
			m_baskets.erase(m_baskets.begin() + static_cast<std::ptrdiff_t>(*it));
		}

		return found;
	}

	void BotActionQueue::RemoveExpired(const GameTime nowMs, const GameTime maxAgeMs)
	{
		m_baskets.erase(std::remove_if(m_baskets.begin(), m_baskets.end(), [nowMs, maxAgeMs](const BotActionBasket& basket)
			{
				return nowMs >= basket.pushedMs && (nowMs - basket.pushedMs) > maxAgeMs;
			}), m_baskets.end());
	}

	float BotActionQueue::FindRelevance(const std::string& action) const
	{
		const auto it = std::find_if(m_baskets.begin(), m_baskets.end(), [&action](const BotActionBasket& basket)
			{
				return basket.action == action;
			});

		return it == m_baskets.end() ? -1.0f : it->relevance;
	}
}
