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
#include "assets/asset_registry.h"
#include "base/filesystem.h"

#include "event_loop.h"
#include "console.h"
#include "login_connector.h"
#include "realm_connector.h"
#include "game_state_mgr.h"
#include "login_state.h"
#include "screen.h"
#include "game_script.h"

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
	static std::shared_ptr<RealmConnector> s_realmConnector;


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
		s_realmConnector = std::make_shared<RealmConnector>(s_networkIO);

		// Start a network thread
		s_networkThread = std::thread(NetWorkProc);
	}

	/// Destroy the login connector, cuts all opened connections and waits
	/// for all network threads to stop running. Thus, this method should
	/// be called from the main thread.
	void NetDestroy()
	{
		// Close the realm connector
		if (s_realmConnector)
		{
			s_realmConnector->resetListener();
			s_realmConnector->close();
		}

		// Close the login connector
		if (s_loginConnector)
		{
			s_loginConnector->resetListener();
			s_loginConnector->close();
		}

		// Destroy the work object that keeps the worker busy so that
		// it can actually exit
		s_networkWork.reset();

		// Wait for the network thread to stop running
		s_networkThread.join();

		s_realmConnector.reset();
		s_loginConnector.reset();
	}
}


namespace mmo
{
	/// This command will try to connect to the login server and make a login attempt using the
	/// first parameter as username and the other parameters as password.
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
	static scoped_connection_container s_frameUiConnections;
	/// Game script instance.
	static std::unique_ptr<GameScript> s_gameScript;

	/// Initializes the global game systems.
	bool InitializeGlobal()
	{
		// Receive the current working directory
		std::error_code error;
		auto current_path = std::filesystem::current_path(error);
		if (error)
		{
			ELOG("Could not obtain working directory: " << error);
			return false;
		}

		// Ensure the logs directory exists
		std::filesystem::create_directories(current_path / "Logs");

		// Setup the log file connection after opening the log file
		s_logFile.open((current_path / "Logs" / "Client.log").string().c_str(), std::ios::out);
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

		// Initialize the console client which also loads the config file
		Console::Initialize(current_path / "Config" / "Config.cfg");

		// Initialize network threads
		NetInit();

		// Verify the connector instances have been initialized
		ASSERT(s_loginConnector && s_realmConnector);

		// Initialize the game script instance
		s_gameScript = std::make_unique<GameScript>(*s_loginConnector, *s_realmConnector);

		// Initialize the frame manager
		FrameManager::Initialize(&s_gameScript->GetLuaState());

		// Connect idle event
		s_frameUiConnections += EventLoop::Idle.connect([](float deltaSeconds, GameTime timestamp) 
		{ 
			FrameManager::Get().Update(deltaSeconds); 
		});

		// Watch for mouse events
		s_frameUiConnections += EventLoop::MouseMove.connect([](int32 x, int32 y) {
			FrameManager::Get().NotifyMouseMoved(Point(x, y)); 
			return false; 
		});
		s_frameUiConnections += EventLoop::MouseDown.connect([](EMouseButton button, int32 x, int32 y) {
			FrameManager::Get().NotifyMouseDown(static_cast<MouseButton>(1 << static_cast<int32>(button)), Point(x, y));
			return false;
		});
		s_frameUiConnections += EventLoop::MouseUp.connect([](EMouseButton button, int32 x, int32 y) {
			FrameManager::Get().NotifyMouseUp(static_cast<MouseButton>(1 << static_cast<int32>(button)), Point(x, y));
			return false;
		});

		s_frameUiConnections += EventLoop::KeyDown.connect([](int32 key) {
			FrameManager::Get().NotifyKeyDown(key);
			return false;
		});
		s_frameUiConnections += EventLoop::KeyChar.connect([](uint16 codepoint) {
			FrameManager::Get().NotifyKeyChar(codepoint);
			return false;
		});
		s_frameUiConnections += EventLoop::KeyUp.connect([](int32 key) {
			FrameManager::Get().NotifyKeyUp(key);
			return false;
		});

		// Register game states
		GameStateMgr::Get().AddGameState(std::make_shared<LoginState>(*s_loginConnector, *s_realmConnector));
		GameStateMgr::Get().SetGameState(LoginState::Name);

		// Lets setup a test command
		Console::RegisterCommand("login", ConsoleCommand_Login, ConsoleCommandCategory::Debug, "Attempts to login with the given account name and password.");

		// Run the RunOnce script
		Console::ExecuteCommand("run Config/RunOnce.cfg");

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

		// Disconnect FrameUI connections
		s_frameUiConnections.disconnect();

		// Reset game script instance
		s_gameScript.release();

		// Destroy the frame manager
		FrameManager::Destroy();

		// Destroy the network thread
		NetDestroy();

		// Destroy the graphics device object
		GraphicsDevice::Destroy();
		Console::Destroy();
		EventLoop::Destroy();
		AssetRegistry::Destroy();

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

	// If no debugger is attached, encapsule the common main function in a try/catch 
	// block to catch exceptions and display an error message to the user.
	if (!::IsDebuggerPresent())
	{
		try
		{
			// Run the common main function
			return mmo::CommonMain(argc, argv);
		}
		catch (const std::exception& e)
		{
			// Display the error message
			MessageBoxA(nullptr, e.what(), "Error", MB_OK | MB_ICONERROR | MB_TASKMODAL);
			return 1;
		}
	}
	else
	{
		// Debugger is present, so just run common main function uncatched, as the debugger
		// should handle uncaught exceptions
		return mmo::CommonMain(argc, argv);
	}
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
