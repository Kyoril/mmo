// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "bot_chat/chat_budget.h"

#include <filesystem>

using namespace mmo;

namespace
{
	/// 2026-10-18 12:00:00 UTC.
	constexpr std::time_t Noon = 1792324800;
	constexpr std::time_t Hour = 3600;
	constexpr std::time_t Day = 24 * Hour;

	ChatPricing HaikuPricing()
	{
		ChatPricing pricing;
		REQUIRE(LookupChatPricing("claude-haiku-5-5", pricing));
		return pricing;
	}
}

TEST_CASE("Unknown models have no price, so they cannot be budgeted by accident", "[chat_budget]")
{
	ChatPricing pricing;
	CHECK_FALSE(LookupChatPricing("claude-made-up-9", pricing));
	CHECK(LookupChatPricing("claude-haiku-5-5", pricing));
	CHECK(pricing.CostUsd(1000000, 0) == Approx(0.10));
	CHECK(pricing.CostUsd(0, 1000000) == Approx(0.50));
}

TEST_CASE("Reservations stop at the daily cap", "[chat_budget]")
{
	ChatBudgetLimits limits;
	limits.dailyUsd = 0.001;
	limits.monthlyUsd = 1.0;
	limits.maxRequestsPerHour = 1000;
	ChatBudget budget(limits, HaikuPricing(), "");

	// 1000 in + 100 out on Haiku 5.5 is $0.00015, so six fit under $0.001 and the seventh does not.
	double reserved = 0.0;
	int accepted = 0;
	while (budget.TryReserve(1000, 100, Noon, reserved))
	{
		++accepted;
		REQUIRE(accepted < 100);
	}

	CHECK(accepted == 6);
	CHECK(budget.GetSpentToday() <= limits.dailyUsd);

	SECTION("a new UTC day starts from zero")
	{
		CHECK(budget.TryReserve(1000, 100, Noon + Day, reserved));
	}
}

TEST_CASE("Settling refunds a reservation down to the real cost", "[chat_budget]")
{
	ChatBudget budget(ChatBudgetLimits(), HaikuPricing(), "");

	double reserved = 0.0;
	REQUIRE(budget.TryReserve(1000, 100, Noon, reserved));
	CHECK(budget.GetSpentToday() == Approx(reserved));

	const double actual = budget.GetPricing().CostUsd(400, 20);
	budget.Settle(reserved, actual, Noon);
	CHECK(budget.GetSpentToday() == Approx(actual));

	SECTION("an unbilled request is refunded completely")
	{
		double second = 0.0;
		REQUIRE(budget.TryReserve(1000, 100, Noon, second));
		budget.Settle(second, 0.0, Noon);
		CHECK(budget.GetSpentToday() == Approx(actual));
	}
}

TEST_CASE("The hourly request cap holds regardless of cost", "[chat_budget]")
{
	ChatBudgetLimits limits;
	limits.maxRequestsPerHour = 3;
	ChatBudget budget(limits, HaikuPricing(), "");

	double reserved = 0.0;
	CHECK(budget.TryReserve(10, 10, Noon, reserved));
	CHECK(budget.TryReserve(10, 10, Noon, reserved));
	CHECK(budget.TryReserve(10, 10, Noon, reserved));
	CHECK_FALSE(budget.TryReserve(10, 10, Noon + 60, reserved));
	CHECK(budget.TryReserve(10, 10, Noon + Hour, reserved));
}

TEST_CASE("The monthly cap holds across days", "[chat_budget]")
{
	ChatBudgetLimits limits;
	limits.dailyUsd = 1.0;
	limits.monthlyUsd = 0.0004;
	limits.maxRequestsPerHour = 1000;
	ChatBudget budget(limits, HaikuPricing(), "");

	double reserved = 0.0;
	CHECK(budget.TryReserve(1000, 100, Noon, reserved));
	CHECK(budget.TryReserve(1000, 100, Noon + Day, reserved));
	CHECK_FALSE(budget.TryReserve(1000, 100, Noon + 2 * Day, reserved));
}

TEST_CASE("The ledger survives a restart", "[chat_budget]")
{
	const std::filesystem::path path = std::filesystem::temp_directory_path() / "mmo_chat_budget_test.json";
	std::filesystem::remove(path);

	ChatBudgetLimits limits;
	limits.dailyUsd = 0.0004;
	limits.maxRequestsPerHour = 1000;

	{
		ChatBudget budget(limits, HaikuPricing(), path.string());
		double reserved = 0.0;
		CHECK(budget.TryReserve(1000, 100, Noon, reserved));
		CHECK(budget.TryReserve(1000, 100, Noon, reserved));
	}

	// A restarted swarm must not get a fresh allowance for the same day.
	ChatBudget restarted(limits, HaikuPricing(), path.string());
	double reserved = 0.0;
	CHECK_FALSE(restarted.TryReserve(1000, 100, Noon, reserved));
	CHECK(restarted.GetRequestsTotal() == 2);

	std::filesystem::remove(path);
}
