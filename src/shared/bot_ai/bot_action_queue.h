// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_next_action.h"

#include "base/typedefs.h"

#include <functional>
#include <string>
#include <vector>

namespace mmo
{
	/// One pending action in the queue, with the time it was pushed so that stale entries can be
	/// dropped rather than run long after whatever asked for them stopped being true.
	struct BotActionBasket final
	{
		std::string action;

		/// Relevance as pushed. Re-pushing an action - to wait for a prerequisite, or as an
		/// alternative - is based on this rather than on the effective value, so that a scaling
		/// multiplier does not compound across ticks.
		float relevance { 0.0f };

		/// What the action was actually worth when it was selected, after multipliers. Only
		/// meaningful on a basket returned by PopBest.
		float effectiveRelevance { 0.0f };

		GameTime pushedMs { 0 };
	};

	/// Pending actions, ordered by relevance rather than by arrival.
	///
	/// The queue is intentionally tiny and linear. It holds at most a handful of entries at any
	/// time - one tick's worth of fired triggers - so a heap would cost more in bookkeeping than
	/// it saves in comparisons, and a linear scan keeps Push able to dedupe by name.
	class BotActionQueue final
	{
	public:
		/// Adds an action, or raises an existing entry for the same action to the higher of the
		/// two relevances. Two triggers asking for the same thing is normal and must not make it
		/// run twice; the more urgent request wins.
		void Push(const std::string& action, float relevance, GameTime nowMs);

		void Push(const BotNextAction& nextAction, GameTime nowMs) { Push(nextAction.action, nextAction.relevance, nowMs); }

		/// Removes and returns the most relevant entry. Ties are broken by insertion order, so a
		/// strategy listing two equally relevant actions gets them in the order it wrote them.
		[[nodiscard]] bool Pop(BotActionBasket& out);

		/// What a queued entry is worth right now. A negative result means the entry is vetoed:
		/// PopBest drops it instead of ranking it.
		using ScoreFn = std::function<float(const std::string& action, float relevance)>;

		/// Removes and returns the entry with the highest score, dropping every vetoed entry it
		/// passes on the way.
		///
		/// Scoring during selection rather than after it is what lets a multiplier reorder the
		/// queue. Popping first and scaling afterwards would mean a doubled Normal action could
		/// never overtake a High one, which makes a scaling multiplier a veto with extra steps.
		[[nodiscard]] bool PopBest(const ScoreFn& score, BotActionBasket& out);

		/// Drops everything pushed longer than maxAgeMs ago. An action that has been outranked
		/// for five seconds is answering a question nobody is asking any more.
		void RemoveExpired(GameTime nowMs, GameTime maxAgeMs);

		void Clear() { m_baskets.clear(); }

		[[nodiscard]] bool IsEmpty() const { return m_baskets.empty(); }
		[[nodiscard]] std::size_t GetSize() const { return m_baskets.size(); }

		/// Relevance currently queued for an action, or a negative value if it is not queued.
		/// Only used by tests and diagnostics.
		[[nodiscard]] float FindRelevance(const std::string& action) const;

	private:
		std::vector<BotActionBasket> m_baskets;

		/// Reused by PopBest so that selecting an action does not allocate.
		std::vector<std::size_t> m_scratchVetoed;
	};
}
