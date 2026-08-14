// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// This file contains the entry point of the game and takes care of initializing the
// game as well as starting the main loop for the application. This is used on all
// client-supported platforms.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include "discord.h"

#include "base/typedefs.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"
#include "assets/asset_registry.h"

#include "event_loop.h"
#include "console/console.h"
#include "net/login_connector.h"
#include "net/realm_connector.h"
#include "game_states/game_state_mgr.h"
#include "ui/model_renderer.h"

#ifdef _WIN32
#include "fmod_audio/fmod_audio.h"
#else
#include "null_audio/null_audio.h"
#endif

#include <iostream>
#include <fstream>
#include <memory>
#include <set>

#include "game_states/world_state.h"

#include "base/executable_path.h"
#include "client_data/project.h"

#include "systems/inventory_client.h"

#include "ui/minimap.h"

#include "base/create_process.h"
#include "game_client/object_mgr.h"
#include "client_context.h"
#include "client_application.h"

namespace mmo
{
	void DispatchOnGameThread(std::function<void()> &&f)
	{
		GetClientContext().timerService.post(std::move(f));
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

		ClientApplication app;
		if (app.Start())
		{
			EventLoop::Run();
			app.Stop();
		}

		return 0;
	}
}

#ifdef _WIN32
#include "base/win_utility.h"
#include "base/crash_handler.h"

namespace
{
	/// Installs the shared crash handler for the client.
	///
	/// The client used to carry its own copy of the Windows stack walk and report writer. That copy
	/// and the servers' emitted the same format, which a third tool (tools/symbolicate_crash.ps1)
	/// parses, so keeping two of them was an invitation to drift. What stays client specific is
	/// wired through the config: where the report goes, the player state worth recording, and
	/// handing the finished report to the uploader.
	void InstallClientCrashHandler()
	{
		mmo::CrashHandlerConfig config;
		config.applicationName = "mmo_client";
		config.outputDirectory = std::filesystem::temp_directory_path();

		config.onCrash = [](std::vector<std::string>& details)
		{
			if (mmo::ObjectMgr::GetActivePlayerGuid())
			{
				if (const auto player = mmo::ObjectMgr::GetActivePlayer())
				{
					std::ostringstream line;
					line << "Active player GUID: " << mmo::log_hex_digit(player->GetGuid());
					details.push_back(line.str());
					details.push_back("Active player name: " + player->GetName());
					details.push_back("Active player level: " + std::to_string(player->GetLevel()));
					details.push_back("Active player map: " + std::to_string(player->GetMapId()));

					std::ostringstream location;
					location << "Active player location: " << player->GetPosition()
						<< " facing " << player->GetFacing().GetValueRadians();
					details.push_back(location.str());
				}
			}

			if (mmo::GetClientContext().logFile.is_open())
			{
				mmo::GetClientContext().logFile.flush();
			}
		};

		config.onReportWritten = [](const std::filesystem::path& reportPath)
		{
			mmo::createProcess("./mmo_error.exe", { reportPath.string(), "./Logs/Client.log" });
		};

		mmo::InstallCrashHandler(std::move(config));
	}
}


/// Procedural entry point on windows platforms.
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
	// InstallCrashHandler itself stands down when a debugger is attached in debug builds.
	InstallClientCrashHandler();

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
