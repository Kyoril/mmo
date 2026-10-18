// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "swarm_host.h"

#include "bot_chat/anthropic_chat_model.h"
#include "bot_chat/chat_budget.h"

#include "base/typedefs.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"

#include "nlohmann/json.hpp"
#include <cxxopts/cxxopts.hpp>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace mmo
{
	namespace
	{
		SwarmHost* g_host = nullptr;

		void handleInterrupt(int)
		{
			// Only ask the loop to stop. Tearing sessions down from a signal handler would race
			// the io service, and the whole point of the shutdown path is that it is orderly.
			if (g_host)
			{
				g_host->Stop();
			}
		}

		void initializeLogging()
		{
			auto options = g_DefaultConsoleLogOptions;
			options.alwaysFlush = true;

			g_DefaultLog.signal().connect([options](const LogEntry& entry) mutable
				{
					static std::mutex coutLogMutex;
					std::scoped_lock lock{ coutLogMutex };
					printLogEntry(std::cout, entry, options);
				});
		}

		/// Reads the connection block written by tools/e2e/e2e_up.ps1, so that a swarm points at
		/// a stack the same way the scenario client does.
		bool loadConnectionConfig(const fs::path& path, BotConfig& outConfig)
		{
			std::ifstream file(path);
			if (!file)
			{
				ELOG("Failed to open swarm config file: " << path);
				return false;
			}

			json data;
			file >> data;

			outConfig.loginHost = data.value("loginHost", outConfig.loginHost);
			outConfig.loginPort = data.value("loginPort", outConfig.loginPort);
			outConfig.realmName = data.value("realmName", outConfig.realmName);
			return true;
		}

		/// One real request through the real budget, without a game server: checks the key, the
		/// network path and the prompt shape for a fraction of a cent.
		int runChatProbe(const SwarmSettings& settings)
		{
			ChatPricing pricing;
			if (!LookupChatPricing(settings.chatModel, pricing))
			{
				ELOG("No known price for model '" << settings.chatModel << "'");
				return 2;
			}

			if (settings.chatApiKey.empty())
			{
				ELOG("ANTHROPIC_API_KEY is not set");
				return 2;
			}

			ChatBudget budget(settings.chatLimits, pricing, settings.chatLedgerPath);
			AnthropicChatModel model(settings.chatApiKey, settings.chatModel);

			ChatCompletionRequest request;
			request.ticket = 1;
			request.maxOutputTokens = 60;
			request.system = "You are a real person playing an online fantasy MMORPG, typing in the in-game chat. "
				"Reply with only the chat message: one short casual line, lowercase, like a player would type.";
			request.prompt = "Chat: General channel.\nRecent messages, oldest first:\nSteve: anyone know where to level at 8?\n"
				"Write your next message in this chat.";

			double reserved = 0.0;
			if (!budget.TryReserve(static_cast<uint32>((request.system.size() + request.prompt.size()) / 3 + 32),
				request.maxOutputTokens, std::time(nullptr), reserved))
			{
				ELOG("The budget refused the probe: $" << budget.GetSpentToday() << " spent today, "
					<< budget.GetRequestsThisHour() << " requests this hour");
				return 1;
			}

			if (!model.Submit(request))
			{
				budget.Settle(reserved, 0.0, std::time(nullptr));
				ELOG("The model did not accept the probe");
				return 1;
			}

			std::vector<ChatCompletionResult> results;
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
			while (results.empty() && std::chrono::steady_clock::now() < deadline)
			{
				model.Poll(results);
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
			model.Stop();

			if (results.empty())
			{
				ELOG("No answer within 60 seconds; the reservation of $" << reserved << " stays charged");
				return 1;
			}

			const ChatCompletionResult& answer = results.front();
			const double actual = answer.reachedModel
				? ((answer.inputTokens || answer.outputTokens) ? pricing.CostUsd(answer.inputTokens, answer.outputTokens) : reserved)
				: 0.0;
			budget.Settle(reserved, actual, std::time(nullptr));

			if (!answer.succeeded)
			{
				ELOG("Probe failed: " << answer.error);
				return 1;
			}

			ILOG("Probe answer: " << answer.text);
			ILOG("Tokens " << answer.inputTokens << " in / " << answer.outputTokens << " out, cost $" << actual
				<< "; spent today $" << budget.GetSpentToday() << " of $" << settings.chatLimits.dailyUsd);
			return 0;
		}
	}
}

int main(int argc, char** argv)
{
	using namespace mmo;

	initializeLogging();

	cxxopts::Options options("bot_swarm", "Runs a population of autonomous bots against a live server stack.");
	options.add_options()
		("c,config", "Connection config JSON, as written by tools/e2e/e2e_up.ps1",
			cxxopts::value<std::string>()->default_value("e2e/runtime/e2e_client.json"))
		("b,bots", "Number of bots to run", cxxopts::value<uint32>()->default_value("10"))
		("roster", "Roster JSON to load and save; empty builds a throwaway roster",
			cxxopts::value<std::string>()->default_value("e2e/runtime/bots/roster.json"))
		("jsonl", "Telemetry output path", cxxopts::value<std::string>()->default_value("e2e/runtime/logs/swarm.jsonl"))
		("duration", "Seconds to run for; 0 runs until interrupted", cxxopts::value<uint32>()->default_value("0"))
		("tick-ms", "Host frame length in milliseconds", cxxopts::value<uint32>()->default_value("50"))
		("login-ramp-ms", "Delay between successive bot logins", cxxopts::value<uint32>()->default_value("250"))
		("account-prefix", "Account name prefix; must match what the accounts were provisioned as",
			cxxopts::value<std::string>()->default_value("swarm"))
		("account-password", "Password shared by every bot account",
			cxxopts::value<std::string>()->default_value("swarmpass"))
		("min-level", "Lowest level a bot is rolled at", cxxopts::value<uint32>()->default_value("1"))
		("max-level", "Highest level a bot is rolled at", cxxopts::value<uint32>()->default_value("10"))
		("faction-template", "Faction template the bots belong to; 0 reads it off the first bot", cxxopts::value<uint32>()->default_value("0"))
		("no-level-roll", "Do not cheat bots to their rolled level; make them earn it")
		("repo-root", "Repository root holding data/editor; empty resolves from the working directory",
			cxxopts::value<std::string>()->default_value(""))
		("chat", "Let bots chat through a language model (reads ANTHROPIC_API_KEY; costs money, capped below)")
		("chat-offline", "With --chat: answer with canned lines instead of calling the model, at no cost")
		("chat-model", "Model id; must be one with a known price", cxxopts::value<std::string>()->default_value("claude-haiku-5-5"))
		("chat-daily-usd", "Hard cap on chat spend per UTC day", cxxopts::value<double>()->default_value("0.25"))
		("chat-monthly-usd", "Hard cap on chat spend per UTC month", cxxopts::value<double>()->default_value("5"))
		("chat-max-per-hour", "Hard cap on model requests per UTC hour", cxxopts::value<uint32>()->default_value("120"))
		("chat-ledger", "Spend ledger shared by every run, so the caps hold across restarts",
			cxxopts::value<std::string>()->default_value("e2e/runtime/bots/chat_ledger.json"))
		("chat-facts", "Optional text file with facts about the game for the bots to know", cxxopts::value<std::string>()->default_value(""))
		("chat-probe", "Send one chat request through the budget, print the answer and its cost, and exit")
		("h,help", "Print help");

	auto result = options.parse(argc, argv);
	if (result.count("help"))
	{
		std::cout << options.help() << std::endl;
		return 0;
	}

	SwarmSettings settings;
	settings.botCount = result["bots"].as<uint32>();
	settings.rosterPath = result["roster"].as<std::string>();
	settings.telemetryPath = result["jsonl"].as<std::string>();
	settings.durationSeconds = result["duration"].as<uint32>();
	settings.tickMs = result["tick-ms"].as<uint32>();
	settings.loginRampMs = result["login-ramp-ms"].as<uint32>();
	settings.accountPrefix = result["account-prefix"].as<std::string>();
	settings.accountPassword = result["account-password"].as<std::string>();
	settings.minLevel = result["min-level"].as<uint32>();
	settings.maxLevel = result["max-level"].as<uint32>();
	settings.playerFactionTemplate = result["faction-template"].as<uint32>();
	settings.applyLevelRoll = result.count("no-level-roll") == 0;
	settings.repoRoot = result["repo-root"].as<std::string>();

	settings.chatEnabled = result.count("chat") != 0;
	settings.chatOffline = result.count("chat-offline") != 0;
	settings.chatModel = result["chat-model"].as<std::string>();
	settings.chatLimits.dailyUsd = result["chat-daily-usd"].as<double>();
	settings.chatLimits.monthlyUsd = result["chat-monthly-usd"].as<double>();
	settings.chatLimits.maxRequestsPerHour = result["chat-max-per-hour"].as<uint32>();
	settings.chatLedgerPath = result["chat-ledger"].as<std::string>();
	settings.chatFactsPath = result["chat-facts"].as<std::string>();

	// From the environment only: a key on the command line ends up in process listings and
	// shell history.
	if (const char* apiKey = std::getenv("ANTHROPIC_API_KEY"))
	{
		settings.chatApiKey = apiKey;
	}

	if (result.count("chat-probe"))
	{
		return runChatProbe(settings);
	}

	if (!loadConnectionConfig(result["config"].as<std::string>(), settings.connectionTemplate))
	{
		return 2;
	}

	for (const std::string& path : { settings.rosterPath, settings.telemetryPath, settings.chatLedgerPath })
	{
		if (path.empty())
		{
			continue;
		}

		const fs::path parent = fs::path(path).parent_path();
		if (!parent.empty())
		{
			std::error_code error;
			fs::create_directories(parent, error);
		}
	}

	SwarmHost host(std::move(settings));
	g_host = &host;
	std::signal(SIGINT, handleInterrupt);
	std::signal(SIGTERM, handleInterrupt);

	if (!host.Prepare())
	{
		return 2;
	}

	host.Run();

	const SwarmCounters& counters = host.GetCounters();
	ILOG("Swarm finished: " << counters.enteredWorld << "/" << counters.botsConfigured
		<< " bots entered the world, peak " << counters.peakOnline << " online, "
		<< counters.kills << " kills, " << counters.deaths << " deaths, "
		<< counters.stuckEvents << " stuck, " << counters.disconnects << " disconnects");

	std::cout << SwarmTelemetry::CountersToJson(counters).dump(2) << std::endl;

	// A swarm that could not get anyone into the world did not test anything, and the caller
	// needs to be able to tell that from a run that simply found nothing wrong.
	return counters.enteredWorld > 0 ? 0 : 1;
}
