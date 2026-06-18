// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "lua_script_mgr.h"

#include "luabind/luabind.hpp"
#include "luabind/lua_include.hpp"
#include "luabind/scope.hpp"

extern "C"
{
#include "lualib.h"
}
#include "log/default_log_levels.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/objects/game_world_object_s.h"

#include <filesystem>

namespace mmo
{
	// Static pointer for use in Lua C callbacks
	static LuaScriptMgr* s_instance = nullptr;

	void ServerLuaStateDeleter::operator()(lua_State* state) const
	{
		lua_close(state);
	}

	LuaScriptMgr::LuaScriptMgr()
		: m_luaState(luaL_newstate())
	{
		s_instance = this;

		luaL_openlibs(m_luaState.get());
		luabind::open(m_luaState.get());

		RegisterBindings();
	}

	LuaScriptMgr::~LuaScriptMgr()
	{
		// Release all script table references
		for (auto& [id, ref] : m_creatureScripts)
		{
			luaL_unref(m_luaState.get(), LUA_REGISTRYINDEX, ref);
		}

		for (auto& [id, ref] : m_objectScripts)
		{
			luaL_unref(m_luaState.get(), LUA_REGISTRYINDEX, ref);
		}

		m_creatureScripts.clear();
		m_objectScripts.clear();

		if (s_instance == this)
		{
			s_instance = nullptr;
		}
	}

	void LuaScriptMgr::RegisterBindings()
	{
		lua_State* L = m_luaState.get();

		// Register global functions for script registration
		lua_pushcfunction(L, &LuaScriptMgr::LuaRegisterCreatureScript);
		lua_setglobal(L, "RegisterCreatureScript");

		lua_pushcfunction(L, &LuaScriptMgr::LuaRegisterObjectScript);
		lua_setglobal(L, "RegisterObjectScript");

		// Register game object bindings using luabind
		LUABIND_MODULE(L,
			luabind::scope(
				luabind::class_<GameCreatureS>("Creature")
					.def("GetName", &GameCreatureS::GetName)
					.def("GetHealth", &GameCreatureS::GetHealth)
					.def("GetMaxHealth", &GameCreatureS::GetMaxHealth)
			),
			luabind::scope(
				luabind::class_<GamePlayerS>("Player")
					.def("GetName", &GamePlayerS::GetName)
					.def("GetHealth", &GamePlayerS::GetHealth)
					.def("GetMaxHealth", &GamePlayerS::GetMaxHealth)
			),
			luabind::scope(
				luabind::class_<GameWorldObjectS>("WorldObject")
			)
		);
	}

	void LuaScriptMgr::LoadScripts(const std::string& scriptDir)
	{
		namespace fs = std::filesystem;

		if (!fs::exists(scriptDir) || !fs::is_directory(scriptDir))
		{
			WLOG("LuaScriptMgr: Script directory '" << scriptDir << "' not found, skipping");
			return;
		}

		uint32 count = 0;
		for (const auto& entry : fs::directory_iterator(scriptDir))
		{
			if (entry.is_regular_file() && entry.path().extension() == ".lua")
			{
				if (LoadScript(entry.path().string()))
				{
					count++;
				}
			}
		}

		ILOG("LuaScriptMgr: Loaded " << count << " Lua script(s) from '" << scriptDir << "'");
		ILOG("LuaScriptMgr: " << m_creatureScripts.size() << " creature script(s), " << m_objectScripts.size() << " object script(s) registered");
	}

	bool LuaScriptMgr::LoadScript(const std::string& filePath)
	{
		const int result = luaL_dofile(m_luaState.get(), filePath.c_str());
		if (result != 0)
		{
			const char* error = lua_tostring(m_luaState.get(), -1);
			ELOG("LuaScriptMgr: Failed to load script '" << filePath << "': " << (error ? error : "unknown error"));
			lua_pop(m_luaState.get(), 1);
			return false;
		}

		return true;
	}

	bool LuaScriptMgr::OnGossipHello(GameCreatureS& creature, GamePlayerS& player)
	{
		const auto it = m_creatureScripts.find(creature.GetEntry().id());
		if (it == m_creatureScripts.end())
		{
			return false;
		}

		lua_State* L = m_luaState.get();

		lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
		lua_getfield(L, -1, "OnGossipHello");

		if (!lua_isfunction(L, -1))
		{
			lua_pop(L, 2);
			return false;
		}

		try
		{
			luabind::object func(luabind::from_stack(L, -1));
			luabind::object tbl(luabind::from_stack(L, -2));
			lua_pop(L, 2);

			luabind::call_function<void>(func, &creature, &player);
			return true;
		}
		catch (const luabind::error& e)
		{
			ELOG("LuaScriptMgr: OnGossipHello error: " << e.what());
			return false;
		}
	}

	bool LuaScriptMgr::OnGossipSelect(GameCreatureS& creature, GamePlayerS& player, uint32 optionId)
	{
		const auto it = m_creatureScripts.find(creature.GetEntry().id());
		if (it == m_creatureScripts.end())
		{
			return false;
		}

		lua_State* L = m_luaState.get();

		lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
		lua_getfield(L, -1, "OnGossipSelect");

		if (!lua_isfunction(L, -1))
		{
			lua_pop(L, 2);
			return false;
		}

		try
		{
			luabind::object func(luabind::from_stack(L, -1));
			lua_pop(L, 2);

			luabind::call_function<void>(func, &creature, &player, optionId);
			return true;
		}
		catch (const luabind::error& e)
		{
			ELOG("LuaScriptMgr: OnGossipSelect error: " << e.what());
			return false;
		}
	}

	bool LuaScriptMgr::OnQuestAccept(GameCreatureS& creature, GamePlayerS& player, uint32 questId)
	{
		const auto it = m_creatureScripts.find(creature.GetEntry().id());
		if (it == m_creatureScripts.end())
		{
			return false;
		}

		lua_State* L = m_luaState.get();

		lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
		lua_getfield(L, -1, "OnQuestAccept");

		if (!lua_isfunction(L, -1))
		{
			lua_pop(L, 2);
			return false;
		}

		try
		{
			luabind::object func(luabind::from_stack(L, -1));
			lua_pop(L, 2);

			luabind::call_function<void>(func, &creature, &player, questId);
			return true;
		}
		catch (const luabind::error& e)
		{
			ELOG("LuaScriptMgr: OnQuestAccept error: " << e.what());
			return false;
		}
	}

	bool LuaScriptMgr::OnQuestComplete(GameCreatureS& creature, GamePlayerS& player, uint32 questId)
	{
		const auto it = m_creatureScripts.find(creature.GetEntry().id());
		if (it == m_creatureScripts.end())
		{
			return false;
		}

		lua_State* L = m_luaState.get();

		lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
		lua_getfield(L, -1, "OnQuestComplete");

		if (!lua_isfunction(L, -1))
		{
			lua_pop(L, 2);
			return false;
		}

		try
		{
			luabind::object func(luabind::from_stack(L, -1));
			lua_pop(L, 2);

			luabind::call_function<void>(func, &creature, &player, questId);
			return true;
		}
		catch (const luabind::error& e)
		{
			ELOG("LuaScriptMgr: OnQuestComplete error: " << e.what());
			return false;
		}
	}

	bool LuaScriptMgr::OnUse(GameWorldObjectS& worldObject, GamePlayerS& player)
	{
		const uint32 entryId = worldObject.Get<uint32>(object_fields::Entry);
		const auto it = m_objectScripts.find(entryId);
		if (it == m_objectScripts.end())
		{
			return false;
		}

		lua_State* L = m_luaState.get();

		lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
		lua_getfield(L, -1, "OnUse");

		if (!lua_isfunction(L, -1))
		{
			lua_pop(L, 2);
			return false;
		}

		try
		{
			luabind::object func(luabind::from_stack(L, -1));
			lua_pop(L, 2);

			luabind::call_function<void>(func, &worldObject, &player);
			return true;
		}
		catch (const luabind::error& e)
		{
			ELOG("LuaScriptMgr: OnUse error: " << e.what());
			return false;
		}
	}

	bool LuaScriptMgr::HasCreatureScript(uint32 entryId) const
	{
		return m_creatureScripts.find(entryId) != m_creatureScripts.end();
	}

	bool LuaScriptMgr::HasObjectScript(uint32 entryId) const
	{
		return m_objectScripts.find(entryId) != m_objectScripts.end();
	}

	int LuaScriptMgr::LuaRegisterCreatureScript(lua_State* L)
	{
		if (!s_instance)
		{
			return luaL_error(L, "LuaScriptMgr not initialized");
		}

		const int entryId = static_cast<int>(luaL_checkinteger(L, 1));
		luaL_checktype(L, 2, LUA_TTABLE);

		// Store a reference to the table in the Lua registry
		lua_pushvalue(L, 2);
		const int ref = luaL_ref(L, LUA_REGISTRYINDEX);

		s_instance->m_creatureScripts[static_cast<uint32>(entryId)] = ref;
		return 0;
	}

	int LuaScriptMgr::LuaRegisterObjectScript(lua_State* L)
	{
		if (!s_instance)
		{
			return luaL_error(L, "LuaScriptMgr not initialized");
		}

		const int entryId = static_cast<int>(luaL_checkinteger(L, 1));
		luaL_checktype(L, 2, LUA_TTABLE);

		// Store a reference to the table in the Lua registry
		lua_pushvalue(L, 2);
		const int ref = luaL_ref(L, LUA_REGISTRYINDEX);

		s_instance->m_objectScripts[static_cast<uint32>(entryId)] = ref;
		return 0;
	}
}
