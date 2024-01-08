// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "program.h"
#include "version.h"

#include "asio.hpp"

#include "log/log_std_stream.h"
#include "log/log_entry.h"
#include "log/default_log_levels.h"
#include "auth_protocol/auth_server.h"
#include "configuration.h"
#include "realm_connector.h"
#include "game/world_instance_manager.h"
#include "player_manager.h"
#include "game/game_object_factory.h"

#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <vector>
#include <thread>

#include "base/filesystem.h"
#include "base/timer_queue.h"
#include "proto_data/project.h"

namespace mmo
{
	bool Program::ShouldRestart = false;

	namespace
	{
		static std::string generateLogFileName(const std::string &prefix)
		{
			std::ostringstream logFileNameStrm;
			logFileNameStrm << prefix << "_";

			const auto timeT = time(nullptr);
			logFileNameStrm
				<< std::put_time(std::localtime(&timeT), "%Y-%b-%d_%H-%M-%S")
				<< logFileNameStrm.widen(' ')
				<< ".log";

			// Try to create log directory if not existing yet
			std::filesystem::create_directories(
				std::filesystem::path(logFileNameStrm.str()).remove_filename());

			return logFileNameStrm.str();
		}
	}

	int32 Program::run(const std::string& configFileName)
	{
		// This is the main ioService object
		asio::io_service ioService;

		// This is the main timer queue
		TimerQueue timerQueue{ ioService };

		// The database service object and keep-alive object
		asio::io_service dbService;

		// Keep the database service alive / busy until this object is alive
		auto dbWork = std::make_shared<asio::io_context::work>(dbService);

		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Load config file
		/////////////////////////////////////////////////////////////////////////////////////////////////

		Configuration config;
		if (!config.load(configFileName))
		{
			return 1;
		}

		/////////////////////////////////////////////////////////////////////////////////////////////////
		// File log setup
		/////////////////////////////////////////////////////////////////////////////////////////////////

		scoped_connection genericLogConnection;
		if (config.isLogActive)
		{
			auto logOptions = g_DefaultFileLogOptions;
			logOptions.alwaysFlush = !config.isLogFileBuffering;

			// Setup the log file connection after opening the log file
			m_logFile.open(generateLogFileName(config.logFileName).c_str(), std::ios::app);
			if (m_logFile)
			{
				genericLogConnection = g_DefaultLog.signal().connect(
					[this, logOptions](const LogEntry& entry)
					{
						printLogEntry(m_logFile, entry, logOptions);
					});
			}
		}

		// Display version infos
		ILOG("Version " << Major << "." << Minor << "." << Build << "." << Revision << " (Commit: " << GitCommit << ")");
		ILOG("Last Change: " << GitLastChange);

		// Load game data
		proto::Project project;
		if (!project.load(config.dataFolder))
		{
			ELOG("Failed to load project from folder '" << config.dataFolder << "'!");
			return 1;
		}

		PlayerManager playerManager;

		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Database setup
		/////////////////////////////////////////////////////////////////////////////////////////////////

		// TODO


		/////////////////////////////////////////////////////////////////////////////////////////////////
		// World Instance manager setup
		/////////////////////////////////////////////////////////////////////////////////////////////////

		WorldInstanceManager worldInstanceManager{ ioService };

		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Game service setup
		/////////////////////////////////////////////////////////////////////////////////////////////////

		auto realmConnector =
			std::make_shared<RealmConnector>(
				std::ref(ioService), 
				std::ref(timerQueue), 
				std::cref(config.hostedMaps),
				std::ref(playerManager),
				std::ref(worldInstanceManager),
				std::make_unique<GameObjectFactory>(),
				project);
		realmConnector->Login(config.realmServerAddress, config.realmServerPort, config.realmServerAuthName, config.realmServerPassword);

		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Create the web service
		/////////////////////////////////////////////////////////////////////////////////////////////////

		// TODO



		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Launch worker threads
		/////////////////////////////////////////////////////////////////////////////////////////////////

		// Create worker threads to process networking asynchronously (may be 0 as well)
		const auto maxNetworkThreads = 0u;
		ILOG("Running with " << maxNetworkThreads + 1 << " network threads");

		// Eventually generate worker threads
		std::vector<std::thread> networkThreads{ maxNetworkThreads };
		if (maxNetworkThreads > 0)
		{
			std::generate_n(networkThreads.begin(), maxNetworkThreads, [&ioService]() {
				return std::thread{ [&ioService] { ioService.run(); } };
			});
		}

		// Run the database service thread
		std::thread dbThread{ [&dbService]() { dbService.run(); } };

		// Also run the io service on the main thread as well
		ioService.run();

		// Wait for network threads to finish execution
		for (auto& thread : networkThreads)
		{
			thread.join();
		}

		// Terminate the database worker and wait for pending database operations to finish
		dbWork.reset();
		dbThread.join();

		return 0;
	}
}
