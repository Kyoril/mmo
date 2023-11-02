// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "base/utilities.h"
#include "base/filesystem.h"

#include <string>
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
	class Console 
		: public NonCopyable
	{
		/// This struct contains a console command.
		struct ConsoleCommand
		{
			/// A help text that is displayed to the user in the console when using the help command.
			std::string help;
			/// The command handler callback function that is executed when executing the command.
			ConsoleCommandHandler handler;
			/// The category of this console command to allow better organization.
			ConsoleCommandCategory category = ConsoleCommandCategory::Default;
		};

	public:
		/// Initializes the console system.
		static void Initialize(const std::filesystem::path& configFile);
		/// Destroys the console system.
		static void Destroy();

		static void ListCommands();

	public:
		/// Registers a new console command.
		static void RegisterCommand(
			const std::string& command,
			ConsoleCommandHandler handler,
			ConsoleCommandCategory category = ConsoleCommandCategory::Default,
			const std::string& help = std::string());

		/// Removes a registered console command.
		static void UnregisterCommand(const std::string& command);

		/// Executes the given command line to execute console commands.
		static void ExecuteCommand(const std::string& commandLine);

	private:
		/// Executed when a key has been pressed. Repeated.
		static bool KeyDown(int32 key);

		static bool KeyUp(int32 key);

		/// Executed when a key character has been fired.
		static bool KeyChar(uint16 codepoint);

		/// Executed when the console should be painted (paint signal in event loop is fired).
		static void Paint();

	private:
		/// A map of all registered console commands.
		static std::map<std::string, ConsoleCommand, StrCaseIComp> s_consoleCommands;
	};
}
