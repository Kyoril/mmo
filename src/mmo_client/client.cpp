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

#include "base/typedefs.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"
#include "graphics/graphics_device.h"
#include "base/filesystem.h"

#include "event_loop.h"
#include "console.h"
#include "login_connector.h"
#include "game_state_mgr.h"
#include "login_state.h"
#include "screen.h"

#include <fstream>
#include <thread>
#include <memory>
#include <mutex>


////////////////////////////////////////////////////////////////
// Network handler

namespace mmo
{
	// The io service used for networking
	static asio::io_service s_networkIO;
	static std::unique_ptr<asio::io_service::work> s_networkWork;
	static std::thread s_networkThread;
	static std::shared_ptr<LoginConnector> s_loginConnector;


	// Runs the network thread to handle incoming packets.
	void NetWorkProc()
	{
		// Run the network thread
		s_networkIO.run();
	}

	/// Initializes the login connector and starts one or multiple network
	/// threads to handle network events. Should be called from the main 
	/// thread.
	void NetInit()
	{
		// Keep the worker busy until this object is destroyed
		s_networkWork = std::make_unique<asio::io_service::work>(s_networkIO);

		// Create the login connector instance
		s_loginConnector = std::make_shared<LoginConnector>(s_networkIO);

		// Start a network thread
		s_networkThread = std::thread(NetWorkProc);
	}

	/// Destroy the login connector, cuts all opened connections and waits
	/// for all network threads to stop running. Thus, this method should
	/// be called from the main thread.
	void NetDestroy()
	{
		// Close the login connector
		s_loginConnector->resetListener();
		s_loginConnector->close();
		s_loginConnector.reset();

		// Destroy the work object that keeps the worker busy so that
		// it can actually exit
		s_networkWork.reset();

		// Wait for the network thread to stop running
		s_networkThread.join();
	}
}


namespace mmo
{
	/// This command will try to connect to the login server and make a login attempt using the
	/// first parameter as username and the second parameter as password.
	static void ConsoleCommand_Login(const std::string& cmd, const std::string& arguments)
	{
		std::size_t spacePos = arguments.find(' ');
		if (spacePos == arguments.npos)
		{
			ELOG("Invalid argument count!");
			return;
		}

		// Try to connect
		s_loginConnector->Connect(arguments.substr(0, spacePos), arguments.substr(spacePos + 1));
	}
}



////////////////////////////////////////////////////////////////
// Initialization and destruction

namespace mmo
{
	static std::ofstream s_logFile;
	static scoped_connection s_logConn;

	void PaintFuncTest()
	{
	}

	/// Initializes the global game systems.
	bool InitializeGlobal()
	{
		// Ensure the logs directory exists
		std::filesystem::create_directories("./Logs");

		// Setup the log file connection after opening the log file
		s_logFile.open("./Logs/Client.log", std::ios::out);
		if (s_logFile)
		{
			s_logConn = g_DefaultLog.signal().connect(
				[](const LogEntry & entry)
			{
				printLogEntry(s_logFile, entry, g_DefaultFileLogOptions);
			});
		}

		// Initialize the event loop
		EventLoop::Initialize();

		// Initialize the console client
		Console::Initialize("Config\\Config.cfg");

		// Initialize network threads
		NetInit();

		// Register game states
		GameStateMgr::Get().AddGameState(std::make_shared<LoginState>());
		GameStateMgr::Get().SetGameState(LoginState::Name);

		// Lets setup a test command
		Console::RegisterCommand("login", ConsoleCommand_Login, ConsoleCommandCategory::Debug, "Attempts to login with the given account name and password.");

		// Run the RunOnce script
		Console::ExecuteCommand("run Config\\RunOnce.cfg");

		// TODO: Initialize other systems

		return true;
	}

	/// Destroys the global game systems.
	void DestroyGlobal()
	{
		// TODO: Destroy systems

		// Remove login command
		Console::UnregisterCommand("login");

		// Remove all registered game states and also leave the current game state.
		GameStateMgr::Get().RemoveAllGameStates();

		// Destroy the network thread
		NetDestroy();

		// Destroy the graphics device object
		GraphicsDevice::Destroy();

		// Destroy the console client
		Console::Destroy();

		// Destroy the event loop
		EventLoop::Destroy();

		// Destroy log
		s_logConn.disconnect();
		s_logFile.close();
	}
}


////////////////////////////////////////////////////////////////
// Entry point

namespace mmo
{
	/// Shared entry point of the application on all platforms.
	int32 CommonMain(int argc, char** argv)
	{
		// TODO: Do something with command line arguments

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

#include "base/win_utility.h"

/// Procedural entry point on windows platforms.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	// Setup log to print each log entry to the debug output on windows
#ifdef _DEBUG
	std::mutex logMutex;
	mmo::g_DefaultLog.signal().connect([&logMutex](const mmo::LogEntry & entry) {
		std::scoped_lock lock{ logMutex };
		OutputDebugStringA((entry.message + "\n").c_str());
	});
#endif

	// Split command line arguments
	int argc = 0;
	PCHAR* argv = CommandLineToArgvA(GetCommandLine(), &argc);

	// Finally, run the common main function on all platforms
	return mmo::CommonMain(argc, argv);
}

#else

/// Procedural entry point on non-windows platforms.
int main(int argc, char** argv)
{
	// Write everything log entry to cout on non-windows platforms by default
	std::mutex logMutex;
	mmo::g_DefaultLog.signal().connect([&logMutex](const mmo::LogEntry & entry) {
		std::scoped_lock lock{ logMutex };
		mmo::printLogEntry(std::cout, entry, mmo::g_DefaultConsoleLogOptions);
	});

	// Finally, run the common main function on all platforms
	return mmo::CommonMain(argc, argv);
}

#endif
