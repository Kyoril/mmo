// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <string>


namespace mmo
{
	namespace console_commands
	{
		/// @brief Handles the ver command to print the current client version to the console.
		void ConsoleCommand_Ver(const std::string& cmd, const std::string& args);

		/// @brief Handles the run command to execute a console script file (used for config files for example).
		void ConsoleCommand_Run(const std::string& cmd, const std::string& args);

		/// @brief Handles the quit command to terminate the event loop.
		void ConsoleCommand_Quit(const std::string& cmd, const std::string& args);

		/// @brief Handles the set command to set a console variable,
		void ConsoleCommand_Set(const std::string& cmd, const std::string& args);

		/// @brief Handles the unset command to unset / remove a console variable.
		void ConsoleCommand_Unset(const std::string& cmd, const std::string& args);

		/// @brief Handles the reset command to reset a console variable.
		void ConsoleCommand_Reset(const std::string& cmd, const std::string& args);

		/// @brief Handle the cvarlist command to list all registered console variables.
		void ConsoleCommand_CVarList(const std::string& cmd, const std::string& args);

		/// Saves all console variables into a config file.
		void ConsoleCommand_SaveConfig(const std::string& cmd, const std::string& args);

		/// @brief Lists all available commands in the console.
		void ConsoleCommand_List(const std::string& cmd, const std::string& args);

#ifdef MMO_WITH_DEV_COMMANDS
		// TODO: Add console commands in here which are only available to developers
#endif
	}
}
