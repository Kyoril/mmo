// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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

#include "client_cache.h"
#include "cursor.h"
#include "loot_client.h"
#include "stream_sink.h"
#include "vendor_client.h"
#include "game_states/world_state.h"
#include "base/timer_queue.h"

#include "base/executable_path.h"
#include "client_data/project.h"

#include "action_bar.h"
#include "spell_cast.h"
#include "trainer_client.h"
#include "quest_client.h"


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

	extern Cursor g_cursor;

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

		// Setup cursor graphics
		g_cursor.LoadCursorTypeFromTexture(CursorType::Pointer, "Interface/Cursor/pointer001.htex");
		g_cursor.LoadCursorTypeFromTexture(CursorType::Interact, "Interface/Cursor/gears001.htex");
		g_cursor.LoadCursorTypeFromTexture(CursorType::Attack, "Interface/Cursor/sword001.htex");
		g_cursor.LoadCursorTypeFromTexture(CursorType::Loot, "Interface/Cursor/bag001.htex");
		g_cursor.LoadCursorTypeFromTexture(CursorType::Gossip, "Interface/Cursor/talk001.htex");
		g_cursor.SetCursorType(CursorType::Pointer);

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

		s_frameUiConnections += EventLoop::KeyDown.connect([](int32 key, bool) {
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

		// Expose model frame methods to lua
		luabind::module(&s_gameScript->GetLuaState())
		[
			luabind::scope(
				luabind::class_<ModelFrame, Frame>("ModelFrame")
				.def("SetModelFile", &ModelFrame::SetModelFile)
				.def("Yaw", &ModelFrame::Yaw)
				.def("SetZoom", &ModelFrame::SetZoom)
				.def("GetZoom", &ModelFrame::GetZoom)
				.def("GetYaw", &ModelFrame::GetYaw)
				.def("ResetYaw", &ModelFrame::ResetYaw))
		];

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

	static proto_client::Project s_project;

	std::unique_ptr<LootClient> s_lootClient;
	std::unique_ptr<VendorClient> s_vendorClient;
	std::unique_ptr<TrainerClient> s_trainerClient;

	std::unique_ptr<DBItemCache> s_itemCache;
	std::unique_ptr<DBCreatureCache> s_creatureCache;
	std::unique_ptr<DBQuestCache> s_questCache;

	static const char* const s_itemCacheFilename = "Cache/Items.db";
	static const char* const s_creatureCacheFilename = "Cache/Creatures.db";
	static const char* const s_questCacheFilename = "Cache/Quests.db";

	static std::unique_ptr<ActionBar> s_actionBar;
	static std::unique_ptr<SpellCast> s_spellCast;
	static std::unique_ptr<QuestClient> s_questClient;

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
		std::filesystem::create_directories(currentPath / "Config");

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

		// Initialize the console client which also loads the config file
		Console::Initialize("Config/Config.cfg");

		// Initialize network threads
		NetInit();

		// Run service
		s_timerConnection = EventLoop::Idle.connect([&](float, const mmo::GameTime&)
			{
				NetWorkProc();
				s_timerService.poll_one();
			});

		// Verify the connector instances have been initialized
		ASSERT(s_loginConnector && s_realmConnector);

		// Load game data
		if (!s_project.load("ClientDB"))
		{
			ELOG("Failed to load project files!");
			return false;
		}

		// Initialize item cache (TODO: Try loading cache from file so we maybe won't have to ask the server next time we run the client)
		s_itemCache = std::make_unique<DBItemCache>(*s_realmConnector);
		if (const auto itemCacheFile = AssetRegistry::OpenFile(s_itemCacheFilename))
		{
			io::StreamSource source(*itemCacheFile);
			io::Reader reader(source);
			s_itemCache->Deserialize(reader);
		}

		s_creatureCache = std::make_unique<DBCreatureCache>(*s_realmConnector);
		if (const auto creatureCacheFile = AssetRegistry::OpenFile(s_creatureCacheFilename))
		{
			io::StreamSource source(*creatureCacheFile);
			io::Reader reader(source);
			s_creatureCache->Deserialize(reader);
		}

		s_questCache = std::make_unique<DBQuestCache>(*s_realmConnector);
		if (const auto questCacheFile = AssetRegistry::OpenFile(s_questCacheFilename))
		{
			io::StreamSource source(*questCacheFile);
			io::Reader reader(source);
			s_questCache->Deserialize(reader);
		}

		// Initialize loot client
		s_lootClient = std::make_unique<LootClient>(*s_realmConnector, *s_itemCache);
		s_vendorClient = std::make_unique<VendorClient>(*s_realmConnector, *s_itemCache);
		s_trainerClient = std::make_unique<TrainerClient>(*s_realmConnector, s_project.spells);
		s_questClient = std::make_unique<QuestClient>(*s_realmConnector, *s_questCache, s_project.spells, *s_itemCache, *s_creatureCache);

		s_spellCast = std::make_unique<SpellCast>(*s_realmConnector, s_project.spells);
		s_actionBar = std::make_unique<ActionBar>(*s_realmConnector, s_project.spells, *s_itemCache, *s_spellCast);

		GameStateMgr& gameStateMgr = GameStateMgr::Get();

		// Register game states
		const auto loginState = std::make_shared<LoginState>(gameStateMgr, *s_loginConnector, *s_realmConnector, *s_timerQueue);
		gameStateMgr.AddGameState(loginState);

		const auto worldState = std::make_shared<WorldState>(gameStateMgr, *s_realmConnector, s_project, *s_timerQueue, *s_lootClient, *s_vendorClient, *s_itemCache, *s_creatureCache, *s_questCache, *s_actionBar, *s_spellCast, *s_trainerClient, *s_questClient);
		gameStateMgr.AddGameState(worldState);
		
		// Initialize the game script instance
		s_gameScript = std::make_unique<GameScript>(*s_loginConnector, *s_realmConnector, *s_lootClient, *s_vendorClient, loginState, s_project, *s_actionBar, *s_spellCast, *s_trainerClient, *s_questClient);
		
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
			FrameManager::Get().NotifyScreenSizeChanged(window->GetWidth(), window->GetHeight());	
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

		s_vendorClient.release();
		s_lootClient.release();

		DestroyFrameUI();

		// Reset game script instance
		s_gameScript.reset();

		// Destroy the network thread
		NetDestroy();

		// Serialize caches
		if (const auto itemCacheFile = AssetRegistry::CreateNewFile(s_itemCacheFilename))
		{
			io::StreamSink sink(*itemCacheFile);
			io::Writer writer(sink);
			s_itemCache->Serialize(writer);
		}
		if (const auto creatureCacheFile = AssetRegistry::CreateNewFile(s_creatureCacheFilename))
		{
			io::StreamSink sink(*creatureCacheFile);
			io::Writer writer(sink);
			s_creatureCache->Serialize(writer);
		}
		if (const auto questCacheFIle = AssetRegistry::CreateNewFile(s_questCacheFilename))
		{
			io::StreamSink sink(*questCacheFIle);
			io::Writer writer(sink);
			s_questCache->Serialize(writer);
		}

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
