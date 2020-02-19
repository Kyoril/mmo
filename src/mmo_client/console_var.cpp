// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "console_var.h"
#include "console_commands.h"
#include "console.h"

#include "base/utilities.h"		// For StrCaseICmp comparator
#include "log/default_log_levels.h"

#include <map>
#include <algorithm>


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
			{ "cvarlist", console_commands::ConsoleCommand_CVarList, ConsoleCommandCategory::Default, "Lists all console variables." }

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
					ILOG("\t" << p.second.GetName() << ":\t" << p.second.GetStringValue());
				}
			});
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
		// Prevent double descruction
		if (!s_consoleVarsInitialized)
		{
			ASSERT(! "ConsoleVarMgr has not been initialized or already been destroyed!");
			return;
		}

		// Unregister console commands
		const size_t numElements = countof(s_cvarCommands);
		for (size_t i = 0; i < numElements; ++i)
		{
			Console::UnregisterCommand(s_cvarCommands[i].command);
		}

		// No longer initialized
		s_consoleVarsInitialized = false;
	}

	ConsoleVar * ConsoleVarMgr::RegisterConsoleVar(const std::string & name, const std::string & description, std::string defaultValue)
	{
		// Check if such a console var already exists and return the new console variable
		auto it = s_consoleVars.find(name);
		if (it != s_consoleVars.end())
		{
			return &it->second;
		}

		// Setup a new console variable
		auto emplaced = s_consoleVars.emplace(std::make_pair(name, ConsoleVar(name, description, std::move(defaultValue))));
		ASSERT(emplaced.first != s_consoleVars.end());

		return &emplaced.first->second;
	}

	bool ConsoleVarMgr::UnregisterConsoleVar(const std::string & name)
	{
		auto it = s_consoleVars.find(name);
		if (it == s_consoleVars.end())
		{
			// CVar didnt exist!
			return false;
		}

		// Set the unregistered flag and don't remove it to prevent memory access errors later on
		it->second.SetFlag(CVF_Unregistered);
		return true;
	}

	ConsoleVar * ConsoleVarMgr::FindConsoleVar(const std::string & name, bool allowUnregistered)
	{
		auto it = s_consoleVars.find(name);
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

	ConsoleVar::ConsoleVar(const std::string & name, const std::string & description, std::string defaultValue)
		: m_name(name)
		, m_description(description)
		, m_defaultValue(std::move(defaultValue))
	{
		Reset();
	}
}
