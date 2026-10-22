// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <filesystem>
#include <string>

#include "lua/lua.hpp"

namespace
{
	/// The shipped UI script under test.
	const std::string g_scriptPath = std::string(MMO_UI_SCRIPT_DIR) + "/GameTooltip.lua";

	/// Stand-ins for the globals GameTooltip_AddSpellDetails queries while it builds a spell
	/// tooltip. The spell itself is described by the globals the test sets afterwards.
	const char* g_stubs = R"lua(
		GameTooltip = {};

		spellCooldown = 0;
		spellCastTime = 0;

		local localizedStrings = {
			SECONDS = "sec",
			MINUTES = "min",
			INSTANT = "Instant",
			COOLDOWN_FORMAT = "%s cooldown",
			CAST_FORMAT = "%s cast",
		};

		function Localize(key) return localizedStrings[key] or key; end

		local player = {};
		function player:GetPower(powerType) return 1000; end
		function player:GetLevel() return 60; end

		function GetUnit(name) return player; end

		function IsRacialSpell(spell) return false; end
		function IsPassiveSpell(spell) return false; end
		function IsChanneledSpell(spell) return false; end

		function GetSpellEffectiveCost(spellId) return 0; end
		function GetSpellEffectiveCastTime(spellId) return spellCastTime; end
		function GetSpellEffectiveCooldown(spellId) return spellCooldown; end
		function GetSpellDescription(spell) return "Description"; end

		function GetCachedItemInfo(itemId) return nil; end
		function GetItemCount(itemId) return 0; end
		function GetTradeSkillValue(skillId) return 0; end
	)lua";

	/// Replaces the line sink of the tooltip with a recording one, so the test does not need the
	/// tooltip frames, and builds the details of a spell with the configured cooldown.
	const char* g_buildSpellDetails = R"lua(
		lines = {};

		function GameTooltip_AddLine(line, alignment, color)
			lines[#lines + 1] = tostring(line);
		end

		local spell = { id = 15, name = "Minor Healing Potion", rank = 0, cost = 0, powertype = 0, level = 0 };
		function spell:GetCreatedItem() return 0; end
		function spell:GetNumReagents() return 0; end
		function spell:GetRequiredSkill() return 0, 0, ""; end

		function BuildSpellDetails()
			lines = {};
			GameTooltip_AddSpellDetails(spell);

			return table.concat(lines, "|");
		end
	)lua";

	void RunChunk(lua_State* state, const char* chunk)
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

	/// Builds the spell tooltip details of a spell whose cooldown is the given number of
	/// milliseconds and returns its lines joined by "|". A Lua error inside the tooltip code --
	/// which is what the player sees as "no tooltip at all" -- is returned as "error: <message>"
	/// so the failing test names the cause.
	std::string SpellDetailLines(const int cooldownMs)
	{
		lua_State* state = luaL_newstate();
		REQUIRE(state != nullptr);
		luaL_openlibs(state);

		RunChunk(state, g_stubs);
		RunFile(state, g_scriptPath);
		RunChunk(state, g_buildSpellDetails);

		lua_pushinteger(state, cooldownMs);
		lua_setglobal(state, "spellCooldown");

		lua_getglobal(state, "BuildSpellDetails");

		std::string result;
		if (lua_pcall(state, 0, 1, 0) != LUA_OK)
		{
			result = std::string("error: ") + lua_tostring(state, -1);
		}
		else
		{
			result = lua_tostring(state, -1);
		}

		lua_close(state);
		return result;
	}
}

TEST_CASE("GameTooltip_Script_Is_Available", "[spell_tooltip]")
{
	// Names the cause up front: without the data/client submodule the other cases can only
	// report "cannot open file", which looks like a broken test rather than a missing checkout.
	REQUIRE(std::filesystem::exists(g_scriptPath));
}

TEST_CASE("SpellTooltip_With_Minute_Cooldown_Builds_Cooldown_Line", "[spell_tooltip]")
{
	// A cooldown of a minute or more is shown in minutes, with the trailing ".0" dropped.
	CHECK(SpellDetailLines(60000) == "Instant|1 min cooldown|Description");
}

TEST_CASE("SpellTooltip_With_Second_Cooldown_Builds_Cooldown_Line", "[spell_tooltip]")
{
	// Below a minute the cooldown is shown in seconds, keeping one decimal when it has one.
	CHECK(SpellDetailLines(1500) == "Instant|1.5 sec cooldown|Description");
}

TEST_CASE("SpellTooltip_Without_Cooldown_Has_No_Cooldown_Line", "[spell_tooltip]")
{
	CHECK(SpellDetailLines(0) == "Instant|Description");
}
