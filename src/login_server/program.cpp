// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "program.h"
#include "configuration.h"
#include "version.h"
#include "mysql_database.h"
#include "player_manager.h"
#include "player.h"
#include "realm_manager.h"
#include "realm.h"
#include "web_service.h"

#include "asio.hpp"

#include "log/log_std_stream.h"
#include "log/log_entry.h"
#include "log/default_log_levels.h"
#include "auth_protocol/auth_protocol.h"
#include "auth_protocol/auth_server.h"
#include "base/constants.h"
#include "base/clock.h"
#include "base/countdown.h"

#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <vector>
#include <mutex>
#include <thread>

#include "base/filesystem.h"
#include "base/timer_queue.h"
#include "network/shutdown_signals.h"
#include "base/database_pool.h"

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

		TimerQueue timerQueue(ioService);

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



		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Database setup
		/////////////////////////////////////////////////////////////////////////////////////////////////

		auto databasePool = DatabasePool<MySQLDatabase>::Create(config.mysqlPoolSize,
			[&config](std::size_t index) -> std::unique_ptr<MySQLDatabase>
			{
				auto database = std::make_unique<MySQLDatabase>(mysql::DatabaseInfo{
					config.mysqlHost,
					config.mysqlPort,
					config.mysqlUser,
					config.mysqlPassword,
					config.mysqlDatabase,
					config.mysqlUpdatePath
				});

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

		const auto sync = [&ioService](std::function<void()> action) { ioService.post(std::move(action)); };
		const auto async = [&databasePool](uint64 key, std::function<void(IDatabase&)> work)
		{
			databasePool->Dispatch(key, [work = std::move(work)](MySQLDatabase& database) { work(database); });
		};

		AsyncDatabase asyncDatabase{ async, sync };

		ILOG("Database ready with " << databasePool->Size() << " connection(s)");

		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Create the realm service
		/////////////////////////////////////////////////////////////////////////////////////////////////

		RealmManager realmManager{ config.maxRealms };

		// Create the realm server
		std::unique_ptr<auth::Server> realmServer;
		try
		{
			realmServer.reset(new auth::Server(std::ref(ioService), config.realmPort, std::bind(&auth::Connection::create, std::ref(ioService), nullptr)));
		}
		catch (const BindFailedException &)
		{
			ELOG("Could not bind on tcp port " << config.realmPort << "! Maybe there is another server instance running on this port?");
			return 1;
		}

		// Careful: Called by multiple threads!
		const auto createRealm = [&realmManager, &asyncDatabase, &timerQueue, &config](std::shared_ptr<Realm::Client> connection)
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

			if (realmManager.HasCapacityBeenReached())
			{
				WLOG("Rejecting realm connection from " << address << ": the configured capacity of "
					<< config.maxRealms << " has been reached");
				connection->close();
				return;
			}

			auto realm = std::make_shared<Realm>(realmManager, asyncDatabase, connection, address.to_string(), timerQueue);
			ILOG("Incoming realm connection from " << address);
			realmManager.AddRealm(std::move(realm));

			connection->startReceiving();
		};

		// Start accepting incoming realm connections
		const scoped_connection realmConnected{ realmServer->connected().connect(createRealm) };
		realmServer->startAccept();



		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Create the player service
		/////////////////////////////////////////////////////////////////////////////////////////////////

		PlayerManager playerManager{ config.maxPlayers };

		// Create the player server
		std::unique_ptr<auth::Server> playerServer;
		try
		{
			playerServer.reset(new mmo::auth::Server(std::ref(ioService), config.playerPort, std::bind(&mmo::auth::Connection::create, std::ref(ioService), nullptr)));
		}
		catch (const mmo::BindFailedException &)
		{
			ELOG("Could not bind on tcp port " << config.playerPort << "! Maybe there is another server instance running on this port?");
			return 1;
		}
		
		// Careful: Called by multiple threads!
		const auto createPlayer = [&playerManager, &realmManager, &asyncDatabase, &config](std::shared_ptr<Player::Client> connection)
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

			// Client connections are anonymous and reachable from the internet, so they get a far
			// tighter bound than the 16 MiB protocol ceiling. The largest packet a login client
			// sends is the logon proof at ~52 bytes; 64 KiB leaves several orders of magnitude of
			// headroom while making the buffer irrelevant as an attack surface.
			connection->SetMaxReceiveBufferSize(64 * 1024);

			auto player = std::make_shared<Player>(playerManager, realmManager, asyncDatabase, connection, address.to_string());
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
			realmManager,
			// Slot 0. The REST handlers call this synchronously on an io thread while a pool
			// thread uses the same instance; both take m_databaseMutex, exactly as the io
			// thread and the db thread did before, so this is no worse off than it was.
			databasePool->Primary()
			);
		

		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Periodic concurrent-player-count sampler
		/////////////////////////////////////////////////////////////////////////////////////////////////

		// Once per minute, snapshot the player count reported by each authenticated realm into the
		// database. The login server keeps only the latest count per realm in memory (updated via
		// ping); this timer turns those live values into the historical series the dashboard charts.
		Countdown playerCountSampleCountdown(timerQueue);
		scoped_connection playerCountSampleConnection;
		playerCountSampleConnection = playerCountSampleCountdown.ended.connect([&realmManager, &asyncDatabase, &playerCountSampleCountdown]()
		{
			std::vector<std::pair<uint32, uint32>> samples;
			realmManager.ForEachRealm([&samples](const Realm& realm)
			{
				if (realm.IsAuthentificated())
				{
					samples.emplace_back(realm.GetRealmId(), realm.GetPlayerCount());
				}
			});

			for (const auto& [realmId, count] : samples)
			{
				asyncDatabase.asyncRequest([](bool) {}, &IDatabase::AddPlayerCountSample, realmId, count);
			}

			// Reschedule for the next minute.
			playerCountSampleCountdown.SetEnd(GetAsyncTimeMs() + constants::OneMinute);
		});
		playerCountSampleCountdown.SetEnd(GetAsyncTimeMs() + constants::OneMinute);

		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Graceful shutdown
		/////////////////////////////////////////////////////////////////////////////////////////////////

		// Declared before the handler that captures it, so the handler can cancel its own wait.
		std::unique_ptr<asio::signal_set> shutdownSignals;
		shutdownSignals = InstallShutdownHandler(ioService,
			[&realmServer, &playerServer, &webService, &playerManager, &realmManager,
			 &shutdownSignals, &timerQueue, &databasePool]()
		{
			ILOG("Shutdown signal received - stopping cleanly");

			// Stop taking new work first, so nothing arrives while the rest winds down. The web
			// service counts: its acceptor holds a pending async_accept that would otherwise keep
			// the io_service busy forever.
			playerServer->Stop();
			realmServer->Stop();
			if (webService)
			{
				webService->Stop();
			}

			// Cancelling the countdown is not enough -- that only invalidates its callback and
			// leaves the queue's timer armed, which is itself outstanding work. Stopping the whole
			// queue is what actually lets the service drain.
			timerQueue.Stop();

			// Then close what is already connected. These post to each connection's own strand,
			// so they run as the io_service keeps dispatching -- which is exactly why the service
			// must be allowed to drain rather than be stopped.
			playerManager.DisconnectAll();
			realmManager.DisconnectAll();

			// The pending async_wait on the signal set is itself outstanding io_service work.
			// Left armed, the service would never run dry and ioService.run() would never return.
			if (shutdownSignals)
			{
				asio::error_code error;
				shutdownSignals->cancel(error);
			}

			// Stopped last, so nothing can still be queueing database work. The pool drains
			// rather than discards: what is queued at this point is character and account
			// writes, and dropping those loses player data.
			if (databasePool)
			{
				databasePool->Stop();
			}
		});

		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Launch worker threads
		/////////////////////////////////////////////////////////////////////////////////////////////////

		// Create worker threads to process networking asynchronously (may be 0 as well)
		const auto maxNetworkThreads = 1u;
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

		ILOG("Login server stopped cleanly");

		return 0;
	}
}
