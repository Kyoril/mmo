// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "base/typedefs.h"
#include "base/service.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"

#include "program.h"

#include <iostream>
#include <mutex>
#include <cstring>

/// Procedural entry point of the application.
int main(int argc, const char* argv[])
{
	// Whether the server application should be started as service
	bool runAsService = false;

	// On linux, we can run the process daemonized. Since we intend windows machines to run as standalone
	// applications instead, we don't support services on windows os.
#ifdef __linux__
	// Parse command line arguments for service parameter
	for (int i = 0; i < argc; ++i)
	{
		if (::strcmp(argv[i], "-s") == 0 ||
			::strcmp(argv[i], "--service") == 0)
		{
			runAsService = true;
			break;
		}
	}

	if (runAsService)
	{
		if (mmo::createService() == mmo::CreateServiceResult::IsObsoleteProcess)
		{
			std::cout << "Login service is now running." << '\n';
			return 0;
		}
	}
#endif

	// Open stdout log output
	auto options = mmo::g_DefaultConsoleLogOptions;
	options.alwaysFlush = false;

	// Add cout to the list of log output streams
	std::mutex coutLogMutex;
	mmo::g_DefaultLog.signal().connect([&coutLogMutex, &options](const mmo::LogEntry & entry) {
		std::scoped_lock lock{ coutLogMutex };
		mmo::printLogEntry(std::cout, entry, options);
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

	// Run the application as long as the result code is 0 and the program is flagged to be restarted
	int32 result = 0;
	do
	{
		// Turn off restart so that the program could terminate eventually after this run
		mmo::Program::ShouldRestart = false;

		// Create the global application instance and run it
		mmo::Program program;
		result = program.run();
	} while (result == 0 && mmo::Program::ShouldRestart);

	// Check for errors
	if (mmo::Program::ShouldRestart)
	{
		ELOG("Server application was flagged for restart but did not terminate successfully: error code " << result);
	}

	// Return the result code
	return result;
}
