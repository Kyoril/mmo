// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "console_commands.h"
#include "console.h"
#include "version.h"
#include "event_loop.h"

#include "log/default_log_levels.h"

#include <fstream>
#include <algorithm>
#include <cctype>

#include "assets/asset_registry.h"


namespace mmo::console_commands
{
	void ConsoleCommand_Ver(const std::string & cmd, const std::string & args)
	{
		DLOG("MMO Client Version " << Major << "." << Minor << "." << Build << " (Build: " << Revision << ")");
	}

	void ConsoleCommand_Run(const std::string & cmd, const std::string & args)
	{
		// Check filename for existence
		if (args.empty())
		{
			ELOG("No filename given");
			return;
		}

		std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(args.c_str());
		if (!file)
		{
			ELOG("Could not open script file \"" << args << "\"");
			return;
		}

		// Read the file line by line and try to execute each line
		std::string line;
		while (std::getline(*file, line))
		{
			// Trim line
			line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](int ch) {
				return !std::isspace(ch);
			}));
			
			if (!line.empty() && line[0] != '#')
			{
				Console::ExecuteCommand(line);
			}
		}
	}

	void ConsoleCommand_Quit(const std::string& cmd, const std::string& args)
	{
		EventLoop::Terminate(0);
	}

}
