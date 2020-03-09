// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "game_script.h"
#include "console.h"

#include <string>
#include <functional>


namespace mmo
{
	// Static methods
	namespace
	{
		/// Allows executing a console command from within lua.
		static int Script_RunConsoleCommand(lua_State* state)
		{
			// Read the first argument (memory managed by lua_State)
			const char* cmdLineArg = luaL_checkstring(state, 1);
			Console::ExecuteCommand(cmdLineArg);

			// No results returned
			return 0;
		}
	}

	namespace
	{
		/// Script function helper struct
		struct ScriptFunctionHelper final
		{
			/// Name under which the function is callable in lua.
			std::string name;
			/// The callback function pointer that is executed.
			lua_CFunction callback;
		};

		// Static script function table for automatically registering global script functions.
		static const ScriptFunctionHelper s_scriptFunctions[] =
		{
			// Name, Callback
			{ "RunConsoleCommand", &Script_RunConsoleCommand },

			// TODO: Add script functions to be registered automatically in here
		};
	}

	GameScript::GameScript()
		: m_globalFunctionsRegistered(false)
	{
		// Initialize the lua state instance
		m_luaState = LuaStatePtr(luaL_newstate());

		// Register custom global functions to the lua state instance
		RegisterGlobalFunctions();
	}

	void GameScript::RegisterGlobalFunctions()
	{
		// Check for double initialization
		ASSERT(!m_globalFunctionsRegistered);

		// Register all script functions
		for (int i = 0; i < sizeof(s_scriptFunctions) / sizeof(ScriptFunctionHelper); ++i)
		{
			lua_pushcfunction(m_luaState.get(), s_scriptFunctions[i].callback);
			lua_setglobal(m_luaState.get(), s_scriptFunctions[i].name.c_str());
		}

		// Functions now registered
		m_globalFunctionsRegistered = true;
	}


}
