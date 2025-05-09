// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "program.h"
#include "login_connector.h"
#include "player_manager.h"
#include "player.h"
#include "world_manager.h"
#include "world.h"
#include "mysql_database.h"
#include "configuration.h"
#include "version.h"
#include "web_service.h"
#include "guild_mgr.h"
#include "motd_manager.h"

#include "asio.hpp"

#include "log/log_std_stream.h"
#include "log/log_entry.h"
#include "log/default_log_levels.h"
#include "auth_protocol/auth_protocol.h"
#include "auth_protocol/auth_server.h"
#include "game_protocol/game_protocol.h"
#include "game_protocol/game_server.h"
#include "base/constants.h"
#include "base/filesystem.h"
#include "base/timer_queue.h"

#include "deps/cxxopts/cxxopts.hpp"

#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <vector>
#include <mutex>
#include <thread>

#include "player_group.h"
#include "assets/asset_registry.h"
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

		TimerQueue dbTimerQueue(dbService);

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
					[this, logOptions](const LogEntry & entry)
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

		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Database setup
		/////////////////////////////////////////////////////////////////////////////////////////////////

		auto database = std::make_unique<MySQLDatabase>(mmo::mysql::DatabaseInfo{
			config.mysqlHost,
			config.mysqlPort,
			config.mysqlUser,
			config.mysqlPassword,
			config.mysqlDatabase,
			config.mysqlUpdatePath
			}, project, dbTimerQueue);
		if (!database->Load())
		{
			ELOG("Could not load the database");
			return 1;
		}

		const auto async = [&dbService](Action action) { dbService.post(std::move(action)); };
		const auto sync = [&ioService](Action action) { ioService.post(std::move(action)); };
		AsyncDatabase asyncDatabase{ *database, async, sync };

		IdGenerator<uint64> groupIdGenerator{ 1 };


		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Create the world service
		/////////////////////////////////////////////////////////////////////////////////////////////////
		
		// Create the MOTD manager
		auto motdManager = std::make_unique<MOTDManager>(asyncDatabase);

		PlayerManager playerManager{ config.maxPlayers, *motdManager };

		// Initialize asset registry
		AssetRegistry::Initialize(config.dataFolder, {});

		// Restore groups
		const auto startTime = GetAsyncTimeMs();
		ILOG("Loading player groups...");
		if (auto groupIds = database->ListGroups())
		{
			for (auto& groupId : *groupIds)
			{
				// Create a new group
				auto group = std::make_shared<PlayerGroup>(groupId, playerManager, asyncDatabase, timerQueue);
				group->Preload();

				// Notify the generator about the new group id to avoid overlaps
				groupIdGenerator.NotifyId(groupId);
			}

			const auto endTime = GetAsyncTimeMs();
			ILOG("Successfully loaded " << groupIds->size() << " player groups in " << (endTime - startTime) << " ms");
		}
		else
		{
			ELOG("Could not restore group ids!");
			return 1;
		}

		WorldManager worldManager{ config.maxWorlds };

		// Create the world server
		std::unique_ptr<auth::Server> worldServer;
		try
		{
			worldServer.reset(new mmo::auth::Server(std::ref(ioService), config.worldPort, std::bind(&mmo::auth::Connection::create, std::ref(ioService), nullptr)));
		}
		catch (const mmo::BindFailedException&)
		{
			ELOG("Could not bind on tcp port " << config.worldPort << "! Maybe there is another server instance running on this port?");
			return 1;
		}

		// Careful: Called by multiple threads!
		const auto createWorld = [&worldManager, &playerManager, &asyncDatabase, &project, &timerQueue](std::shared_ptr<World::Client> connection)
		{
			asio::ip::address address;

			try
			{
				address = connection->getRemoteAddress();
			}
			catch (const asio::system_error& error)
			{
				ELOG(error.what());
				return;
			}

			auto world = std::make_shared<World>(timerQueue, worldManager, playerManager, asyncDatabase, connection, address.to_string(), project);
			ILOG("Incoming world node connection from " << address);
			worldManager.AddWorld(std::move(world));

			// Now we can start receiving data
			connection->startReceiving();
		};

		// Keep realm busy
		asio::io_context::work work{ ioService };

		// Load all guilds
		GuildMgr guildMgr{ asyncDatabase, playerManager };
		guildMgr.LoadGuilds();

		

		// Wait for all guilds to load
		while (!guildMgr.GuildsLoaded())
		{
			dbService.run_one();
			ioService.run_one();
		}

		// Start accepting incoming world node connections
		const scoped_connection worldNodeConnected{ worldServer->connected().connect(createWorld) };
		worldServer->startAccept();



		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Login at login server
		/////////////////////////////////////////////////////////////////////////////////////////////////


		// Setup the login connector and connect to the login server
		auto loginConnector = std::make_shared<LoginConnector>(ioService, timerQueue, playerManager);
		if (!loginConnector->Login(config.loginServerAddress, config.loginServerPort, config.realmName, config.realmPasswordHash))
		{
			return 1;
		}

		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Create the player service
		/////////////////////////////////////////////////////////////////////////////////////////////////

		// Create the player server
		std::unique_ptr<game::Server> playerServer;
		try
		{
			playerServer.reset(new mmo::game::Server(std::ref(ioService), config.playerPort, std::bind(&mmo::game::Connection::Create, std::ref(ioService), nullptr)));
		}
		catch (const mmo::BindFailedException &)
		{
			ELOG("Could not bind on tcp port " << config.playerPort << "! Maybe there is another server instance running on this port?");
			return 1;
		}

		// Careful: Called by multiple threads!
		const auto createPlayer = [&playerManager, &worldManager, &asyncDatabase, &loginConnector, &project, &timerQueue, &groupIdGenerator, &guildMgr](std::shared_ptr<Player::Client> connection)
		{
			asio::ip::address address;

			try
			{
				address = connection->getRemoteAddress();
			}
			catch (const asio::system_error &error)
			{
				ELOG(error.what());
				return;
			}

			auto player = std::make_shared<Player>(timerQueue, playerManager, worldManager, *loginConnector, asyncDatabase, connection, address.to_string(), project, groupIdGenerator, guildMgr);
			ILOG("Incoming player connection from " << address);
			playerManager.AddPlayer(std::move(player));

			// Now we can start receiving data
			connection->startReceiving();
		};

		// Start accepting incoming player connections
		const scoped_connection playerConnected{ playerServer->connected().connect(createPlayer) };
		playerServer->startAccept();



		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Create the web service
		/////////////////////////////////////////////////////////////////////////////////////////////////

		auto webService = std::make_unique<WebService>(
			ioService,
			config.webPort,
			config.webPassword,
			playerManager,
			*database,
			*motdManager
		);


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
