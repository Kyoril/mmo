// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "chat_budget.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <utility>

namespace mmo
{
	namespace
	{
		std::tm toUtc(const std::time_t time)
		{
			std::tm result {};
#ifdef _WIN32
			gmtime_s(&result, &time);
#else
			gmtime_r(&time, &result);
#endif
			return result;
		}

		std::string formatUtc(const std::time_t time, const char* format)
		{
			const std::tm utc = toUtc(time);
			char buffer[32] = {};
			std::strftime(buffer, sizeof(buffer), format, &utc);
			return buffer;
		}
	}

	double ChatPricing::CostUsd(const uint32 inputTokens, const uint32 outputTokens) const
	{
		return (static_cast<double>(inputTokens) * inputUsdPerMTok
			+ static_cast<double>(outputTokens) * outputUsdPerMTok) / 1000000.0;
	}

	bool LookupChatPricing(const std::string& model, ChatPricing& outPricing)
	{
		// First-party list prices, US dollars per million tokens.
		struct KnownModel
		{
			const char* id;
			double input;
			double output;
		};

		static const KnownModel knownModels[] = {
			{ "claude-haiku-5-5", 0.10, 0.50 },
			{ "claude-haiku-4-5", 1.00, 5.00 },
			{ "claude-sonnet-5-5", 2.00, 10.00 },
		};

		for (const KnownModel& known : knownModels)
		{
			if (model == known.id)
			{
				outPricing.inputUsdPerMTok = known.input;
				outPricing.outputUsdPerMTok = known.output;
				return true;
			}
		}

		return false;
	}

	ChatBudget::ChatBudget(ChatBudgetLimits limits, ChatPricing pricing, std::string ledgerPath)
		: m_limits(limits)
		, m_pricing(pricing)
		, m_ledgerPath(std::move(ledgerPath))
	{
		Load();
	}

	bool ChatBudget::TryReserve(const uint32 maxInputTokens, const uint32 maxOutputTokens, const std::time_t now, double& outReservedUsd)
	{
		outReservedUsd = 0.0;

		// Another process may have spent since this one last looked.
		Load();
		Roll(now);

		const double reserve = m_pricing.CostUsd(maxInputTokens, maxOutputTokens);
		if (m_hourRequests >= m_limits.maxRequestsPerHour
			|| m_daySpentUsd + reserve > m_limits.dailyUsd
			|| m_monthSpentUsd + reserve > m_limits.monthlyUsd)
		{
			return false;
		}

		m_daySpentUsd += reserve;
		m_monthSpentUsd += reserve;
		m_totalSpentUsd += reserve;
		++m_hourRequests;
		++m_totalRequests;

		if (!Save())
		{
			// Spending without a record of it is how a cap stops being one.
			m_daySpentUsd -= reserve;
			m_monthSpentUsd -= reserve;
			m_totalSpentUsd -= reserve;
			--m_hourRequests;
			--m_totalRequests;
			return false;
		}

		outReservedUsd = reserve;
		return true;
	}

	void ChatBudget::Settle(const double reservedUsd, const double actualUsd, const std::time_t now)
	{
		Load();
		Roll(now);

		// Never refund more than was reserved: a response claiming a larger cost than the worst
		// case is charged in full.
		const double delta = std::max(actualUsd, 0.0) - reservedUsd;
		m_daySpentUsd = std::max(0.0, m_daySpentUsd + delta);
		m_monthSpentUsd = std::max(0.0, m_monthSpentUsd + delta);
		m_totalSpentUsd = std::max(0.0, m_totalSpentUsd + delta);
		static_cast<void>(Save());
	}

	void ChatBudget::Roll(const std::time_t now)
	{
		const std::string day = formatUtc(now, "%Y-%m-%d");
		const std::string month = formatUtc(now, "%Y-%m");
		const std::string hour = formatUtc(now, "%Y-%m-%dT%H");

		if (day != m_day)
		{
			m_day = day;
			m_daySpentUsd = 0.0;
		}

		if (month != m_month)
		{
			m_month = month;
			m_monthSpentUsd = 0.0;
		}

		if (hour != m_hour)
		{
			m_hour = hour;
			m_hourRequests = 0;
		}
	}

	void ChatBudget::Load()
	{
		if (m_ledgerPath.empty())
		{
			return;
		}

		std::ifstream file(m_ledgerPath);
		if (!file)
		{
			return;
		}

		const nlohmann::json data = nlohmann::json::parse(file, nullptr, false);
		if (data.is_discarded() || !data.is_object())
		{
			return;
		}

		m_day = data.value("day", std::string());
		m_month = data.value("month", std::string());
		m_hour = data.value("hour", std::string());
		m_daySpentUsd = data.value("daySpentUsd", 0.0);
		m_monthSpentUsd = data.value("monthSpentUsd", 0.0);
		m_totalSpentUsd = data.value("totalSpentUsd", 0.0);
		m_hourRequests = data.value("hourRequests", 0u);
		m_totalRequests = data.value("totalRequests", static_cast<uint64>(0));
	}

	bool ChatBudget::Save() const
	{
		if (m_ledgerPath.empty())
		{
			return true;
		}

		const nlohmann::json data = {
			{ "day", m_day },
			{ "month", m_month },
			{ "hour", m_hour },
			{ "daySpentUsd", m_daySpentUsd },
			{ "monthSpentUsd", m_monthSpentUsd },
			{ "totalSpentUsd", m_totalSpentUsd },
			{ "hourRequests", m_hourRequests },
			{ "totalRequests", m_totalRequests },
			{ "limits", {
				{ "dailyUsd", m_limits.dailyUsd },
				{ "monthlyUsd", m_limits.monthlyUsd },
				{ "maxRequestsPerHour", m_limits.maxRequestsPerHour } } },
		};

		// Written aside and renamed over, so that a crash mid-write cannot leave a truncated
		// ledger that the next start reads as "nothing spent yet".
		const std::filesystem::path target(m_ledgerPath);
		const std::filesystem::path temp = target.string() + ".tmp";

		std::error_code error;
		if (target.has_parent_path())
		{
			std::filesystem::create_directories(target.parent_path(), error);
		}

		{
			std::ofstream file(temp, std::ios::trunc);
			if (!file)
			{
				return false;
			}

			file << data.dump(2);
			if (!file)
			{
				return false;
			}
		}

		std::filesystem::rename(temp, target, error);
		return !error;
	}
}
