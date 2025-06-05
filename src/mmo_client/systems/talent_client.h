#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

struct lua_State;

namespace mmo
{
	class TalentClient final : public NonCopyable
	{
	public:

		void Initialize();

		void Shutdown();

		void RegisterScriptFunctions(lua_State* luaState);

	};
}
