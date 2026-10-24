// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "lua/lua.hpp"
#include <string>

namespace
{
	struct ActionBarMoveFixture
	{
		ActionBarMoveFixture()
		{
			lua = luaL_newstate();
			luaL_openlibs(lua);
			Run(R"lua(
				keys={"SHIFT"}; shift=false; control=false; alt=false;
				picked=0; used=0; button={id=1};
				function GetKeysForBinding(name) assert(name=="MOVEACTIONBAR"); return keys; end
				function IsShiftKeyDown() return shift; end
				function IsControlKeyDown() return control; end
				function IsAltKeyDown() return alt; end
				function PickupActionButton(slot) picked=picked+1; lastSlot=slot; end
				function UseActionButton(slot) used=used+1; end
				function CursorHasPayload() return true; end
			)lua");
			const std::string path = std::string(MMO_UI_SCRIPT_DIR) + "/ActionBar.lua";
			REQUIRE(luaL_loadfile(lua, path.c_str()) == 0);
			Call();
		}

		~ActionBarMoveFixture()
		{
			lua_close(lua);
		}

		void Call()
		{
			const int result = lua_pcall(lua, 0, 0, 0);
			INFO((result ? lua_tostring(lua, -1) : ""));
			REQUIRE(result == 0);
		}

		void Run(const char* code)
		{
			REQUIRE(luaL_loadstring(lua, code) == 0);
			Call();
		}

		lua_State* lua = nullptr;
	};
}

TEST_CASE("Action bar pickup uses the player's assigned hold key on both bars", "[actionbar][bindings]")
{
	ActionBarMoveFixture fixture;
	fixture.Run(R"lua(
		ActionBarButton_OnDrag(button,"LEFT"); assert(picked==0);
		shift=true; ActionBarButton_OnDrag(button,"LEFT"); assert(picked==1);
		keys={"CONTROL"}; ActionBarButton_OnDrag(button,"LEFT"); assert(picked==1);
		control=true; button.id=13;
		ActionBarButton_OnDrag(button,"RIGHT"); assert(picked==1);
		ActionBarButton_OnDrag(button,"LEFT"); assert(picked==2 and lastSlot==12);
		control=false; keys={"ALT"}; alt=true;
		ActionButton_OnClick(button,"LEFT"); assert(picked==3 and used==0);
		alt=false; ActionButton_OnClick(button,"LEFT"); assert(picked==3 and used==1);
	)lua");
}

TEST_CASE("Custom hold keys release and old assignments cannot unlock the bar", "[actionbar][bindings]")
{
	ActionBarMoveFixture fixture;
	fixture.Run(R"lua(
		keys={"F12"}; shift=true;
		ActionButton_OnClick(button,"LEFT"); assert(used==1 and picked==0);
		ActionBar_MoveKeyBinding("F12","DOWN");
		ActionBarButton_OnDrag(button,"LEFT"); assert(picked==1);
		ActionBar_MoveKeyBinding("F12","UP");
		ActionBarButton_OnDrag(button,"LEFT"); assert(picked==1);
		ActionBar_MoveKeyBinding("F12","DOWN"); keys={"F11"};
		ActionBarButton_OnDrag(button,"LEFT"); assert(picked==1);
		keys={}; ActionBarButton_OnDrag(button,"LEFT"); assert(picked==1);
	)lua");
}

TEST_CASE("Hold combinations require their modifiers and dropping needs no hold key", "[actionbar][bindings]")
{
	ActionBarMoveFixture fixture;
	fixture.Run(R"lua(
		keys={"CTRL-F12","F11"}; ActionBar_MoveKeyBinding("CTRL-F12","DOWN");
		assert(not ActionBar_IsMoveKeyDown());
		control=true; assert(ActionBar_IsMoveKeyDown());
		control=false; assert(not ActionBar_IsMoveKeyDown());
		ActionBar_MoveKeyBinding("F11","DOWN"); assert(ActionBar_IsMoveKeyDown());
		ActionBar_MoveKeyBinding("F11","UP"); assert(not ActionBar_IsMoveKeyDown());
		ActionBarButton_OnDrop(button,"LEFT"); assert(picked==1);
	)lua");
}
