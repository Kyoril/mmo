// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/macros.h"

#include "deps/lua/lua.hpp"

#include <memory>


namespace mmo
{
	/// Helper class for deleting a lua_State in a smart pointer.
	struct LuaStateDeleter final
	{
		void operator()(lua_State* state)
		{
			// Call lua_close on the state to delete it
			lua_close(state);
		}
	};

	/// This class manages everything related to lua scripts for the game client.
	class GameScript final
		: public NonCopyable
	{
	public:
		GameScript();

	public:
		/// Gets the current lua state
		inline lua_State& GetLuaState() { ASSERT(m_luaState);  return *m_luaState; }

	private:
		/// Registers global functions to the internal lua state.
		void RegisterGlobalFunctions();

	private:
		typedef std::unique_ptr<lua_State, LuaStateDeleter> LuaStatePtr;

		/// The current lua state.
		LuaStatePtr m_luaState;
		/// Whether the global functions have been registered.
		bool m_globalFunctionsRegistered = false;
	};
}
