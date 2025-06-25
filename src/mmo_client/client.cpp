// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// This file contains the entry point of the game and takes care of initializing the
// game as well as starting the main loop for the application. This is used on all
// client-supported platforms.

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#   include <DbgHelp.h>
#   pragma comment(lib, "dbghelp.lib")
#endif

#include "discord.h"

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

#ifdef _WIN32
#   include "audio/fmod_audio.h"
#else
#   include "audio/null_audio.h"
#endif

#include <iostream>
#include <fstream>
#include <thread>
#include <memory>

#include "data/client_cache.h"
#include "cursor.h"
#include "systems/loot_client.h"
#include "stream_sink.h"
#include "systems/vendor_client.h"
#include "game_states/world_state.h"
#include "base/timer_queue.h"

#include "base/executable_path.h"
#include "client_data/project.h"

#include "systems/action_bar.h"
#include "systems/spell_cast.h"
#include "systems/trainer_client.h"
#include "systems/quest_client.h"
#include "systems/party_info.h"
#include "systems/guild_client.h"
#include "systems/talent_client.h"

#include "ui/minimap.h"

#include "char_creation/char_create_info.h"
#include "char_creation/char_select.h"
#include "base/create_process.h"
#include "game_client/object_mgr.h"
#include "ui/unit_model_frame.h"

#include "luabind/luabind.hpp"
#include "luabind/iterator_policy.hpp"
#include "luabind/out_value_policy.hpp"
#include "ui/minimap_frame.h"


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
	static Localization s_localization;
	static std::unique_ptr<Minimap> s_minimap;

	extern Cursor g_cursor;

	/// Initializes everything related to FrameUI.
	bool InitializeFrameUi()
	{
		if (const auto window = GraphicsDevice::Get().GetAutoCreatedWindow())
		{
			s_frameUiConnections += window->Resized.connect([](uint16 width, uint16 height)
			{
				FrameManager::Get().NotifyScreenSizeChanged(width, height);
			});
		}

		if (!s_localization.LoadFromFile())
		{
			ELOG("Failed to initialize localization!");
		}

		// Initialize the frame manager
		FrameManager::Initialize(&s_gameScript->GetLuaState(), s_localization);

		// Register model renderer
		FrameManager::Get().RegisterFrameRenderer("ModelRenderer", [](const std::string& name)
		{
			return std::make_unique<ModelRenderer>(name);
		});

		// Register model frame type
		FrameManager::Get().RegisterFrameFactory("Model", [](const std::string& name) {
			return std::make_shared<ModelFrame>(name);
		});
		FrameManager::Get().RegisterFrameFactory("UnitModel", [](const std::string& name) {
			return std::make_shared<UnitModelFrame>(name);
			});
		FrameManager::Get().RegisterFrameFactory("Minimap", [](const std::string& name) {
			return std::make_shared<MinimapFrame>(name, *s_minimap);
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
				.def("ResetYaw", &ModelFrame::ResetYaw)
				.def("InvalidateModel", &ModelFrame::InvalidateModel)
				.def("SetAutoRender", &ModelFrame::SetAutoRender)),
				
			luabind::scope(
				luabind::class_<UnitModelFrame, ModelFrame>("UnitModelFrame")
				.def("SetUnit", &UnitModelFrame::SetUnit))
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
		FrameManager::Get().UnregisterFrameFactory("UnitModel");

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

	static std::unique_ptr<IAudio> s_audio;

	std::unique_ptr<LootClient> s_lootClient;
	std::unique_ptr<VendorClient> s_vendorClient;
	std::unique_ptr<TrainerClient> s_trainerClient;

	std::unique_ptr<ClientCache> s_clientCache;

	static std::unique_ptr<ActionBar> s_actionBar;
	static std::unique_ptr<SpellCast> s_spellCast;
	static std::unique_ptr<QuestClient> s_questClient;
	static std::unique_ptr<PartyInfo> s_partyInfo;
	static std::unique_ptr<GuildClient> s_guildClient;

	static std::unique_ptr<CharCreateInfo> s_charCreateInfo;
	static std::unique_ptr<CharSelect> s_charSelect;
	static std::unique_ptr<TalentClient> s_talentClient;

	static std::unique_ptr<Discord> s_discord;

	static GameTimeComponent s_gameTime;

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

#ifdef _WIN32
		s_audio = std::make_unique<FMODAudio>();
#else
        s_audio = std::make_unique<NullAudio>();
#endif
		s_audio->Create();

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

		s_clientCache = std::make_unique<ClientCache>(*s_realmConnector);
		if (!s_clientCache->Load())
		{
			ELOG("Failed to load the client cache!");
			return false;
		}

		s_discord = std::make_unique<Discord>();
		s_discord->Initialize();

		// Setup minimap
		s_minimap = std::make_unique<Minimap>(256);

		s_charCreateInfo = std::make_unique<CharCreateInfo>(s_project, *s_realmConnector);
		s_charSelect = std::make_unique<CharSelect>(s_project, *s_realmConnector);

		// Initialize loot client
		s_lootClient = std::make_unique<LootClient>(*s_realmConnector, s_clientCache->GetItemCache());
		s_vendorClient = std::make_unique<VendorClient>(*s_realmConnector, s_clientCache->GetItemCache());
		s_trainerClient = std::make_unique<TrainerClient>(*s_realmConnector, s_project.spells);
		s_questClient = std::make_unique<QuestClient>(*s_realmConnector, s_clientCache->GetQuestCache(), s_project.spells, s_clientCache->GetItemCache(), s_clientCache->GetCreatureCache(), s_localization);
		s_partyInfo = std::make_unique<PartyInfo>(*s_realmConnector, s_clientCache->GetNameCache());
		s_guildClient = std::make_unique<GuildClient>(*s_realmConnector, s_clientCache->GetGuildCache(), s_project.races, s_project.classes);

		s_spellCast = std::make_unique<SpellCast>(*s_realmConnector, s_project.spells, s_project.ranges);
		s_actionBar = std::make_unique<ActionBar>(*s_realmConnector, s_project.spells, s_clientCache->GetItemCache(), *s_spellCast);
		s_talentClient = std::make_unique<TalentClient>(s_project.talentTabs, s_project.talents, s_project.spells, *s_realmConnector);

		GameStateMgr& gameStateMgr = GameStateMgr::Get();

		// Register game states
		const auto loginState = std::make_shared<LoginState>(gameStateMgr, *s_loginConnector, *s_realmConnector, *s_timerQueue, *s_audio, *s_discord);
		gameStateMgr.AddGameState(loginState);

		const auto worldState = std::make_shared<WorldState>(gameStateMgr, *s_realmConnector, s_project, *s_timerQueue, *s_lootClient, *s_vendorClient, 
			*s_actionBar, *s_spellCast, *s_trainerClient, *s_questClient, *s_audio, *s_partyInfo, *s_charSelect, *s_guildClient, *s_clientCache, *s_discord, s_gameTime, *s_talentClient,
			*s_minimap);
		gameStateMgr.AddGameState(worldState);
		
		// Initialize the game script instance
		s_gameScript = std::make_unique<GameScript>(*s_loginConnector, *s_realmConnector, *s_lootClient, *s_vendorClient, loginState, s_project, *s_actionBar, *s_spellCast, *s_trainerClient, *s_questClient, *s_audio, *s_partyInfo, *s_charCreateInfo, *s_charSelect, *s_guildClient, s_gameTime, *s_talentClient);
		
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
		s_minimap.reset();
		s_timerConnection.disconnect();

		// Remove all registered game states and also leave the current game state.
		GameStateMgr::Get().RemoveAllGameStates();

		s_vendorClient.reset();
		s_lootClient.reset();

		DestroyFrameUI();

		// Reset game script instance
		s_gameScript.reset();

		// Destroy the network thread
		NetDestroy();

		ASSERT(s_clientCache);
		s_clientCache->Save();

		s_audio.reset();

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

// Add this helper function before the ExceptionFilterWin32 function
void LogStackTrace(CONTEXT* context, std::ostringstream& output)
{
	HANDLE process = GetCurrentProcess();
	HANDLE thread = GetCurrentThread();

	// Initialize symbols
	SymInitialize(process, NULL, TRUE);

	// Set options
	SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);

	// Initialize stack frame
	STACKFRAME64 stack = {};
	stack.AddrPC.Offset = context->Rip;
	stack.AddrPC.Mode = AddrModeFlat;
	stack.AddrFrame.Offset = context->Rbp;
	stack.AddrFrame.Mode = AddrModeFlat;
	stack.AddrStack.Offset = context->Rsp;
	stack.AddrStack.Mode = AddrModeFlat;

	// Reserve space for symbol info
	constexpr int MAX_SYMBOL_NAME = 256;
	constexpr int SIZE_OF_SYMBOL_INFO = sizeof(SYMBOL_INFO) + MAX_SYMBOL_NAME;
	uint8_t buffer[SIZE_OF_SYMBOL_INFO];
	SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	symbol->MaxNameLen = MAX_SYMBOL_NAME;

	// Line info
	IMAGEHLP_LINE64 line = {};
	line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
	DWORD displacement;

	// Walk the stack
	for (int frameNum = 0; frameNum < 30; frameNum++)
	{
		BOOL result = StackWalk64(
			IMAGE_FILE_MACHINE_AMD64,
			process,
			thread,
			&stack,
			context,
			NULL,
			SymFunctionTableAccess64,
			SymGetModuleBase64,
			NULL
		);

		if (!result || stack.AddrPC.Offset == 0)
			break;

		output << "Frame " << frameNum << ": ";

		// Try to get symbol name
		if (SymFromAddr(process, stack.AddrPC.Offset, NULL, symbol))
		{
			output << symbol->Name << " (0x" << std::hex << stack.AddrPC.Offset << std::dec << ")";
		}
		else
		{
			output << "Unknown (0x" << std::hex << stack.AddrPC.Offset << std::dec << ")";
		}

		// Try to get line info
		if (SymGetLineFromAddr64(process, stack.AddrPC.Offset, &displacement, &line))
		{
			output << " at " << line.FileName << ":" << line.LineNumber;
		}

		output << "\r\n";
	}

	SymCleanup(process);
}

LONG WINAPI ExceptionFilterWin32(_In_ struct _EXCEPTION_POINTERS* ExceptionInfo)
{
	// Implement an exception handler which formulates a readable error message, include a stack trace and some more data about the crash and
	// then calls an executable to send the error message to some server of us.

	std::ostringstream exceptionMessageBuffer;
	exceptionMessageBuffer << "Unhandled exception: 0x" << std::hex << ExceptionInfo->ExceptionRecord->ExceptionCode;

	// Get exception name from exception code
	switch (ExceptionInfo->ExceptionRecord->ExceptionCode)
	{
	case EXCEPTION_ACCESS_VIOLATION: exceptionMessageBuffer << " EXCEPTION_ACCESS_VIOLATION"; break;
	case EXCEPTION_DATATYPE_MISALIGNMENT: exceptionMessageBuffer << " EXCEPTION_DATATYPE_MISALIGNMENT"; break;
	case EXCEPTION_BREAKPOINT: exceptionMessageBuffer << " EXCEPTION_BREAKPOINT"; break;
	case EXCEPTION_SINGLE_STEP: exceptionMessageBuffer << " EXCEPTION_SINGLE_STEP"; break;
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: exceptionMessageBuffer << " EXCEPTION_ARRAY_BOUNDS_EXCEEDED"; break;
	case EXCEPTION_FLT_DENORMAL_OPERAND: exceptionMessageBuffer << " EXCEPTION_FLT_DENORMAL_OPERAND"; break;
	case EXCEPTION_FLT_DIVIDE_BY_ZERO: exceptionMessageBuffer << " EXCEPTION_FLT_DIVIDE_BY_ZERO"; break;
	case EXCEPTION_FLT_INEXACT_RESULT: exceptionMessageBuffer << " EXCEPTION_FLT_INEXACT_RESULT"; break;
	case EXCEPTION_FLT_INVALID_OPERATION: exceptionMessageBuffer << " EXCEPTION_FLT_INVALID_OPERATION"; break;
	case EXCEPTION_FLT_OVERFLOW: exceptionMessageBuffer << " EXCEPTION_FLT_OVERFLOW"; break;
	case EXCEPTION_FLT_STACK_CHECK: exceptionMessageBuffer << " EXCEPTION_FLT_STACK_CHECK"; break;
	case EXCEPTION_FLT_UNDERFLOW: exceptionMessageBuffer << " EXCEPTION_FLT_UNDERFLOW"; break;
	case EXCEPTION_INT_DIVIDE_BY_ZERO: exceptionMessageBuffer << " EXCEPTION_INT_DIVIDE_BY_ZERO"; break;
	case EXCEPTION_INT_OVERFLOW: exceptionMessageBuffer << " EXCEPTION_INT_OVERFLOW"; break;
	case EXCEPTION_PRIV_INSTRUCTION: exceptionMessageBuffer << " EXCEPTION_PRIV_INSTRUCTION"; break;
	case EXCEPTION_IN_PAGE_ERROR: exceptionMessageBuffer << " EXCEPTION_IN_PAGE_ERROR"; break;
	case EXCEPTION_ILLEGAL_INSTRUCTION: exceptionMessageBuffer << " EXCEPTION_ILLEGAL_INSTRUCTION"; break;
	case EXCEPTION_NONCONTINUABLE_EXCEPTION: exceptionMessageBuffer << " EXCEPTION_NONCONTINUABLE_EXCEPTION"; break;
	case EXCEPTION_STACK_OVERFLOW: exceptionMessageBuffer << " EXCEPTION_STACK_OVERFLOW"; break;
	case EXCEPTION_INVALID_DISPOSITION: exceptionMessageBuffer << " EXCEPTION_INVALID_DISPOSITION"; break;
	case EXCEPTION_GUARD_PAGE: exceptionMessageBuffer << " EXCEPTION_GUARD_PAGE"; break;
	case EXCEPTION_INVALID_HANDLE: exceptionMessageBuffer << " EXCEPTION_INVALID_HANDLE"; break;
	}
	exceptionMessageBuffer << std::endl << std::endl;

	exceptionMessageBuffer << "Exception address: 0x" << std::hex << ExceptionInfo->ExceptionRecord->ExceptionAddress << std::endl;
	exceptionMessageBuffer << "Exception flags: 0x" << std::hex << ExceptionInfo->ExceptionRecord->ExceptionFlags << std::endl;

	exceptionMessageBuffer << "Exception parameters: ";
	for (size_t i = 0; i < ExceptionInfo->ExceptionRecord->NumberParameters; ++i)
	{
		exceptionMessageBuffer << "0x" << std::hex << ExceptionInfo->ExceptionRecord->ExceptionInformation[i] << " ";
	}
	exceptionMessageBuffer << std::endl;

	exceptionMessageBuffer << "Context flags: 0x" << std::hex << ExceptionInfo->ContextRecord->ContextFlags << std::endl;
	exceptionMessageBuffer << "Context address: 0x" << std::hex << ExceptionInfo->ContextRecord->Rip << std::endl;
	exceptionMessageBuffer << "Context stack pointer: 0x" << std::hex << ExceptionInfo->ContextRecord->Rsp << std::endl;
	exceptionMessageBuffer << "Context base pointer: 0x" << std::hex << ExceptionInfo->ContextRecord->Rbp << std::endl;
	exceptionMessageBuffer << "Context instruction pointer: 0x" << std::hex << ExceptionInfo->ContextRecord->Rip << std::endl;
	exceptionMessageBuffer << std::endl;

	exceptionMessageBuffer << "-----------------------------------------------\r\n";
	exceptionMessageBuffer << "STACK TRACE\r\n";
	exceptionMessageBuffer << "-----------------------------------------------\r\n";

	// Get the stack trace
	LogStackTrace(ExceptionInfo->ContextRecord, exceptionMessageBuffer);

	// Log some player data if available
	if (mmo::ObjectMgr::GetActivePlayerGuid())
	{
		if (const auto player = mmo::ObjectMgr::GetActivePlayer())
		{
			exceptionMessageBuffer << "-----------------------------------------------\r\n";
			exceptionMessageBuffer << "PLAYER DATA\r\n";
			exceptionMessageBuffer << "-----------------------------------------------\r\n";
			exceptionMessageBuffer << "Active player GUID: " << mmo::log_hex_digit(player->GetGuid()) << "\r\n";
			exceptionMessageBuffer << "Active player name: " << player->GetName() << "\r\n";
			exceptionMessageBuffer << "Active player level: " << player->GetLevel() << "\r\n";
			exceptionMessageBuffer << "Active player map: " << player->GetMapId() << "\r\n";
			exceptionMessageBuffer << "Active player location: " << player->GetPosition() << "\r\n";
			exceptionMessageBuffer << "Active player facing: " << player->GetFacing().GetValueRadians() << "\r\n";
		}
	}

	// Write to a temporary file
	std::filesystem::path tempPath = std::filesystem::temp_directory_path();

	// Ensure filename contains current timestamp to make it more unique
	const auto now = std::chrono::system_clock::now();
	const auto nowTime = std::chrono::system_clock::to_time_t(now);

	std::tm nowTm;
	localtime_s(&nowTm, &nowTime);
	std::ostringstream filename;
	filename << "mmo_error_" << std::put_time(&nowTm, "%Y%m%d_%H%M%S") << ".txt";

	const std::filesystem::path tempFile = tempPath / filename.str();
	std::ofstream errorFile(tempFile.string());
	if (errorFile.is_open())
	{
		errorFile << exceptionMessageBuffer.str();
		errorFile.close();
	}
	else
	{
		std::cerr << "Could not open error file for writing: " << tempFile.string() << std::endl;
	}

	// Flush log file first
	if (mmo::s_logFile.is_open())
	{
		mmo::s_logFile.flush();
	}

	// Call error sender executable
	mmo::createProcess("./mmo_error.exe", { tempFile.string(), "./Logs/Client.log" });
	return 0;
}


/// Procedural entry point on windows platforms.
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
	// In debug builds, we only want to add an exception handler when no debugger is attached. In release builds, we always set this handler
#ifdef _DEBUG
	if (!IsDebuggerPresent())
	{
#endif
		SetUnhandledExceptionFilter(ExceptionFilterWin32);
#ifdef _DEBUG
	}
#endif

	// Setup log to print each log entry to the debug output on windows
#ifdef _DEBUG
	if (IsDebuggerPresent())
	{
		std::mutex logMutex;
		mmo::g_DefaultLog.signal().connect([&logMutex](const mmo::LogEntry& entry) {
			std::scoped_lock lock{ logMutex };
			OutputDebugStringA((entry.message + "\n").c_str());
			});
	}
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
