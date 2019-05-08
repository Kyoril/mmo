// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"

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
	class Console : public NonCopyable
	{
		/// A custom compare operator used to make string keys in the s_consoleCommands
		/// map case insensitive.
		struct ConsoleCommandComp
		{
			bool operator() (const std::string& lhs, const std::string& rhs) const {
				return _stricmp(lhs.c_str(), rhs.c_str()) < 0;
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
		/// Initializes the console system.
		static void Initialize(const std::string& configFile);
		/// Destroys the console system.
		static void Destroy();

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
		static void ExecuteComamnd(std::string commandLine);

	private:
		/// A map of all registered console commands.
		static std::map<std::string, ConsoleCommand, ConsoleCommandComp> s_consoleCommands;
	};
}
