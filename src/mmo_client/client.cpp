// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// This file contains the entry point of the game and takes care of initializing the
// game as well as starting the main loop for the application. This is used on all
// client-supported platforms.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")
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
#include "fmod_audio/fmod_audio.h"
#else
#include "null_audio/null_audio.h"
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
#include "systems/cooldown_manager.h"
#include "systems/trainer_client.h"
#include "systems/quest_client.h"
#include "systems/party_info.h"
#include "systems/guild_client.h"
#include "systems/friend_client.h"
#include "systems/talent_client.h"
#include "systems/inventory_client.h"

#include "ui/minimap.h"

#include "char_creation/char_create_info.h"
#include "char_creation/char_select.h"
#include "base/create_process.h"
#include "game_client/object_mgr.h"
#include "ui/unit_model_frame.h"
#include "client_context.h"
#include "client_runtime.h"

#include "luabind/luabind.hpp"
#include "luabind/iterator_policy.hpp"
#include "luabind/out_value_policy.hpp"
#include "ui/minimap_frame.h"
#include "ui/cooldown_frame.h"

////////////////////////////////////////////////////////////////
// Network handler

namespace mmo
{
	void DispatchOnGameThread(std::function<void()> &&f)
	{
		GetClientContext().timerService.post(std::move(f));
	}
}

namespace mmo
{
	// The io service used for networking
	/// Runs the network thread to handle incoming packets.
	void NetWorkProc()
	{
		auto& context = GetClientContext();
		if (!context.runtime)
		{
			return;
		}

		context.runtime->PollNetwork();
	}

	/// Initializes the login connector and starts one or multiple network
	/// threads to handle network events. Should be called from the main
	/// thread.
	void NetInit()
	{
		auto& context = GetClientContext();
		context.runtime = std::make_unique<ClientRuntime>();
		context.runtime->Initialize();
	}

	/// Destroy the login connector, cuts all opened connections and waits
	/// for all network threads to stop running. Thus, this method should
	/// be called from the main thread.
	void NetDestroy()
	{
		auto& context = GetClientContext();
		if (!context.runtime)
		{
			return;
		}

		context.runtime->Shutdown();
		context.runtime.reset();
	}
}

////////////////////////////////////////////////////////////////
// FrameUI stuff

namespace mmo
{
	extern Cursor g_cursor;

	/// Initializes everything related to FrameUI.
	bool InitializeFrameUi()
	{
		auto& context = GetClientContext();

		if (const auto window = GraphicsDevice::Get().GetAutoCreatedWindow())
		{
			context.frameUiConnections += window->Resized.connect([](uint16 width, uint16 height)
															{ FrameManager::Get().NotifyScreenSizeChanged(width, height); });
		}

		if (!context.localization.LoadFromFile())
		{
			ELOG("Failed to initialize localization!");
		}

		// Initialize the frame manager
		FrameManager::Initialize(&context.gameScript->GetLuaState(), context.localization);

		// Register model renderer
		FrameManager::Get().RegisterFrameRenderer("ModelRenderer", [](const std::string &name)
												  { return std::make_unique<ModelRenderer>(name); });

		// Register model frame type
		FrameManager::Get().RegisterFrameFactory("Model", [](const std::string &name)
												 { return std::make_shared<ModelFrame>(name); });
		FrameManager::Get().RegisterFrameFactory("UnitModel", [](const std::string &name)
												 { return std::make_shared<UnitModelFrame>(name); });
		FrameManager::Get().RegisterFrameFactory("Minimap", [](const std::string &name)
												 { return std::make_shared<MinimapFrame>(name, *GetClientContext().minimap); });
		FrameManager::Get().RegisterFrameFactory("Cooldown", [](const std::string &name)
												 { return std::make_shared<CooldownFrame>(name); });

		// Setup cursor graphics
		g_cursor.LoadCursorTypeFromTexture(CursorType::Pointer, "Interface/Cursor/pointer001.htex");
		g_cursor.LoadCursorTypeFromTexture(CursorType::Interact, "Interface/Cursor/gears001.htex");
		g_cursor.LoadCursorTypeFromTexture(CursorType::Attack, "Interface/Cursor/sword001.htex");
		g_cursor.LoadCursorTypeFromTexture(CursorType::Loot, "Interface/Cursor/bag001.htex");
		g_cursor.LoadCursorTypeFromTexture(CursorType::Gossip, "Interface/Cursor/talk001.htex");
		g_cursor.SetCursorType(CursorType::Pointer);

		// Connect idle event
		context.frameUiConnections += EventLoop::Idle.connect([](float deltaSeconds, GameTime timestamp)
														{ FrameManager::Get().Update(deltaSeconds); });

		// Watch for mouse events
		context.frameUiConnections += EventLoop::MouseMove.connect([](int32 x, int32 y)
															 {
			FrameManager::Get().NotifyMouseMoved(Point(x, y));
			return false; });
		context.frameUiConnections += EventLoop::MouseDown.connect([](EMouseButton button, int32 x, int32 y)
															 {
			// Returns true if the UI consumed the event, preventing further processing
			return FrameManager::Get().NotifyMouseDown(static_cast<MouseButton>(1 << static_cast<int32>(button)), Point(x, y)); });
		context.frameUiConnections += EventLoop::MouseUp.connect([](EMouseButton button, int32 x, int32 y)
														   {
			// Returns true if the UI consumed the event, preventing further processing
			return FrameManager::Get().NotifyMouseUp(static_cast<MouseButton>(1 << static_cast<int32>(button)), Point(x, y)); });

		context.frameUiConnections += EventLoop::KeyDown.connect([](int32 key, bool)
														   {
			FrameManager::Get().NotifyKeyDown(key);
			return true; });
		context.frameUiConnections += EventLoop::KeyChar.connect([](uint16 codepoint)
														   {
			FrameManager::Get().NotifyKeyChar(codepoint);
			return false; });
		context.frameUiConnections += EventLoop::KeyUp.connect([](int32 key)
														 {
			FrameManager::Get().NotifyKeyUp(key);
			return false; });

		// Expose model frame methods to lua

		LUABIND_MODULE(&context.gameScript->GetLuaState(),
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
							   .def("SetUnit", &UnitModelFrame::SetUnit)),

					   luabind::scope(
						   luabind::class_<CooldownFrame, Frame>("CooldownFrame")
							   .def("SetProgress", &CooldownFrame::SetProgress)
							   .def("GetProgress", &CooldownFrame::GetProgress)));

		return true;
	}

	/// Destroys everything related to FrameUI.
	void DestroyFrameUI()
	{
		auto& context = GetClientContext();

		// Disconnect FrameUI connections
		context.frameUiConnections.disconnect();

		// Unregister model renderer
		FrameManager::Get().RemoveFrameRenderer("ModelRenderer");
		FrameManager::Get().UnregisterFrameFactory("Model");
		FrameManager::Get().UnregisterFrameFactory("UnitModel");
		FrameManager::Get().UnregisterFrameFactory("Cooldown");

		// Destroy the frame manager
		FrameManager::Destroy();
	}
}

////////////////////////////////////////////////////////////////
// Initialization and destruction

namespace mmo
{
	/// Initializes the global game systems.
	bool InitializeGlobal()
	{
		auto& context = GetClientContext();
		context.project = std::make_unique<proto_client::Project>();
		context.gameTime = std::make_unique<GameTimeComponent>();
		context.timerQueue = std::make_unique<TimerQueue>(context.timerService);

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
		context.logFile.open((currentPath / "Logs" / "Client.log").string().c_str(), std::ios::out);
		if (context.logFile)
		{
			context.logConnection = g_DefaultLog.signal().connect(
				[](const LogEntry &entry)
				{
					printLogEntry(GetClientContext().logFile, entry, g_DefaultFileLogOptions);
				});
		}

		// Initialize the event loop
		EventLoop::Initialize();

		// Initialize the console client which also loads the config file
		Console::Initialize("Config/Config.cfg");

		// Initialize network threads
		NetInit();

#ifdef _WIN32
		context.audio = std::make_unique<FMODAudio>();
#else
		context.audio = std::make_unique<NullAudio>();
#endif
		context.audio->Create();

		// Run service
		context.timerConnection = EventLoop::Idle.connect([&](float, const mmo::GameTime &)
													{
				NetWorkProc();
				GetClientContext().timerService.poll_one(); });

		// Verify the connector instances have been initialized
		ASSERT(context.runtime && context.runtime->IsInitialized());
		auto& loginConnector = context.runtime->GetLoginConnector();
		auto& realmConnector = context.runtime->GetRealmConnector();

		// Load game data
		if (!context.project->load("ClientDB"))
		{
			ELOG("Failed to load project files!");
			return false;
		}

		context.clientCache = std::make_unique<ClientCache>(realmConnector);
		if (!context.clientCache->Load())
		{
			ELOG("Failed to load the client cache!");
			return false;
		}

		context.discord = std::make_unique<Discord>();
		context.discord->Initialize();

		// Setup minimap
		context.minimap = std::make_unique<Minimap>(256);

		context.charCreateInfo = std::make_unique<CharCreateInfo>(*context.project, realmConnector);
		context.charSelect = std::make_unique<CharSelect>(*context.project, realmConnector);

		// Initialize loot client
		context.lootClient = std::make_unique<LootClient>(realmConnector, context.clientCache->GetItemCache());
		context.vendorClient = std::make_unique<VendorClient>(realmConnector, context.clientCache->GetItemCache());
		context.trainerClient = std::make_unique<TrainerClient>(realmConnector, context.project->spells);
		context.inventoryClient = std::make_unique<InventoryClient>(realmConnector);
		context.questClient = std::make_unique<QuestClient>(realmConnector, context.clientCache->GetQuestCache(), context.project->spells, context.clientCache->GetItemCache(), context.clientCache->GetCreatureCache(), context.localization);
		context.partyInfo = std::make_unique<PartyInfo>(realmConnector, context.clientCache->GetNameCache());
		context.guildClient = std::make_unique<GuildClient>(realmConnector, context.clientCache->GetGuildCache(), context.project->races, context.project->classes);
		context.friendClient = std::make_unique<FriendClient>(realmConnector, context.project->races, context.project->classes);
		context.spellCast = std::make_unique<SpellCast>(realmConnector, context.project->spells, context.project->ranges);
		context.cooldownManager = std::make_unique<CooldownManager>();
		context.actionBar = std::make_unique<ActionBar>(realmConnector, context.project->spells, context.clientCache->GetItemCache(), *context.spellCast);
		context.talentClient = std::make_unique<TalentClient>(context.project->talentTabs, context.project->talents, context.project->spells, realmConnector);

		GameStateMgr &gameStateMgr = GameStateMgr::Get();

		// Register game states
		const auto loginState = std::make_shared<LoginState>(gameStateMgr, loginConnector, realmConnector, *context.timerQueue, *context.audio, *context.discord);
		gameStateMgr.AddGameState(loginState);

		const auto worldState = std::make_shared<WorldState>(gameStateMgr, realmConnector, *context.project, *context.timerQueue, *context.lootClient, *context.vendorClient,
															 *context.actionBar, *context.spellCast, *context.cooldownManager, *context.trainerClient, *context.questClient, *context.audio, *context.partyInfo, *context.charSelect, *context.guildClient, *context.friendClient, *context.clientCache, *context.discord, *context.gameTime, *context.talentClient,
															 *context.minimap, *context.inventoryClient);
		gameStateMgr.AddGameState(worldState);

		// Initialize the game script instance
		context.gameScript = std::make_unique<GameScript>(loginConnector, realmConnector, *context.lootClient, *context.vendorClient, loginState, *context.project, *context.actionBar, *context.spellCast, *context.cooldownManager, *context.trainerClient, *context.questClient, *context.audio, *context.partyInfo, *context.charCreateInfo, *context.charSelect, *context.guildClient, *context.friendClient, *context.gameTime, *context.talentClient);
		context.minimap->RegisterScriptFunctions(&context.gameScript->GetLuaState());

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
		auto& context = GetClientContext();
		context.minimap.reset();
		context.timerConnection.disconnect();

		// Remove all registered game states and also leave the current game state.
		GameStateMgr::Get().RemoveAllGameStates();

		// Shutdown client systems
		if (context.lootClient)
			context.lootClient->Shutdown();
		if (context.vendorClient)
			context.vendorClient->Shutdown();
		if (context.trainerClient)
			context.trainerClient->Shutdown();
		if (context.inventoryClient)
			context.inventoryClient->Shutdown();
		if (context.questClient)
			context.questClient->Shutdown();
		if (context.partyInfo)
			context.partyInfo->Shutdown();
		if (context.guildClient)
			context.guildClient->Shutdown();
		if (context.friendClient)
			context.friendClient->Shutdown();
		context.vendorClient.reset();
		context.lootClient.reset();
		context.trainerClient.reset();
		context.inventoryClient.reset();

		DestroyFrameUI();

		// Reset game script instance
		context.gameScript.reset();

		// Destroy the network thread
		NetDestroy();

		ASSERT(context.clientCache);
		context.clientCache->Save();

		context.talentClient.reset();
		context.actionBar.reset();
		context.spellCast.reset();
		context.cooldownManager.reset();
		context.questClient.reset();
		context.partyInfo.reset();
		context.guildClient.reset();
		context.friendClient.reset();
		context.charCreateInfo.reset();
		context.charSelect.reset();
		context.discord.reset();
		context.clientCache.reset();
		context.timerQueue.reset();
		context.gameTime.reset();
		context.project.reset();

		context.audio.reset();

		// Destroy the graphics device object
		Console::Destroy();
		EventLoop::Destroy();
		AssetRegistry::Destroy();

		// Destroy log
		context.logConnection.disconnect();
		context.logFile.close();
		context.timerService.stop();
		context.timerService.reset();
	}
}

////////////////////////////////////////////////////////////////
// Entry point

namespace mmo
{
	/// Shared entry point of the application on all platforms.
	int32 CommonMain(int argc, char **argv)
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
void LogStackTrace(CONTEXT *context, std::ostringstream &output)
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
	SYMBOL_INFO *symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);
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
			NULL);

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

LONG WINAPI ExceptionFilterWin32(_In_ struct _EXCEPTION_POINTERS *ExceptionInfo)
{
	// Implement an exception handler which formulates a readable error message, include a stack trace and some more data about the crash and
	// then calls an executable to send the error message to some server of us.

	std::ostringstream exceptionMessageBuffer;
	exceptionMessageBuffer << "Unhandled exception: 0x" << std::hex << ExceptionInfo->ExceptionRecord->ExceptionCode;

	// Get exception name from exception code
	switch (ExceptionInfo->ExceptionRecord->ExceptionCode)
	{
	case EXCEPTION_ACCESS_VIOLATION:
		exceptionMessageBuffer << " EXCEPTION_ACCESS_VIOLATION";
		break;
	case EXCEPTION_DATATYPE_MISALIGNMENT:
		exceptionMessageBuffer << " EXCEPTION_DATATYPE_MISALIGNMENT";
		break;
	case EXCEPTION_BREAKPOINT:
		exceptionMessageBuffer << " EXCEPTION_BREAKPOINT";
		break;
	case EXCEPTION_SINGLE_STEP:
		exceptionMessageBuffer << " EXCEPTION_SINGLE_STEP";
		break;
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		exceptionMessageBuffer << " EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
		break;
	case EXCEPTION_FLT_DENORMAL_OPERAND:
		exceptionMessageBuffer << " EXCEPTION_FLT_DENORMAL_OPERAND";
		break;
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		exceptionMessageBuffer << " EXCEPTION_FLT_DIVIDE_BY_ZERO";
		break;
	case EXCEPTION_FLT_INEXACT_RESULT:
		exceptionMessageBuffer << " EXCEPTION_FLT_INEXACT_RESULT";
		break;
	case EXCEPTION_FLT_INVALID_OPERATION:
		exceptionMessageBuffer << " EXCEPTION_FLT_INVALID_OPERATION";
		break;
	case EXCEPTION_FLT_OVERFLOW:
		exceptionMessageBuffer << " EXCEPTION_FLT_OVERFLOW";
		break;
	case EXCEPTION_FLT_STACK_CHECK:
		exceptionMessageBuffer << " EXCEPTION_FLT_STACK_CHECK";
		break;
	case EXCEPTION_FLT_UNDERFLOW:
		exceptionMessageBuffer << " EXCEPTION_FLT_UNDERFLOW";
		break;
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
		exceptionMessageBuffer << " EXCEPTION_INT_DIVIDE_BY_ZERO";
		break;
	case EXCEPTION_INT_OVERFLOW:
		exceptionMessageBuffer << " EXCEPTION_INT_OVERFLOW";
		break;
	case EXCEPTION_PRIV_INSTRUCTION:
		exceptionMessageBuffer << " EXCEPTION_PRIV_INSTRUCTION";
		break;
	case EXCEPTION_IN_PAGE_ERROR:
		exceptionMessageBuffer << " EXCEPTION_IN_PAGE_ERROR";
		break;
	case EXCEPTION_ILLEGAL_INSTRUCTION:
		exceptionMessageBuffer << " EXCEPTION_ILLEGAL_INSTRUCTION";
		break;
	case EXCEPTION_NONCONTINUABLE_EXCEPTION:
		exceptionMessageBuffer << " EXCEPTION_NONCONTINUABLE_EXCEPTION";
		break;
	case EXCEPTION_STACK_OVERFLOW:
		exceptionMessageBuffer << " EXCEPTION_STACK_OVERFLOW";
		break;
	case EXCEPTION_INVALID_DISPOSITION:
		exceptionMessageBuffer << " EXCEPTION_INVALID_DISPOSITION";
		break;
	case EXCEPTION_GUARD_PAGE:
		exceptionMessageBuffer << " EXCEPTION_GUARD_PAGE";
		break;
	case EXCEPTION_INVALID_HANDLE:
		exceptionMessageBuffer << " EXCEPTION_INVALID_HANDLE";
		break;
	}
	exceptionMessageBuffer << std::endl
						   << std::endl;

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
	if (mmo::GetClientContext().logFile.is_open())
	{
		mmo::GetClientContext().logFile.flush();
	}

	// Call error sender executable
	mmo::createProcess("./mmo_error.exe", {tempFile.string(), "./Logs/Client.log"});
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
		mmo::g_DefaultLog.signal().connect([&logMutex](const mmo::LogEntry &entry)
										   {
			std::scoped_lock lock{ logMutex };
			OutputDebugStringA((entry.message + "\n").c_str()); });
	}
#endif

	// Split command line arguments
	auto argc = 0;
	auto *const argv = CommandLineToArgvA(GetCommandLine(), &argc);

	// If no debugger is attached, encapsulate the common main function in a try/catch
	// block to catch exceptions and display an error message to the user.
	if (!::IsDebuggerPresent())
	{
		try
		{
			// Run the common main function
			return mmo::CommonMain(argc, argv);
		}
		catch (const std::exception &e)
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
int main_osx(int argc, char *argv[]);
#endif

/// Procedural entry point on non-windows platforms.
int main(int argc, char **argv)
{
	// Set working directory
	std::filesystem::current_path(mmo::GetExecutablePath());

	// Write everything log entry to cout on non-windows platforms by default
	std::mutex logMutex;
	mmo::g_DefaultLog.signal().connect([&logMutex](const mmo::LogEntry &entry)
									   {
		std::scoped_lock lock{ logMutex };
		mmo::printLogEntry(std::cout, entry, mmo::g_DefaultConsoleLogOptions); });

#ifdef __APPLE__
	return main_osx(argc, argv);
#else
	// Finally, run the common main function on all platforms
	return mmo::CommonMain(argc, argv);
#endif
}

#endif
