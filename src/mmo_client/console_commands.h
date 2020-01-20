// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include <string>


namespace mmo
{
	namespace console_commands
	{
		void ConsoleCommand_Ver(const std::string& cmd, const std::string& args);
		void ConsoleCommand_Run(const std::string& cmd, const std::string& args);

#ifdef MMO_WITH_DEV_COMMANDS
		// TODO: Add console commands in here which are only available to developers
#endif
	}
}
