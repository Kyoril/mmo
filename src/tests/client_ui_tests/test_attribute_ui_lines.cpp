// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <string>

#include "lua/lua.hpp"

// Attributes are game data. The UI must name them from the data (GetAttributeName and friends)
// instead of a hard-coded list of five, both in item tooltips and in the level up chat lines.

namespace
{
	const std::string g_tooltipScript = std::string(MMO_UI_SCRIPT_DIR) + "/GameTooltip.lua";
	const std::string g_chatScript = std::string(MMO_UI_SCRIPT_DIR) + "/ChatFrame.lua";

	/// Two attributes with non-default ids and names, so nothing can pass by matching the
	/// shipped Stamina .. Spirit by accident.
	const char* g_stubs = R"lua(
		lines = "";

		local attributes = {
			{ id = 1, name = "Might", levelUp = "Might +%d" },
			{ id = 7, name = "Luck", levelUp = "" },
		};
		local gains = { [1] = 3, [7] = 2 };

		function GetNumAttributes() return #attributes; end
		function GetAttributeInfo(index)
			local a = attributes[index + 1];
			if not a then return -1, "", ""; end
			return a.id, a.name, "";
		end
		function GetAttributeName(id)
			for _, a in ipairs(attributes) do if a.id == id then return a.name; end end
			return "";
		end
		function GetAttributeLevelUpText(id)
			for _, a in ipairs(attributes) do if a.id == id then return a.levelUp; end end
			return "";
		end
		function GetLevelUpAttributeGain(id) return gains[id] or 0; end

		function Localize(key) return key; end
		function GameTooltip_AddLine(text) lines = lines .. text .. ";"; end
		function PlaySoundEntry() end
		ChatFrame = { AddMessage = function(self, text) lines = lines .. text .. ";"; end };
	)lua";

	void Run(lua_State* state, const char* chunk)
	{
		if (luaL_loadstring(state, chunk) != LUA_OK || lua_pcall(state, 0, 0, 0) != LUA_OK)
		{
			FAIL(lua_tostring(state, -1));
		}
	}

	void RunFile(lua_State* state, const std::string& path)
	{
		if (luaL_loadfile(state, path.c_str()) != LUA_OK || lua_pcall(state, 0, 0, 0) != LUA_OK)
		{
			FAIL(lua_tostring(state, -1));
		}
	}

	std::string Lines(lua_State* state)
	{
		lua_getglobal(state, "lines");
		std::string result = lua_tostring(state, -1);
		lua_pop(state, 1);
		return result;
	}

	lua_State* NewState(const std::string& script)
	{
		lua_State* state = luaL_newstate();
		REQUIRE(state != nullptr);
		luaL_openlibs(state);
		Run(state, g_stubs);
		RunFile(state, script);
		return state;
	}
}

TEST_CASE("Item tooltips name attribute stats from the game data", "[attribute_ui]")
{
	lua_State* state = NewState(g_tooltipScript);

	// The script defines the real GameTooltip_AddLine, which needs tooltip frames: record instead
	Run(state, "function GameTooltip_AddLine(text) lines = lines .. text .. \";\"; end");

	// Slot 0: attribute 7, slot 1: a non-attribute stat, slot 2: an attribute that no longer
	// exists (skipped), slot 3: unused (ends the list), slot 4: never reached.
	Run(state, R"lua(
		local stats = {
			[0] = { attribute = 7, type = nil, value = 5 },
			[1] = { attribute = -1, type = "MANA", value = 20 },
			[2] = { attribute = 3, type = nil, value = 9 },
			[3] = { attribute = -1, type = nil, value = 0 },
			[4] = { attribute = 1, type = nil, value = 4 },
		};
		local item = {
			GetStatAttribute = function(self, i) return stats[i] and stats[i].attribute or -1; end,
			GetStatType = function(self, i) return stats[i] and stats[i].type or nil; end,
			GetStatValue = function(self, i) return stats[i] and stats[i].value or 0; end,
		};
		GameTooltip_AddItemStatLines(item);
	)lua");

	CHECK(Lines(state) == "+5 Luck;+20 MANA;");
	lua_close(state);
}

TEST_CASE("Level up chat prints the gain of every attribute that has a level up line", "[attribute_ui]")
{
	lua_State* state = NewState(g_chatScript);

	Run(state, "ChatFrame_OnPlayerLevelUp(nil, 5, 0, 0, 0, 0);");

	// Luck gained 2 but has no level up line; LEVEL_UP is the untranslated key from the stub
	CHECK(Lines(state) == "LEVEL_UP;Might +3;");
	lua_close(state);
}
