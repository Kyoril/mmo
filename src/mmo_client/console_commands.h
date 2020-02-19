// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include <string>


namespace mmo
{
	namespace console_commands
	{
		/// Handles the ver command to print the current client version to the console.
		void ConsoleCommand_Ver(const std::string& cmd, const std::string& args);
		/// Handles the run command to execute a console script file (used for config files for example).
		void ConsoleCommand_Run(const std::string& cmd, const std::string& args);

		/// Handles the set command to set a console variable,
		void ConsoleCommand_Set(const std::string& cmd, const std::string& args);
		/// Handles the unset command to unset / remove a console variable.
		void ConsoleCommand_Unset(const std::string& cmd, const std::string& args);
		/// Handles the reset command to reset a console variable.
		void ConsoleCommand_Reset(const std::string& cmd, const std::string& args);
		/// Handle the cvarlist command to list all registered console variables.
		void ConsoleCommand_CVarList(const std::string& cmd, const std::string& args);


#ifdef MMO_WITH_DEV_COMMANDS
		// TODO: Add console commands in here which are only available to developers
#endif
	}
}
