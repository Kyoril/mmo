// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <ctime>
#include <string>

namespace mmo
{
	/// Price of a language model in US dollars per million tokens.
	struct ChatPricing final
	{
		double inputUsdPerMTok { 0.0 };
		double outputUsdPerMTok { 0.0 };

		/// What a request with the given token counts costs.
		[[nodiscard]] double CostUsd(uint32 inputTokens, uint32 outputTokens) const;
	};

	/// Looks up the list price of a known model.
	///
	/// Deliberately a closed list: a budget computed with a guessed price is not a budget, so an
	/// unknown model has to be given its prices explicitly.
	///
	/// @return False if the model is not known.
	[[nodiscard]] bool LookupChatPricing(const std::string& model, ChatPricing& outPricing);

	/// Hard limits on what the swarm may spend on chat.
	struct ChatBudgetLimits final
	{
		/// US dollars per UTC day.
		double dailyUsd { 0.25 };

		/// US dollars per UTC calendar month.
		double monthlyUsd { 5.0 };

		/// Requests per UTC hour, whatever they cost.
		uint32 maxRequestsPerHour { 120 };
	};

	/// Spend ledger that enforces ChatBudgetLimits across restarts and processes.
	///
	/// The cap holds because every request is charged its worst-case cost *before* it is sent and
	/// refunded down to its real cost once the answer says what it was. A crash, a hang or a
	/// second process sharing the ledger can therefore only make the ledger overstate what was
	/// spent, never understate it.
	///
	/// The ledger is a small JSON file, re-read before every reservation so that several swarm
	/// processes can share one. It is not locked: two processes reserving in the same millisecond
	/// can each see the other's last state, which overshoots by at most one request each.
	class ChatBudget final
	{
	public:
		/// @param limits Caps to enforce.
		/// @param pricing Prices of the model in use.
		/// @param ledgerPath JSON file to persist to. Empty keeps the ledger in memory only, which
		///		is for tests: a cap that resets with every restart caps nothing.
		ChatBudget(ChatBudgetLimits limits, ChatPricing pricing, std::string ledgerPath);

		/// Charges the worst-case cost of a request.
		/// @param maxInputTokens Upper bound of the prompt size.
		/// @param maxOutputTokens The request's max_tokens.
		/// @param now Current wall clock time; decides which day, month and hour are charged.
		/// @param outReservedUsd Amount charged, to hand back to Settle.
		/// @return False if the request would break a cap or the ledger could not be saved.
		[[nodiscard]] bool TryReserve(uint32 maxInputTokens, uint32 maxOutputTokens, std::time_t now, double& outReservedUsd);

		/// Replaces a reservation by the actual cost of the request.
		/// @param reservedUsd What TryReserve charged.
		/// @param actualUsd What the request cost; zero for a request that was rejected unbilled.
		void Settle(double reservedUsd, double actualUsd, std::time_t now);

		[[nodiscard]] const ChatPricing& GetPricing() const { return m_pricing; }
		[[nodiscard]] const ChatBudgetLimits& GetLimits() const { return m_limits; }

		[[nodiscard]] double GetSpentToday() const { return m_daySpentUsd; }
		[[nodiscard]] double GetSpentThisMonth() const { return m_monthSpentUsd; }
		[[nodiscard]] double GetSpentTotal() const { return m_totalSpentUsd; }
		[[nodiscard]] uint32 GetRequestsThisHour() const { return m_hourRequests; }
		[[nodiscard]] uint64 GetRequestsTotal() const { return m_totalRequests; }

	private:
		void Load();
		[[nodiscard]] bool Save() const;

		/// Starts a fresh day, month or hour if the clock has moved past the stored one.
		void Roll(std::time_t now);

	private:
		ChatBudgetLimits m_limits;
		ChatPricing m_pricing;
		std::string m_ledgerPath;

		std::string m_day;
		std::string m_month;
		std::string m_hour;
		double m_daySpentUsd { 0.0 };
		double m_monthSpentUsd { 0.0 };
		double m_totalSpentUsd { 0.0 };
		uint32 m_hourRequests { 0 };
		uint64 m_totalRequests { 0 };
	};
}
