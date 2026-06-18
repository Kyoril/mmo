// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <memory>
#include <unordered_map>
#include <string>

struct lua_State;

namespace mmo
{
	class GameCreatureS;
	class GamePlayerS;
	class GameWorldObjectS;

	/// Helper class for deleting a server-side lua_State in a smart pointer.
	struct ServerLuaStateDeleter final
	{
		void operator()(lua_State* state) const;
	};

	/// @brief Manages server-side Lua scripts for creature and world object event hooks.
	/// Scripts are loaded from a directory and registered per creature/object entry ID.
	class LuaScriptMgr final
	{
	public:
		/// @brief Constructs the Lua script manager and initializes the Lua VM.
		LuaScriptMgr();

		/// @brief Destructor — closes the Lua state.
		~LuaScriptMgr();

		// Non-copyable
		LuaScriptMgr(const LuaScriptMgr&) = delete;
		LuaScriptMgr& operator=(const LuaScriptMgr&) = delete;

		/// @brief Loads all Lua scripts from the specified directory.
		/// @param scriptDir Path to the scripts directory (e.g., "data/scripts").
		void LoadScripts(const std::string& scriptDir);

		/// @brief Fires OnGossipHello event for a creature entry.
		/// @param creature The creature being interacted with.
		/// @param player The player who initiated gossip.
		/// @return True if a script handled the event, false otherwise.
		bool OnGossipHello(GameCreatureS& creature, GamePlayerS& player);

		/// @brief Fires OnGossipSelect event for a creature entry.
		/// @param creature The creature being interacted with.
		/// @param player The player who selected an option.
		/// @param optionId The gossip option selected.
		/// @return True if a script handled the event, false otherwise.
		bool OnGossipSelect(GameCreatureS& creature, GamePlayerS& player, uint32 optionId);

		/// @brief Fires OnQuestAccept event for a creature entry.
		/// @param creature The quest giver creature.
		/// @param player The player accepting the quest.
		/// @param questId The quest being accepted.
		/// @return True if a script handled the event, false otherwise.
		bool OnQuestAccept(GameCreatureS& creature, GamePlayerS& player, uint32 questId);

		/// @brief Fires OnQuestComplete event for a creature entry.
		/// @param creature The quest turn-in creature.
		/// @param player The player completing the quest.
		/// @param questId The quest being completed.
		/// @return True if a script handled the event, false otherwise.
		bool OnQuestComplete(GameCreatureS& creature, GamePlayerS& player, uint32 questId);

		/// @brief Fires OnUse event for a world object entry.
		/// @param worldObject The world object being used.
		/// @param player The player using the object.
		/// @return True if a script handled the event, false otherwise.
		bool OnUse(GameWorldObjectS& worldObject, GamePlayerS& player);

		/// @brief Checks if a creature entry has a registered script.
		/// @param entryId The creature entry ID.
		/// @return True if a script is registered.
		bool HasCreatureScript(uint32 entryId) const;

		/// @brief Checks if an object entry has a registered script.
		/// @param entryId The object entry ID.
		/// @return True if a script is registered.
		bool HasObjectScript(uint32 entryId) const;

	private:
		/// @brief Registers luabind bindings for game object types.
		void RegisterBindings();

		/// @brief Loads a single Lua script file.
		/// @param filePath Path to the Lua script file.
		/// @return True if loaded successfully.
		bool LoadScript(const std::string& filePath);

		/// @brief Static callback for RegisterCreatureScript Lua global.
		/// @param L The Lua state.
		/// @return Number of return values.
		static int LuaRegisterCreatureScript(lua_State* L);

		/// @brief Static callback for RegisterObjectScript Lua global.
		/// @param L The Lua state.
		/// @return Number of return values.
		static int LuaRegisterObjectScript(lua_State* L);

	private:
		typedef std::unique_ptr<lua_State, ServerLuaStateDeleter> LuaStatePtr;

		LuaStatePtr m_luaState;

		/// Maps entry ID to Lua registry reference for the script table.
		std::unordered_map<uint32, int> m_creatureScripts;
		std::unordered_map<uint32, int> m_objectScripts;
	};
}
