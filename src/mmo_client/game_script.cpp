// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "game_script.h"
#include "console.h"

#include <string>
#include <functional>

#include "luabind/luabind.hpp"


namespace mmo
{
	// Static script methods
	namespace
	{
		/// Allows executing a console command from within lua.
		static void Script_RunConsoleCommand(const char* cmdLine)
		{
			ASSERT(cmdLine);
			Console::ExecuteCommand(cmdLine);
		}
	}


	GameScript::GameScript()
		: m_globalFunctionsRegistered(false)
	{
		// Initialize the lua state instance
		m_luaState = LuaStatePtr(luaL_newstate());

		// Initialize luabind
		luabind::open(m_luaState.get());

		// Register custom global functions to the lua state instance
		RegisterGlobalFunctions();
	}

	void GameScript::RegisterGlobalFunctions()
	{
		// Check for double initialization
		ASSERT(!m_globalFunctionsRegistered);

		// Register common functions
		luabind::module(m_luaState.get())
		[
			luabind::def("RunConsoleCommand", &Script_RunConsoleCommand)
		];

		// Functions now registered
		m_globalFunctionsRegistered = true;
	}


}
