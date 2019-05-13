// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "program.h"
#include "version.h"

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

#include "base/filesystem.h"

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

		// TODO



		/////////////////////////////////////////////////////////////////////////////////////////////////
		// File log setup
		/////////////////////////////////////////////////////////////////////////////////////////////////

		// TODO

		// Display version infos
		ILOG("Version " << Major << "." << Minor << "." << Build << "." << Revisision << " (Commit: " << GitCommit << ")");
		ILOG("Last Change: " << GitLastChange);



		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Database setup
		/////////////////////////////////////////////////////////////////////////////////////////////////

		// TODO


		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Create the realm connector service
		/////////////////////////////////////////////////////////////////////////////////////////////////

		// TODO


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
