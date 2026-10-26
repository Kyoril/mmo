// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "lua/lua.hpp"
#include <string>

namespace
{
	struct DisposalFixture
	{
		DisposalFixture()
		{
			lua = luaL_newstate();
			luaL_openlibs(lua);
			Run(R"lua(
				function Localize(key) return key; end
				function NewItem(name)
					return { GetName=function() return name; end,
						GetStackCount=function() return 1; end,
						IsSameItem=function(self,other) return self==other; end };
				end
				first=NewItem("First"); second=NewItem("Second");
				items={[23]=first,[24]=second}; cursorSlot=23; cursorType="item";
				StaticDialog={Hide=function() end};
				function CursorHasItem() return cursorType=="item"; end
				function GetCursorItemSlot() return cursorSlot; end
				function GetInventorySlotItem(unit,slot) return items[slot]; end
				function ClearCursorItem() cursorType=nil; cursorSlot=nil; end
				function DestroyItemInstance(item) destroyed=item; end
				function DestroyItem(slot,count) legacyDestroyed=slot; end
			)lua");
			Load("StaticDialog.lua");
			Load("GameParent.lua");
			Run(R"lua(
				function StaticDialog_Show(which,name)
					StaticDialog.which=which; StaticDialog.data=nil; shownName=name;
					return StaticDialog;
				end
			)lua");
		}

		~DisposalFixture()
		{
			lua_close(lua);
		}
		void Run(const char* code)
		{
			REQUIRE(luaL_loadstring(lua, code) == 0);
			const int result = lua_pcall(lua, 0, 0, 0);
			INFO((result ? lua_tostring(lua, -1) : ""));
			REQUIRE(result == 0);
		}
		void Load(const char* name)
		{
			const std::string path = std::string(MMO_UI_SCRIPT_DIR) + "/" + name;
			REQUIRE(luaL_loadfile(lua, path.c_str()) == 0);
			const int result = lua_pcall(lua, 0, 0, 0);
			INFO((result ? lua_tostring(lua, -1) : ""));
			REQUIRE(result == 0);
		}
		lua_State* lua;
	};
}

TEST_CASE("Disposal remembers the item after returning it to inventory", "[inventory][disposal]")
{
	DisposalFixture fixture;
	fixture.Run(R"lua(
		HandleInvalidItemDrop();
		assert(StaticDialog.data==first);
		ClearCursorItem();
		StaticDialog_Button1Clicked();
		assert(destroyed==first);
		assert(StaticDialog.data==nil);
		destroyed=nil;
		StaticDialog_Button1Clicked();
		assert(destroyed==nil);
	)lua");
}

TEST_CASE("Disposal follows a moved item and preserves an unrelated cursor item", "[inventory][disposal]")
{
	DisposalFixture fixture;
	fixture.Run(R"lua(
		HandleInvalidItemDrop();
		items[23]=second; items[24]=first;
		cursorSlot=23;
		StaticDialog_Button1Clicked();
		assert(destroyed==first);
		assert(CursorHasItem() and cursorSlot==23);
	)lua");
}

TEST_CASE("Cancelling disposal clears the pending confirmation", "[inventory][disposal]")
{
	DisposalFixture fixture;
	fixture.Run(R"lua(
		HandleInvalidItemDrop();
		StaticDialog_Button2Clicked();
		assert(destroyed==nil and legacyDestroyed==nil);
		assert(StaticDialog.data==nil and not CursorHasItem());
		StaticDialog_Button1Clicked();
		assert(destroyed==nil and legacyDestroyed==nil);
	)lua");
}

TEST_CASE("Dropping an action shortcut cannot request item disposal", "[inventory][disposal]")
{
	DisposalFixture fixture;
	fixture.Run(R"lua(
		cursorType="shortcut";
		HandleInvalidItemDrop();
		assert(StaticDialog.which==nil);
		assert(destroyed==nil and legacyDestroyed==nil);
	)lua");
}
