// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_core/bot_session.h"
#include "scenario_engine.h"

#include "base/typedefs.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"

#include "nlohmann/json.hpp"
#include <cxxopts/cxxopts.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace mmo
{
	bool LoadE2eConfig(const fs::path& path, BotConfig& outConfig)
	{
		std::ifstream file(path);
		if (!file)
		{
			ELOG("Failed to open e2e client config file: " << path);
			return false;
		}

		json data;
		file >> data;

		outConfig.loginHost = data.value("loginHost", outConfig.loginHost);
		outConfig.loginPort = data.value("loginPort", outConfig.loginPort);
		outConfig.username = data.value("accountName", outConfig.username);
		outConfig.password = data.value("accountPassword", outConfig.password);
		outConfig.realmName = data.value("realmName", outConfig.realmName);
		outConfig.characterName = data.value("characterName", outConfig.characterName);
		outConfig.createCharacter = data.value("createCharacter", outConfig.createCharacter);
		outConfig.race = data.value("race", outConfig.race);
		outConfig.characterClass = data.value("class", outConfig.characterClass);
		outConfig.gender = data.value("gender", outConfig.gender);
		return true;
	}

	void InitializeLogging()
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
}

int main(int argc, char** argv)
{
	using namespace mmo;

	InitializeLogging();

	cxxopts::Options options("e2e_client", "Headless scenario test client for the mmo project.");
	options.add_options()
		("c,config", "Path to e2e client config JSON (written by tools/e2e/e2e_up.ps1)",
			cxxopts::value<std::string>()->default_value("e2e/runtime/e2e_client.json"))
		("character", "Character name override", cxxopts::value<std::string>())
			("class", "Character class id override used when creating the character", cxxopts::value<uint32>())
		("s,script", "Path to a Lua scenario script to execute", cxxopts::value<std::string>())
		("transcript", "Path of the JSONL transcript to write for a scenario run", cxxopts::value<std::string>()->default_value(""))
		("t,timeout", "Scenario watchdog timeout in seconds", cxxopts::value<uint32>()->default_value("120"))
		("smoke", "Connect, enter world and stay online for --smoke-seconds, then exit")
		("smoke-seconds", "Duration of the smoke test in seconds", cxxopts::value<uint32>()->default_value("60"))
		("connect-timeout", "Timeout in seconds for reaching the world", cxxopts::value<uint32>()->default_value("60"))
		("h,help", "Show help");

	std::string configPath;
	std::string characterOverride;
	int32 classOverride = -1;
	std::string scriptPath;
	std::string transcriptPath;
	bool smokeMode = false;
	uint32 smokeSeconds = 60;
	uint32 connectTimeout = 60;
	uint32 scenarioTimeout = 120;

	try
	{
		const auto results = options.parse(argc, argv);
		if (results.count("help") > 0)
		{
			std::cout << options.help() << '\n';
			return bot_exit_code::Success;
		}

		configPath = results["config"].as<std::string>();
		smokeMode = results.count("smoke") > 0;
		smokeSeconds = results["smoke-seconds"].as<uint32>();
		connectTimeout = results["connect-timeout"].as<uint32>();
		scenarioTimeout = results["timeout"].as<uint32>();
		transcriptPath = results["transcript"].as<std::string>();
		if (results.count("character") > 0)
		{
			characterOverride = results["character"].as<std::string>();
		}
		if (results.count("class") > 0)
		{
			classOverride = static_cast<int32>(results["class"].as<uint32>());
		}
		if (results.count("script") > 0)
		{
			scriptPath = results["script"].as<std::string>();
		}
	}
	catch (const cxxopts::OptionException& e)
	{
		std::cerr << "Failed to parse command line: " << e.what() << '\n';
		std::cerr << options.help() << '\n';
		return bot_exit_code::SetupFailed;
	}

	if (!smokeMode && scriptPath.empty())
	{
		std::cerr << "Nothing to do: pass --smoke or --script <scenario.lua>\n";
		std::cerr << options.help() << '\n';
		return bot_exit_code::SetupFailed;
	}

	if (!scriptPath.empty() && !fs::exists(scriptPath))
	{
		ELOG("Scenario script not found: " << scriptPath);
		return bot_exit_code::SetupFailed;
	}

	// Defaults tuned for the e2e stack; everything can be overridden via the config file.
	BotConfig config;
	config.loginHost = "127.0.0.1";
	// Character names must be 3-12 letters (no digits/symbols - realm-side validation).
	config.characterName = "Smoke";
	config.createCharacter = true;
	config.race = 0;
	config.characterClass = 0;	// Mage - has mana, so scenarios can cast spells
	config.gender = 0;

	if (!LoadE2eConfig(configPath, config))
	{
		return bot_exit_code::SetupFailed;
	}

	if (!characterOverride.empty())
	{
		config.characterName = characterOverride;
	}

	if (classOverride >= 0)
	{
		config.characterClass = static_cast<uint8>(classOverride);
	}

	// The scenario client runs exactly one bot, but the session no longer owns the io service or
	// the navigation meshes -- the swarm host shares both across many sessions, so they are
	// created by the host in either case.
	asio::io_service io;
	auto navService = std::make_shared<BotNavService>();

	BotSession session(io, std::move(navService), std::move(config));
	if (!session.Start())
	{
		return bot_exit_code::SetupFailed;
	}

	bot_exit_code::Type result = session.WaitForWorld(connectTimeout);
	if (result == bot_exit_code::Success)
	{
		if (smokeMode)
		{
			result = session.RunSmoke(smokeSeconds);
		}
		else
		{
			result = ScenarioEngine::Run(session, scriptPath, scenarioTimeout, transcriptPath);
		}
	}

	session.Shutdown();
	return result;
}
