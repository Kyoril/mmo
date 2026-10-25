// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "lua/lua.hpp"
#include <string>

namespace
{
	struct EventFixture
	{
		EventFixture()
		{
			lua = luaL_newstate();
			luaL_openlibs(lua);
		}

		~EventFixture()
		{
			lua_close(lua);
		}

		void Run(const char* code)
		{
			const int result = luaL_dostring(lua, code);
			INFO((result ? lua_tostring(lua, -1) : ""));
			REQUIRE(result == 0);
		}

		void Load(const char* name)
		{
			const std::string path = std::string(MMO_UI_SCRIPT_DIR) + "/" + name;
			const int result = luaL_dofile(lua, path.c_str());
			INFO((result ? lua_tostring(lua, -1) : ""));
			REQUIRE(result == 0);
		}

		lua_State* lua;
	};
}

TEST_CASE("Duel UI uses named event and result constants", "[duel][events]")
{
	EventFixture fixture;
	fixture.Run(R"lua(
		DuelEvent = { Pending=10, Invited=11, Countdown=12, Active=13, Finished=14, Error=15 };
		DuelResult = { Cancelled=20, Won=21, Lost=22, Declined=23, Expired=24 };
		StaticDialogs = {};
		function Localize(key) return key; end
		StaticDialog = { Hide=function() hidden=true; end, BringToFront=function() raised=true; end };
		function StaticDialog_Show(which) StaticDialog.which=which; end
		ChatFrame = { AddMessage=function(_, text) message=text; end };
		frame = { RegisterEvent=function(_, _, callback) update=callback; end };
	)lua");
	fixture.Load("Duel.lua");
	fixture.Run(R"lua(
		DuelFrame_OnLoad(frame);
		update(frame, DuelEvent.Invited, 0, "Opponent");
		assert(StaticDialog.which == "DUEL_REQUEST" and raised);
		update(frame, DuelEvent.Pending, 0, "Opponent");
		assert(message == "CHAT_FORMAT_DUEL" and not hidden);
		update(frame, DuelEvent.Countdown, 3, "Opponent");
		assert(hidden);
		local seen = {};
		function Localize(key) seen[key]=true; return key; end
		for _, result in pairs(DuelResult) do update(frame, DuelEvent.Finished, result, "Opponent"); end
		for _, key in ipairs({"DUEL_CANCELLED", "DUEL_WON", "DUEL_LOST", "DUEL_DECLINED", "DUEL_EXPIRED"}) do
			assert(seen[key], key);
		end
		update(frame, DuelEvent.Active, 0, "Opponent"); assert(seen.DUEL_START);
		update(frame, DuelEvent.Error, 0, "Opponent"); assert(seen.DUEL_UNAVAILABLE);
	)lua");
}

TEST_CASE("Target frame refreshes faction relationships through their own event", "[faction][events]")
{
	EventFixture fixture;
	fixture.Run(R"lua(
		events = {};
		TargetFrame = { RegisterEvent=function(_, name, callback) events[name]=callback; end };
	)lua");
	fixture.Load("TargetFrame.lua");
	fixture.Run(R"lua(
		local updates=0;
		function TargetFrame_Update() updates=updates+1; end
		TargetFrame_OnLoad();
		assert(events.FACTION_CHANGED);
		events.FACTION_CHANGED();
		assert(updates==2);
	)lua");
}
