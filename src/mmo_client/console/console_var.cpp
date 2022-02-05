// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "console_var.h"
#include "console_commands.h"
#include "console.h"

#include "base/utilities.h"		// For StrCaseICmp comparator
#include "log/default_log_levels.h"

#include <map>
#include <algorithm>
#include <fstream>
#include <utility>


namespace mmo
{
	/// All console variables.
	static std::map<std::string, ConsoleVar, mmo::StrCaseIComp> s_consoleVars;
	static bool s_consoleVarsInitialized = false;


	// Console commands that are managed by the console variable manager
	namespace
	{
		typedef struct
		{
			const std::string command;
			ConsoleCommandHandler handler;
			ConsoleCommandCategory category;
			const std::string help;
		} CVarConsoleCommandList;

		static const CVarConsoleCommandList s_cvarCommands[] =
		{
			{ "set", console_commands::ConsoleCommand_Set, ConsoleCommandCategory::Default, "Sets a console variable to a given value." },
			{ "reset", console_commands::ConsoleCommand_Reset, ConsoleCommandCategory::Default, "Resets a console variable to it's default value." },
			{ "unset", console_commands::ConsoleCommand_Unset, ConsoleCommandCategory::Default, "Removes a console variable."},
			{ "cvarlist", console_commands::ConsoleCommand_CVarList, ConsoleCommandCategory::Default, "Lists all console variables." },
			{ "saveconfig", console_commands::ConsoleCommand_SaveConfig, ConsoleCommandCategory::Default, "Saves all console variables into a config file." }

			// TODO: Add your console var mgr commands to this list to make them register and unregister automatically
		};
	}


	// Console command implementations
	namespace console_commands
	{
		void ConsoleCommand_Set(const std::string & cmd, const std::string & args)
		{
			// Split argument string
			std::vector<std::string> arguments;
			TokenizeString(args, arguments);

			// Check for argument count and eventually print an error string into the console
			if (arguments.size() < 2)
			{
				ELOG("Invalid number of arguments provided! Usage: set [cvar_name] [value]");
				return;
			}

			// Set the respective cvar value if the cvar already exists, or create a new cvar if it doesn't exist yet
			ConsoleVar* var = ConsoleVarMgr::FindConsoleVar(arguments[0]);
			if (var != nullptr)
			{
				var->Set(arguments[1]);
			}
			else
			{
				ConsoleVarMgr::RegisterConsoleVar(arguments[0], "", arguments[1]);
			}
		}

		void ConsoleCommand_Unset(const std::string & cmd, const std::string & args)
		{
			// Validate argument count
			if (args.empty())
			{
				ELOG("Invalid number of arguments provided! Usage: unset [cvar_name]");
				return;
			}

			ConsoleVarMgr::UnregisterConsoleVar(args);
		}

		void ConsoleCommand_Reset(const std::string & cmd, const std::string & args)
		{
			// Validate argument count
			if (args.empty())
			{
				ELOG("Invalid number of arguments provided! Usage: unset [cvar_name]");
				return;
			}

			// Try to find cvar by name
			ConsoleVar* var = ConsoleVarMgr::FindConsoleVar(args);
			if (var == nullptr || !var->IsValid())
			{
				ELOG("Could not find cvar \"" << args << "\"");
				return;
			}

			// Reset variable
			var->Reset();
		}

		void ConsoleCommand_CVarList(const std::string & cmd, const std::string & args)
		{
			ILOG("Currently registered cvars:");

			// Enumerate all valid cvars and print them
			std::for_each(s_consoleVars.begin(), s_consoleVars.end(), [](std::pair<const std::string, ConsoleVar>& p) {
				if (p.second.IsValid())
				{
					ILOG("\t" << p.second.GetName() << ":\t" << p.second.GetStringValue() << "(Modified: " << p.second.HasBeenModified() << ")");
				}
			});
		}

		void ConsoleCommand_SaveConfig(const std::string& cmd, const std::string& args)
		{
			std::fstream configFile("Config/Config.cfg", std::ios::out);
			if (!configFile)
			{
				ELOG("Unable to save config file!");
				return;
			}

			for(auto& pair : s_consoleVars)
			{
				configFile << "set " << pair.second.GetName() << " " << pair.second.GetStringValue() << "\n";
			}

			configFile.flush();
			configFile.close();

			ILOG("Successfully saved config file!");
		}
	}


	void ConsoleVarMgr::Initialize()
	{
		// Prevent double initialization
		if (s_consoleVarsInitialized)
		{
			ASSERT(! "ConsoleVarMgr has already been initialized!");
			return;
		}

		// Register console commands
		const size_t numElements = countof(s_cvarCommands);
		for (size_t i = 0; i < numElements; ++i)
		{
			const auto& e = s_cvarCommands[i];
			Console::RegisterCommand(e.command, e.handler, e.category, e.help);
		}

		// Now initialized
		s_consoleVarsInitialized = true;
	}

	void ConsoleVarMgr::Destroy()
	{
		if (!s_consoleVarsInitialized)
		{
			ASSERT(! "ConsoleVarMgr has not been initialized or already been destroyed!");
			return;
		}
		
		const size_t numElements = countof(s_cvarCommands);
		for (size_t i = 0; i < numElements; ++i)
		{
			Console::UnregisterCommand(s_cvarCommands[i].command);
		}

		s_consoleVars.clear();

		// No longer initialized
		s_consoleVarsInitialized = false;
	}

	ConsoleVar * ConsoleVarMgr::RegisterConsoleVar(const std::string & name, const std::string & description, std::string defaultValue)
	{
		// Check if such a console var already exists and return the new console variable
		const auto it = s_consoleVars.find(name);
		if (it != s_consoleVars.end())
		{
			return &it->second;
		}

		// Setup a new console variable
		const auto [key, _] = s_consoleVars.emplace(std::make_pair(name, ConsoleVar(name, description, std::move(defaultValue))));
		ASSERT(key != s_consoleVars.end());
		
		return &key->second;
	}

	bool ConsoleVarMgr::UnregisterConsoleVar(const std::string & name)
	{
		const auto it = s_consoleVars.find(name);
		if (it == s_consoleVars.end())
		{
			// CVar didn't exist!
			return false;
		}

		// Set the unregistered flag and don't remove it to prevent memory access errors later on
		it->second.SetFlag(CVF_Unregistered);
		return true;
	}

	ConsoleVar * ConsoleVarMgr::FindConsoleVar(const std::string & name, bool allowUnregistered)
	{
		const auto it = s_consoleVars.find(name);
		if (it == s_consoleVars.end())
		{
			return nullptr;
		}

		// If we don't want to allow to find console vars that have the unregistered flag set, then
		// apply a filter.
		if (!allowUnregistered)
		{
			if (it->second.HasFlag(CVF_Unregistered))
			{
				return nullptr;
			}
		}

		// Found a cvar instance!
		return &(it->second);
	}

	ConsoleVar::ConsoleVar(std::string name, std::string description, std::string defaultValue)
		: m_name(std::move(name))
		, m_description(std::move(description))
		, m_defaultValue(std::move(defaultValue))
	{
		Reset();
	}
}
