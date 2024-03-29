// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

// This file contains the entry point of the game and takes care of initializing the
// game as well as starting the main loop for the application. This is used on all
// client-supported platforms.

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#endif

#include "base/typedefs.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"
#include "assets/asset_registry.h"
#include "base/filesystem.h"

#include "event_loop.h"
#include "console/console.h"
#include "net/login_connector.h"
#include "net/realm_connector.h"
#include "game_states/game_state_mgr.h"
#include "game_states/login_state.h"
#include "game_script.h"
#include "ui/model_frame.h"
#include "ui/model_renderer.h"

#include <iostream>
#include <fstream>
#include <thread>
#include <memory>

#include "game_states/world_state.h"
#include "base/timer_queue.h"

#include "base/executable_path.h"


////////////////////////////////////////////////////////////////
// Network handler

namespace mmo
{
	static asio::io_service s_timerService;
	
	void DispatchOnGameThread(std::function<void()>&& f)
	{
		s_timerService.post(std::move(f));
	}
}

namespace mmo
{
	// The io service used for networking
	static std::unique_ptr<asio::io_service> s_networkIO;
	static std::unique_ptr<asio::io_service::work> s_networkWork;
	static std::shared_ptr<LoginConnector> s_loginConnector;
	static std::shared_ptr<RealmConnector> s_realmConnector;


	/// Runs the network thread to handle incoming packets.
	void NetWorkProc()
	{
		// Run the network thread
		s_networkIO->poll_one();
	}

	/// Initializes the login connector and starts one or multiple network
	/// threads to handle network events. Should be called from the main 
	/// thread.
	void NetInit()
	{
		s_networkIO = std::make_unique<asio::io_service>();

		// Keep the worker busy until this object is destroyed
		s_networkWork = std::make_unique<asio::io_service::work>(*s_networkIO);

		// Create the login connector instance
		s_loginConnector = std::make_shared<LoginConnector>(*s_networkIO);
		s_realmConnector = std::make_shared<RealmConnector>(*s_networkIO);
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
		s_networkIO->stop();

		// Wait for the network thread to stop running
		s_networkIO->reset();

		s_realmConnector.reset();
		s_loginConnector.reset();
	}
}

////////////////////////////////////////////////////////////////
// FrameUI stuff

namespace mmo
{
	static scoped_connection_container s_frameUiConnections;
	static std::unique_ptr<GameScript> s_gameScript;


	/// Initializes everything related to FrameUI.
	bool InitializeFrameUi()
	{
		auto window = GraphicsDevice::Get().GetAutoCreatedWindow();
		if (window)
		{
			s_frameUiConnections += window->Resized.connect([](uint16 width, uint16 height)
			{
				FrameManager::Get().NotifyScreenSizeChanged(width, height);
			});
		}

		// Initialize the frame manager
		FrameManager::Initialize(&s_gameScript->GetLuaState());

		// Register model renderer
		FrameManager::Get().RegisterFrameRenderer("ModelRenderer", [](const std::string& name)
		{
			return std::make_unique<ModelRenderer>(name);
		});

		// Register model frame type
		FrameManager::Get().RegisterFrameFactory("Model", [](const std::string& name) {
			return std::make_shared<ModelFrame>(name);
		});

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
			return true;
		});
		s_frameUiConnections += EventLoop::KeyChar.connect([](uint16 codepoint) {
			FrameManager::Get().NotifyKeyChar(codepoint);
			return false;
		});
		s_frameUiConnections += EventLoop::KeyUp.connect([](int32 key) {
			FrameManager::Get().NotifyKeyUp(key);
			return false;
		});
		
		return true;
	}

	/// Destroys everything related to FrameUI.
	void DestroyFrameUI()
	{
		// Disconnect FrameUI connections
		s_frameUiConnections.disconnect();

		// Unregister model renderer
		FrameManager::Get().RemoveFrameRenderer("ModelRenderer");
		FrameManager::Get().UnregisterFrameFactory("Model");

		// Destroy the frame manager
		FrameManager::Destroy();
	}
}


////////////////////////////////////////////////////////////////
// Initialization and destruction

namespace mmo
{
	static std::ofstream s_logFile;
	static scoped_connection s_logConn;

	static std::unique_ptr<TimerQueue> s_timerQueue;
	static scoped_connection s_timerConnection;

	/// Initializes the global game systems.
	bool InitializeGlobal()
	{
		s_timerQueue = std::make_unique<TimerQueue>(s_timerService);

		// Receive the current working directory
		std::error_code error;
		const auto currentPath = std::filesystem::current_path(error);
		if (error)
		{
			ELOG("Could not obtain working directory: " << error);
			return false;
		}

		// Ensure the logs directory exists
		std::filesystem::create_directories(currentPath / "Logs");

		// Setup the log file connection after opening the log file
		s_logFile.open((currentPath / "Logs" / "Client.log").string().c_str(), std::ios::out);
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

		// Run service
		s_timerConnection = EventLoop::Idle.connect([&](float, const mmo::GameTime&)
		{
			NetWorkProc();
			s_timerService.run_one();
		});

		// Initialize the console client which also loads the config file
		Console::Initialize(currentPath / "Config" / "Config.cfg");

		// Initialize network threads
		NetInit();

		// Verify the connector instances have been initialized
		ASSERT(s_loginConnector && s_realmConnector);

		// Load game data


		GameStateMgr& gameStateMgr = GameStateMgr::Get();

		// Register game states
		const auto loginState = std::make_shared<LoginState>(gameStateMgr, *s_loginConnector, *s_realmConnector, *s_timerQueue);
		gameStateMgr.AddGameState(loginState);

		const auto worldState = std::make_shared<WorldState>(gameStateMgr, *s_realmConnector);
		gameStateMgr.AddGameState(worldState);
		
		// Initialize the game script instance
		s_gameScript = std::make_unique<GameScript>(*s_loginConnector, *s_realmConnector, loginState);
		
		// Setup FrameUI library
		if (!InitializeFrameUi())
		{
			return false;
		}

		// Enter login state
		gameStateMgr.SetGameState(LoginState::Name);

		// Run the RunOnce script
		Console::ExecuteCommand("run Config/RunOnce.cfg");

		const auto window = GraphicsDevice::Get().GetAutoCreatedWindow();
		if (window)
		{
			FrameManager::Get().NotifyScreenSizeChanged(
			window->GetWidth(), window->GetHeight());	
		}

		// TODO: Initialize other systems

		return true;
	}

	/// Destroys the global game systems.
	void DestroyGlobal()
	{
		s_timerConnection.disconnect();
		
		// Remove all registered game states and also leave the current game state.
		GameStateMgr::Get().RemoveAllGameStates();

		DestroyFrameUI();

		// Reset game script instance
		s_gameScript.reset();

		// Destroy the network thread
		NetDestroy();

		// Destroy the graphics device object
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
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
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
	auto argc = 0;
	auto* const argv = CommandLineToArgvA(GetCommandLine(), &argc);

	// If no debugger is attached, encapsulate the common main function in a try/catch 
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

	// Debugger is present, so just run common main function uncatched, as the debugger
	// should handle uncaught exceptions
	return mmo::CommonMain(argc, argv);
}

#else

#ifdef __APPLE__
int main_osx(int argc, char* argv[]);
#endif

/// Procedural entry point on non-windows platforms.
int main(int argc, char** argv)
{
    // Set working directory
    std::filesystem::current_path(mmo::GetExecutablePath());
    
	// Write everything log entry to cout on non-windows platforms by default
	std::mutex logMutex;
	mmo::g_DefaultLog.signal().connect([&logMutex](const mmo::LogEntry & entry) {
		std::scoped_lock lock{ logMutex };
		mmo::printLogEntry(std::cout, entry, mmo::g_DefaultConsoleLogOptions);
	});

#ifdef __APPLE__
    return main_osx(argc, argv);
#else
	// Finally, run the common main function on all platforms
	return mmo::CommonMain(argc, argv);
#endif
}

#endif
