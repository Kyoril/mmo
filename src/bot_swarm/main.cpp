// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "swarm_host.h"

#include "base/typedefs.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"

#include "nlohmann/json.hpp"
#include <cxxopts/cxxopts.hpp>

#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>

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
		("min-level", "Lowest level a bot is rolled at", cxxopts::value<uint32>()->default_value("1"))
		("max-level", "Highest level a bot is rolled at", cxxopts::value<uint32>()->default_value("10"))
		("faction-template", "Faction template the bots belong to; 0 reads it off the first bot", cxxopts::value<uint32>()->default_value("0"))
		("no-level-roll", "Do not cheat bots to their rolled level; make them earn it")
		("repo-root", "Repository root holding data/editor; empty resolves from the working directory",
			cxxopts::value<std::string>()->default_value(""))
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
	settings.minLevel = result["min-level"].as<uint32>();
	settings.maxLevel = result["max-level"].as<uint32>();
	settings.playerFactionTemplate = result["faction-template"].as<uint32>();
	settings.applyLevelRoll = result.count("no-level-roll") == 0;
	settings.repoRoot = result["repo-root"].as<std::string>();

	if (!loadConnectionConfig(result["config"].as<std::string>(), settings.connectionTemplate))
	{
		return 2;
	}

	for (const std::string& path : { settings.rosterPath, settings.telemetryPath })
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
