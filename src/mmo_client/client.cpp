// Copyright (C) 2019, Robin Klimonow. All rights reserved.

// This file contains the entry point of the game and takes care of initializing the
// game as well as starting the main loop for the application. This is used on all
// client-supported platforms.

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#else
#	include <iostream>
#endif

#include "login_connector.h"

#include "base/typedefs.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"

#include "graphics/graphics_device.h"

#include <mutex>


namespace mmo
{
	/// Initializes the global game systems.
	bool InitializeGlobal()
	{
		// Initialize the graphics api
		auto& device = GraphicsDevice::CreateD3D11();

		// TODO: Initialize other systems

		return true;
	}

	/// Destroys the global game systems.
	void DestroyGlobal()
	{
		// Destroy the graphics device object
		GraphicsDevice::Destroy();

		// TODO: Destroy systems
	}

	/// Shared entry point of the application on all platforms.
	int32 CommonMain()
	{
		// Initialize the game systems, and on success, run the main event loop
		if (InitializeGlobal())
		{
			// TODO: Run event loop

			// The io service used for networking
			asio::io_service networkIO;

			// Create a new connector and run it
			auto connector = std::make_shared<LoginConnector>(networkIO);
			connector->Connect("Kyoril", "12345");

			// Run the network IO
			networkIO.run();

			// After finishing the main even loop, destroy everything that has
			// beein initialized so far
			DestroyGlobal();
		}

		return 0;
	}
}


#ifdef _WIN32

/// Procedural entry point on windows platforms.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	// Setup log to print each log entry to the debug output on windows
	std::mutex logMutex;
	mmo::g_DefaultLog.signal().connect([&logMutex](const mmo::LogEntry & entry) {
		std::scoped_lock lock{ logMutex };
		OutputDebugStringA((entry.message + "\n").c_str());
	});
	
	// Finally, run the common main function on all platforms
	return mmo::CommonMain();
}

#else

/// Procedural entry point on non-windows platforms.
int main(int argc, const char* argv[])
{
	// Write everything log entry to cout on non-windows platforms by default
	std::mutex logMutex;
	mmo::g_DefaultLog.signal().connect([&logMutex](const mmo::LogEntry & entry) {
		std::scoped_lock lock{ logMutex };
		mmo::printLogEntry(std::cout, entry, mmo::g_DefaultConsoleLogOptions);
	});

	// Finally, run the common main function on all platforms
	return mmo::CommonMain();
}

#endif
