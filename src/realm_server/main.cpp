// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "base/typedefs.h"
#include "base/service.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"
#include "cxxopts/cxxopts.hpp"

#include "program.h"
#include "player.h"

#include <iostream>
#include <mutex>
#include <cstring>


/// Procedural entry point of the application.
int main(int argc, char* argv[])
{
	// Whether the server application should be started as service
	bool runAsService = false;

	// The config file name for the server to use
	std::string configFileName = "config/realm_server.cfg";

	// Prepare command line options
	cxxopts::Options options("MMO Realm Server", "Realm server for the mmo project.");
	options.allow_unrecognised_options().add_options()
#ifdef __linux__
		("s,service", "Run this application as a service")
#endif
		("c,config", "Config file name", cxxopts::value<std::string>()->default_value(configFileName))
		;

	// Parse command line options
	auto results = options.parse(argc, argv);

	// Try to find the name of the config file name in the command line string
	try
	{
		configFileName = results["config"].as<std::string>();
	}
	catch(const std::exception& e)
	{
		WLOG(e.what());
	}

	// On linux, we can run the process daemonized. Since we intend windows machines to run as standalone
	// applications instead, we don't support services on windows os.
#ifdef __linux__
	if (results.count("s") > 0)
	{
		runAsService = true;

		if (mmo::createService() == mmo::CreateServiceResult::IsObsoleteProcess)
		{
			std::cout << "Realm service is now running." << '\n';
			return 0;
		}
	}
#endif

	// Open stdout log output
	auto logOptions = mmo::g_DefaultConsoleLogOptions;
	logOptions.alwaysFlush = false;

	// Add cout to the list of log output streams
	std::mutex coutLogMutex;
	mmo::g_DefaultLog.signal().connect([&coutLogMutex, &logOptions](const mmo::LogEntry & entry) {
		std::scoped_lock lock{ coutLogMutex };
		mmo::printLogEntry(std::cout, entry, logOptions);
	});

	// Notify about the start of the login server application
	if (runAsService)
	{
		ILOG("Starting realm server as service...");
	}
	else
	{
		ILOG("Starting the realm server application...");
	}

	ILOG("Using config file " << configFileName << "...");

	// Notify in case of debug builds
#ifdef _DEBUG
	DLOG("Debug build enabled");
#endif

	// Run the application as long as the result code is 0 and the program is flagged to be restarted
	int32 result = 0;
	do
	{
		// Turn off restart so that the program could terminate eventually after this run
		mmo::Program::ShouldRestart = false;

		// Create the global application instance and run it
		mmo::Program program;
		result = program.run(configFileName);
	} while (result == 0 && mmo::Program::ShouldRestart);

	// Check for errors
	if (mmo::Program::ShouldRestart)
	{
		ELOG("Server application was flagged for restart but did not terminate successfully: error code " << result);
	}

	// Return the result code
	return result;
}
