// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <filesystem>
#include <string>

#include "lua/lua.hpp"

namespace
{
	/// The shipped UI script under test.
	const std::string g_scriptPath = std::string(MMO_UI_SCRIPT_DIR) + "/InventoryButton.lua";

	/// Stand-ins for the globals InventoryItemButton_OnClick touches on its right click path.
	/// Every call is recorded in a table the test reads back afterwards.
	const char* g_stubs = R"lua(
		calls = {};

		local function record(name, arg)
			calls[#calls + 1] = { name = name, arg = arg };
		end

		vendorVisible = false;

		function IsTrading() return false; end
		function MailFrame_IsSendPanelVisible() return false; end
		function IsShiftKeyDown() return false; end

		BankFrame = { IsVisible = function(self) return false; end };
		VendorFrame = { IsVisible = function(self) return vendorVisible; end };

		function UseContainerItem(slot) record("UseContainerItem", slot); end
		function PickupContainerItem(slot) record("PickupContainerItem", slot); end
		function TradeFrame_AddFromInventory(slot) record("TradeFrame_AddFromInventory", slot); end
		function MailFrame_AttachItem(slot) record("MailFrame_AttachItem", slot); end
		function BankFrame_OnItemRightClicked(slot) record("BankFrame_OnItemRightClicked", slot); end
		function PlaySound(path) record("PlaySound", path); end
		function Localize(key) return key; end
	)lua";

	/// Flattens the recorded calls of one kind into a single string so the test can assert on
	/// them without walking the Lua stack for every entry.
	const char* g_collect = R"lua(
		function Collect(name)
			local result = "";
			for _, call in ipairs(calls) do
				if call.name == name then
					result = result .. tostring(call.arg) .. ";";
				end
			end
			return result;
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

	std::string Collect(lua_State* state, const char* callName)
	{
		lua_getglobal(state, "Collect");
		lua_pushstring(state, callName);
		if (lua_pcall(state, 1, 1, 0) != LUA_OK)
		{
			FAIL(lua_tostring(state, -1));
		}

		const std::string result = lua_tostring(state, -1);
		lua_pop(state, 1);
		return result;
	}

	/// Right clicks inventory slot 23 with the vendor window either open or closed and returns
	/// the sounds the UI played.
	std::string RightClickSounds(const bool vendorVisible, std::string& outUsedSlots)
	{
		lua_State* state = luaL_newstate();
		REQUIRE(state != nullptr);
		luaL_openlibs(state);

		RunChunk(state, g_stubs);
		RunChunk(state, g_collect);
		RunFile(state, g_scriptPath);

		lua_pushboolean(state, vendorVisible ? 1 : 0);
		lua_setglobal(state, "vendorVisible");

		RunChunk(state, "InventoryItemButton_OnClick({ id = 23 }, \"RIGHT\");");

		const std::string sounds = Collect(state, "PlaySound");
		outUsedSlots = Collect(state, "UseContainerItem");

		lua_close(state);
		return sounds;
	}
}

TEST_CASE("InventoryButton_Script_Is_Available", "[inventory_button]")
{
	// Names the cause up front: without the data/client submodule the other cases can only
	// report "cannot open file", which looks like a broken test rather than a missing checkout.
	REQUIRE(std::filesystem::exists(g_scriptPath));
}

TEST_CASE("InventoryButton_RightClick_Without_Vendor_Plays_No_Coin_Sound", "[inventory_button]")
{
	std::string usedSlots;
	const std::string sounds = RightClickSounds(false, usedSlots);

	// The item is used, not sold, so the money sound must stay silent.
	CHECK(usedSlots == "23;");
	CHECK(sounds == "");
}

TEST_CASE("InventoryButton_RightClick_At_Vendor_Plays_Coin_Sound", "[inventory_button]")
{
	std::string usedSlots;
	const std::string sounds = RightClickSounds(true, usedSlots);

	// With a merchant open the same right click sells the item, and that is the one action the
	// coin sound belongs to.
	CHECK(usedSlots == "23;");
	CHECK(sounds == "Sound/Interface/Coins_1.wav;");
}
