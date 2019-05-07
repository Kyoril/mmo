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

#include <thread>
#include <memory>
#include <mutex>

#include <functional>
#include <map>


namespace mmo
{
	/// Defines a console command handler function pointer.
	typedef std::function<void(const std::string& command, const std::string& args)> ConsoleCommandHandler;

	/// Enumerates console command categories.
	enum class ConsoleCommandCategory
	{
		/// The default console command category.
		Default,
		/// Console commands related to graphics.
		Graphics,
		/// Console commands related to debugging.
		Debug,
		/// Gameplay-related console commands.
		Game,
		/// Game master (admin) related console commands.
		Gm,
		/// Sound-related console commands.
		Sound
	};

	/// This class manages the console client.
	class Console : public NonCopyable
	{
		struct ConsoleCommandComp
		{
			bool operator() (const std::string& lhs, const std::string& rhs) const {
				return stricmp(lhs.c_str(), rhs.c_str()) < 0;
			}
		};

		/// This struct contains a console command.
		struct ConsoleCommand
		{
			std::string help;
			ConsoleCommandHandler handler;
			ConsoleCommandCategory category = ConsoleCommandCategory::Default;
		};

	public:
		/// Registers a new console command.
		static void RegisterCommand(
			const std::string& command,
			ConsoleCommandHandler handler,
			ConsoleCommandCategory category = ConsoleCommandCategory::Default,
			const std::string& help = std::string())
		{
			// Don't do anything if this console command is already registered
			auto it = s_consoleCommands.find(command);
			if (it == s_consoleCommands.end())
			{
				return;
			}

			// Build command structure and add it
			ConsoleCommand cmd;
			cmd.category = category;
			cmd.help = std::move(help);
			cmd.handler = std::move(handler);
			s_consoleCommands.emplace(command, cmd);
		}
		/// Removes a registered console command.
		static void UnregisterCommand(const std::string& command)
		{
			// Remove the respective iterator
			auto it = s_consoleCommands.find(command);
			if (it != s_consoleCommands.end())
			{
				s_consoleCommands.erase(it);
			}
		}
		/// Executes the given command line to execute console commands.
		static void ExecuteComamnd(std::string commandLine)
		{
			// Will hold the command name
			std::string command;

			// Find the first space and use it to get the command
			auto space = commandLine.find(' ');
			if (space == commandLine.npos)
			{
				command = commandLine;
			}
			else
			{
				command = commandLine.substr(0, space);
			}


		}

	private:
		/// A map of all registered console commands.
		static std::map<std::string, ConsoleCommand, ConsoleCommandComp> s_consoleCommands;
	};
}


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
