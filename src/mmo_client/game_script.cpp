// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "game_script.h"
#include "console.h"
#include "login_connector.h"

#include <string>
#include <functional>

#include "luabind/luabind.hpp"
#include "luabind/iterator_policy.hpp"


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

		static int32 Script_GetRealmCount(LoginConnector& connector)
		{
			return static_cast<int32>(connector.GetRealms().size());
		}

		static const mmo::RealmData* Script_GetRealmData(LoginConnector& connector, int32 index)
		{
			const auto& realms = connector.GetRealms();
			if (index < 0 || index >= realms.size())
			{
				ELOG("GetRealm: Invalid realm index provided (" << index << ")");
				return nullptr;
			}

			return &realms[index];
		}

		static void Script_Print(const std::string& text)
		{
			ILOG(text);
		}
	}


	GameScript::GameScript(LoginConnector& loginConnector)
		: m_loginConnector(loginConnector)
		, m_globalFunctionsRegistered(false)
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
			luabind::class_<mmo::RealmData>("RealmData")
				.def_readonly("id", &mmo::RealmData::id)
				.def_readonly("name", &mmo::RealmData::name),

			luabind::class_<LoginConnector>("LoginConnector")
				.def("GetRealms", &LoginConnector::GetRealms, luabind::return_stl_iterator()),

			luabind::def("RunConsoleCommand", &Script_RunConsoleCommand),
			luabind::def("print", &Script_Print)
		];

		// Set login connector instance
		luabind::globals(m_luaState.get())["loginConnector"] = &m_loginConnector;

		// Functions now registered
		m_globalFunctionsRegistered = true;
	}


}