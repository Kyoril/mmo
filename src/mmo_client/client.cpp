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
#include "event_loop.h"
#include "console.h"

#include <thread>
#include <memory>
#include <mutex>


namespace mmo
{
	// The io service used for networking
	static asio::io_service s_networkIO;
	static std::unique_ptr<asio::io_service::work> s_networkWork;
	static std::thread s_networkThread;
	static std::shared_ptr<LoginConnector> s_loginConnector;

	// Runs the network thread to handle incoming packets.
	void NetworkWorkProc()
	{
		// Run the network thread
		s_networkIO.run();
	}

	// Client idle connection
	static scoped_connection s_clientIdleCon;

	// A simple idle event that is running for the game client.
	static void OnClientIdle(float deltaSeconds, GameTime timestamp)
	{

	}

	/// Initializes the global game systems.
	bool InitializeGlobal()
	{
		// Initialize the event loop
		EventLoop::Initialize();

		// Register idle event
		s_clientIdleCon = EventLoop::Idle.connect(OnClientIdle);

		// Initialize the console client
		Console::Initialize("Config\\Config.cfg");

		// Initialize the graphics api
		auto& device = GraphicsDevice::CreateD3D11();
		device.SetWindowTitle(TEXT("MMORPG"));

		// Initialize the network thread
		s_networkWork = std::make_unique<asio::io_service::work>(s_networkIO);
		s_loginConnector = std::make_shared<LoginConnector>(s_networkIO);
		s_networkThread = std::thread(NetworkWorkProc);
		
		// TODO: Initialize other systems

		return true;
	}

	/// Destroys the global game systems.
	void DestroyGlobal()
	{
		// TODO: Destroy systems

		// Shutdown network thread
		s_loginConnector->close();
		s_loginConnector.reset();
		s_networkWork.reset();
		s_networkThread.join();

		// Destroy the graphics device object
		GraphicsDevice::Destroy();

		// Destroy the console client
		Console::Destroy();

		// Cut idle event connection
		s_clientIdleCon.disconnect();

		// Destroy the event loop
		EventLoop::Destroy();
	}

	/// Shared entry point of the application on all platforms.
	int32 CommonMain()
	{
		// Initialize the game systems, and on success, run the main event loop
		if (InitializeGlobal())
		{
			// Run the event loop
			EventLoop::Run();

			// After finishing the main even loop, destroy everything that has
			// being initialized so far
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
