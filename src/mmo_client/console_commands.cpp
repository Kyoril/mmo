// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "console_commands.h"
#include "console.h"
#include "version.h"

#include "log/default_log_levels.h"

#include <fstream>
#include <algorithm>
#include <cctype>

namespace mmo
{
	namespace console_commands
	{
		void ConsoleCommand_Ver(const std::string & cmd, const std::string & args)
		{
			// TODO: Write it to the console
			DLOG("MMO Client Version " << Major << "." << Minor << "." << Build << " (Build: " << Revisision << ")");
		}

		void ConsoleCommand_Run(const std::string & cmd, const std::string & args)
		{
			// Check filename for existance
			if (args.empty())
			{
				ELOG("No filename given");
				return;
			}

			// Open the file for reading
			std::ifstream file(args.c_str(), std::ios::in);
			if (!file)
			{
				ELOG("Could not open script file \"" << args << "\"");
				return;
			}

			// Read the file line by line and try to execute each line
			std::string line;
			while (std::getline(file, line))
			{
				// Trim line
				line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](int ch) {
					return !std::isspace(ch);
				}));

				// Check if line is empty, and if not, execute it
				if (!line.empty())
				{
					Console::ExecuteComamnd(line);
				}
			}
		}
	}
}
