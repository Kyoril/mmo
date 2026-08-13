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
#include "friend_mgr.h"
#include "chat_channel_mgr.h"
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
#include "base/crash_handler.h"
#include "base/filesystem.h"
#include "base/timer_queue.h"
#include "network/shutdown_signals.h"
#include "base/database_pool.h"

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
		/// Carries an interface type into makeWorker, which cannot take an explicit template
		/// argument because it is a lambda.
		template <class T>
		struct InterfaceTag
		{
			using type = T;
		};

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

		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Crash handler
		/////////////////////////////////////////////////////////////////////////////////////////////////

		// Installed once the log file exists so the handler can flush it. Without that flush the tail
		// of the log covering the crash is lost whenever log buffering is enabled, which is exactly
		// the part needed to understand the crash.
		{
			CrashHandlerConfig crashConfig;
			crashConfig.applicationName = "realm_server";
			crashConfig.outputDirectory = std::filesystem::path(config.logFileName).parent_path();
			crashConfig.onCrash = [this](std::vector<std::string>& /*details*/)
			{
				if (m_logFile.is_open())
				{
					m_logFile.flush();
				}
			};

			InstallCrashHandler(std::move(crashConfig));
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

		auto databasePool = DatabasePool<MySQLDatabase>::Create(config.mysqlPoolSize,
			[&config, &project](std::size_t index) -> std::unique_ptr<MySQLDatabase>
			{
				auto database = std::make_unique<MySQLDatabase>(mmo::mysql::DatabaseInfo{
					config.mysqlHost,
					config.mysqlPort,
					config.mysqlUser,
					config.mysqlPassword,
					config.mysqlDatabase,
					config.mysqlUpdatePath
					}, project);

				if (!database->Connect())
				{
					return nullptr;
				}

				// Only the first connection migrates. Running the update scripts from several
				// connections at once races on the history table.
				if (index == 0 && !database->ApplyMigrations())
				{
					return nullptr;
				}

				return database;
			},
			[](MySQLDatabase& database) { database.KeepAlive(); });

		if (!databasePool)
		{
			ELOG("Could not load the database");
			return 1;
		}

		ILOG("Database ready with " << databasePool->Size() << " connection(s)");

		const auto sync = [&ioService](std::function<void()> action) { ioService.post(std::move(action)); };

		// One adaptor per narrow interface. MySQLDatabase implements all of them, so each adaptor
		// is a static upcast performed on whichever pool thread the key routed to.
		const auto makeWorker = [&databasePool](auto interfaceTag)
		{
			using TInterface = typename decltype(interfaceTag)::type;
			return [&databasePool](uint64 key, std::function<void(TInterface&)> work)
			{
				databasePool->Dispatch(key, [work = std::move(work)](MySQLDatabase& database) { work(database); });
			};
		};

		AsyncDatabase asyncDatabase{ makeWorker(InterfaceTag<IDatabase>{}), sync };

		// Narrow async wrappers — each subsystem gets only the interface it needs.
		AsyncGuildDatabase  asyncGuildDb{ makeWorker(InterfaceTag<IGuildDatabase>{}), sync };
		AsyncFriendDatabase asyncFriendDb{ makeWorker(InterfaceTag<IFriendDatabase>{}), sync };
		AsyncMOTDDatabase   asyncMotdDb{ makeWorker(InterfaceTag<IMOTDDatabase>{}), sync };
		AsyncChatChannelDatabase asyncChatChannelDb{ makeWorker(InterfaceTag<IChatChannelDatabase>{}), sync };

		IdGenerator<uint64> groupIdGenerator{ 1 };


		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Create the world service
		/////////////////////////////////////////////////////////////////////////////////////////////////
		
		// Create the MOTD manager
		auto motdManager = std::make_unique<MOTDManager>(asyncMotdDb);

		PlayerManager playerManager{ config.maxPlayers, *motdManager };

		// Initialize asset registry
		AssetRegistry::Initialize(config.dataFolder, {});

		// Restore groups
		const auto startTime = GetAsyncTimeMs();
		ILOG("Loading player groups...");
		if (auto groupIds = databasePool->Primary().ListGroups())
		{
			for (auto& groupId : *groupIds)
			{
				// Create a new group
				auto group = std::make_shared<PlayerGroup>(groupId, playerManager, AsyncGroupDatabase{ makeWorker(InterfaceTag<IGroupDatabase>{}), sync }, timerQueue);
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
		const auto createWorld = [&worldManager, &playerManager, &asyncDatabase, &project, &timerQueue, &config](std::shared_ptr<World::Client> connection)
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

			if (worldManager.HasCapacityBeenReached())
			{
				WLOG("Rejecting world node connection from " << address << ": the configured capacity of "
					<< config.maxWorlds << " has been reached");
				connection->close();
				return;
			}

			auto world = std::make_shared<World>(timerQueue, worldManager, playerManager, asyncDatabase, connection, address.to_string(), project);
			ILOG("Incoming world node connection from " << address);
			worldManager.AddWorld(std::move(world));

			// Now we can start receiving data
			connection->startReceiving();
		};

		// Keeps the io_service alive while the realm has no outstanding io of its own. Held by
		// pointer so the shutdown handler can release it -- a stack object could not be.
		auto ioWork = std::make_shared<asio::io_context::work>(ioService);

		// Load all guilds
		GuildMgr guildMgr{ asyncGuildDb, playerManager };
		guildMgr.LoadGuilds();

		// Initialize friend manager
		FriendMgr friendMgr{ asyncFriendDb, playerManager };
		friendMgr.LoadAllFriendships();

		// Initialize chat channel manager from game data
		ChannelMgr channelMgr{ project, asyncChatChannelDb, playerManager, timerQueue };
		channelMgr.Initialize();

		

		// Wait for all guilds to load.
		//
		// Only the io service is pumped now: the query itself runs on a pool thread, and its
		// result comes back through the sync dispatcher, which posts here. run_one() blocks until
		// that arrives rather than spinning.
		while (!guildMgr.GuildsLoaded())
		{
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
		const auto createPlayer = [&playerManager, &worldManager, &asyncDatabase, &loginConnector, &project, &timerQueue, &groupIdGenerator, &guildMgr, &friendMgr, &channelMgr, &config](std::shared_ptr<Player::Client> connection)
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

			if (playerManager.HasPlayerCapacityBeenReached())
			{
				WLOG("Rejecting player connection from " << address << ": the configured capacity of "
					<< config.maxPlayers << " has been reached");
				connection->close();
				return;
			}

			// See the login server: the largest client->realm packet is a chat message, so
			// 64 KiB is generous. Server<->server links keep the 16 MiB default.
			connection->SetMaxReceiveBufferSize(64 * 1024);

			auto player = std::make_shared<Player>(timerQueue, playerManager, worldManager, *loginConnector, asyncDatabase, connection, address.to_string(), project, groupIdGenerator, guildMgr, friendMgr, channelMgr);
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
			// Slot 0. The REST handlers call this synchronously on the io thread while a pool
			// thread uses the same instance; both take m_databaseMutex, exactly as the io thread
			// and the db thread did before.
			databasePool->Primary(),
			*motdManager
		);


		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Graceful shutdown
		/////////////////////////////////////////////////////////////////////////////////////////////////

		// Declared before the handler that captures it, so the handler can cancel its own wait.
		std::unique_ptr<asio::signal_set> shutdownSignals;
		shutdownSignals = InstallShutdownHandler(ioService,
			[&worldServer, &playerServer, &webService, &playerManager, &worldManager,
			 &shutdownSignals, &timerQueue, &ioWork, &databasePool]()
		{
			ILOG("Shutdown signal received - stopping cleanly");

			// Stop taking new work first, so nothing arrives while the rest winds down. The web
			// service counts: its acceptor holds a pending async_accept that would otherwise keep
			// the io_service busy forever.
			playerServer->Stop();
			worldServer->Stop();
			if (webService)
			{
				webService->Stop();
			}

			// An armed timer is outstanding io_service work, so the queue has to be stopped or
			// the service never runs dry.
			timerQueue.Stop();

			// Then close what is already connected.
			playerManager.DisconnectAll();
			worldManager.DisconnectAll();

			// The pending async_wait on the signal set is itself outstanding io_service work.
			// Left armed, the service would never run dry and ioService.run() would never return.
			if (shutdownSignals)
			{
				asio::error_code error;
				shutdownSignals->cancel(error);
			}

			// Releasing the work guard is what lets the service drain and run() return. Note this
			// is the ONLY thing that winds it down: ioService.stop() would discard the closes
			// above instead of running them.
			ioWork.reset();

			// Stopped last, so nothing can still be queueing database work. The pool drains
			// rather than discards: what is queued at this point is character writes.
			if (databasePool)
			{
				databasePool->Stop();
			}
		});

		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Launch worker threads
		/////////////////////////////////////////////////////////////////////////////////////////////////

		// Single-threaded on purpose. Cross-session access throughout this server (player groups,
		// guilds, chat channels, friends) goes through raw Player pointers taken from the
		// managers, which is only sound while one thread runs all of it. See the threading note
		// on PlayerManager before changing this.
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

		// Also run the io service on the main thread as well
		ioService.run();

		// Wait for network threads to finish execution
		for (auto& thread : networkThreads)
		{
			thread.join();
		}

		// Already stopped by the shutdown handler on the signal path; Stop() is idempotent and
		// this covers the paths that exit without a signal.
		databasePool->Stop();

		ILOG("Realm server stopped cleanly");

		return 0;
	}
}
