#include "program.h"
#include "configuration.h"
#include "version.h"
#include "mysql_database.h"
#include "player_manager.h"
#include "player.h"

#include "asio.hpp"

#include "log/log_std_stream.h"
#include "log/log_entry.h"
#include "log/default_log_levels.h"
#include "auth_protocol/auth_protocol.h"
#include "auth_protocol/auth_server.h"
#include "base/constants.h"

#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <vector>
#include <mutex>
#include <thread>

#if defined(__GNUC__)
#	if __GNUC__ <= 7
#		include <experimental/filesystem>
namespace std
{
	namespace filesystem = std::experimental::filesystem::v1;
}
#	endif
#else
#	include <filesystem>
#endif

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

	int32 Program::run()
	{
		// This is the main ioService object
		asio::io_service ioService;

		// The database service object and keep-alive object
		asio::io_service dbService;

		// Keep the database service alive / busy until this object is alive
		auto dbWork = std::make_shared<asio::io_context::work>(dbService);



		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Load config file
		/////////////////////////////////////////////////////////////////////////////////////////////////

		Configuration config;
		if (!config.load("config/login_server.cfg"))
		{
			return 1;
		}



		/////////////////////////////////////////////////////////////////////////////////////////////////
		// File log setup
		/////////////////////////////////////////////////////////////////////////////////////////////////

		if (config.isLogActive)
		{
			auto logOptions = g_DefaultFileLogOptions;
			logOptions.alwaysFlush = !config.isLogFileBuffering;

			// Setup the log file connection after opening the log file
			scoped_connection genericLogConnection;
			m_logFile.open(generateLogFileName(config.logFileName).c_str(), std::ios::app);
			if (m_logFile)
			{
				genericLogConnection = g_DefaultLog.signal().connect(
					[this, &logOptions](const LogEntry & entry)
				{
					printLogEntry(m_logFile, entry, logOptions);
				});
			}
		}

		// Display version infos
		ILOG("Version " << Major << "." << Minor << "." << Build << "." << Revisision << " (Commit: " << GitCommit << ")");
		ILOG("Last Change: " << GitLastChange);



		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Database setup
		/////////////////////////////////////////////////////////////////////////////////////////////////

		auto database = std::make_unique<MySQLDatabase>(mmo::mysql::DatabaseInfo{
			config.mysqlHost, 
			config.mysqlPort,
			config.mysqlUser, 
			config.mysqlPassword, 
			config.mysqlDatabase 
		});
		if (!database->load())
		{
			ELOG("Could not load the database");
			return 1;
		}

		const auto async = [&dbService](Action action) { dbService.post(std::move(action)); };
		const auto sync = [&ioService](Action action) { ioService.post(std::move(action)); };
		AsyncDatabase asyncDatabase{ *database, async, sync };



		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Create the realm service
		/////////////////////////////////////////////////////////////////////////////////////////////////

		/*RealmManager realmManager{ config.maxRealms };

		// Create the realm server
		std::unique_ptr<binary::Server> realmServer;
		try
		{
			realmServer.reset(new binary::Server(std::ref(ioService), constants::DefaultLoginRealmPort, std::bind(&binary::Connection::create, std::ref(ioService), nullptr)));
		}
		catch (const BindFailedException &)
		{
			ELOG("Could not bind on tcp port " << constants::DefaultLoginRealmPort << "! Maybe there is another server instance running on this port?");
			return 1;
		}

		// Careful: Called by multiple threads!
		const auto createRealm = [&realmManager, &asyncDatabase](std::shared_ptr<Realm::Client> connection)
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

			auto realm = std::make_shared<Realm>(realmManager, asyncDatabase, connection, address.to_string());
			ILOG("Incoming realm connection from " << address);
			realmManager.addRealm(std::move(realm));

			connection->startReceiving();
		};

		// Start accepting incoming realm connections
		const scoped_connection realmConnected{ realmServer->connected().connect(createRealm) };
		realmServer->startAccept();*/



		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Create the player service
		/////////////////////////////////////////////////////////////////////////////////////////////////

		PlayerManager playerManager{ config.maxPlayers };

		// Create the player server
		std::unique_ptr<auth::Server> playerServer;
		try
		{
			playerServer.reset(new mmo::auth::Server(std::ref(ioService), constants::DefaultLoginPlayerPort, std::bind(&mmo::auth::Connection::create, std::ref(ioService), nullptr)));
		}
		catch (const mmo::BindFailedException &)
		{
			ELOG("Could not bind on tcp port " << constants::DefaultLoginPlayerPort << "! Maybe there is another server instance running on this port?");
			return 1;
		}
		
		// Careful: Called by multiple threads!
		const auto createPlayer = [&playerManager, &asyncDatabase](std::shared_ptr<Player::Client> connection)
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

			auto player = std::make_shared<Player>(std::ref(playerManager), std::ref(asyncDatabase), connection, address.to_string());
			ILOG("Incoming player connection from " << address);
			playerManager.addPlayer(std::move(player));

			// Now we can start receiving data
			connection->startReceiving();
		};

		// Start accepting incoming player connections
		const scoped_connection playerConnected{ playerServer->connected().connect(createPlayer) };
		playerServer->startAccept();



		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Create the web service
		/////////////////////////////////////////////////////////////////////////////////////////////////

		// TODO



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
