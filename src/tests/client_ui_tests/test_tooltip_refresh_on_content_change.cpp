// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <filesystem>
#include <string>

#include "lua/lua.hpp"

// The mouse enters a frame only once, so a button whose content is replaced while the cursor
// rests on it (right clicking an inventory item to equip it, dropping a spell onto an action
// button) kept showing the previous content's tooltip until the mouse moved. Both scripts must
// refill the tooltip from the slot's new content instead, like CharacterWindow_OnAttributeChanged
// already does for the stat tooltips.

namespace
{
	const std::string g_inventoryScript = std::string(MMO_UI_SCRIPT_DIR) + "/InventoryButton.lua";
	const std::string g_actionBarScript = std::string(MMO_UI_SCRIPT_DIR) + "/ActionBar.lua";

	/// Records every call the scripts make into the tooltip, so the test can read back what the
	/// tooltip was filled from and in which order.
	const char* g_recorder = R"lua(
		calls = {};

		function Record(name, arg)
			calls[#calls + 1] = { name = name, arg = arg };
		end

		function Collect(name)
			local result = "";
			for _, call in ipairs(calls) do
				if call.name == name then
					result = result .. tostring(call.arg) .. ";";
				end
			end
			return result;
		end

		AnchorPoint = { NONE = 0, TOP = 1, RIGHT = 2, BOTTOM = 3, LEFT = 4, H_CENTER = 5, V_CENTER = 6 };

		local tooltipParent = { GetHeight = function(self) return 1000; end };

		GameTooltip = {};
		function GameTooltip:Show() Record("Show", "tooltip"); end
		function GameTooltip:Hide() Record("Hide", "tooltip"); end
		function GameTooltip:ClearAnchors() end
		function GameTooltip:SetAnchor() end
		function GameTooltip:GetHeight() return 120; end
		function GameTooltip:GetParent() return tooltipParent; end

		function Localize(key) return key; end
		function GetUIScale() return { x = 1.0, y = 1.0 }; end
	)lua";

	/// Stand-ins for the globals the inventory button touches. The slot's content is the
	/// `slotItem` global, so the test can replace it underneath the hovered button.
	const char* g_inventoryStubs = R"lua(
		ItemQualityColors = { [1] = "FFFFFFFF" };

		slotItem = nil;
		hovered = true;

		function MakeItem(name)
			local item = { name = name };
			function item:IsSameItem(other) return other ~= nil and other.name == self.name; end
			function item:IsUsable() return true; end
			function item:GetIcon() return "Icon/" .. name; end
			function item:GetStackCount() return 1; end
			function item:GetQuality() return 1; end
			return item;
		end

		function GetInventorySlotItem(unit, slot) return slotItem; end

		function GameTooltip_SetItem(item) Record("SetItem", item.name); end
		function GameTooltip_AddLine() end

		button = { id = 42 };
		function button:IsHovered() return hovered; end
		function button:GetX() return 100; end
		function button:GetRect() return { top = 400, bottom = 500 }; end
		function button:SetText() end
		function button:SetProperty() end
		function button:GetProperty(name) return "false"; end
	)lua";

	/// Stand-ins for the globals the action bar touches. Slot 0 holds the `slotSpell` global.
	const char* g_actionBarStubs = R"lua(
		slotSpell = nil;
		hovered = true;

		function MakeSpell(id, name)
			return { id = id, name = name, icon = "Icon/" .. name, cost = 0, powertype = 0 };
		end

		function IsActionButtonSpell(slot) return slot == 0 and slotSpell ~= nil; end
		function GetActionButtonSpell(slot) if slot == 0 then return slotSpell; end return nil; end
		function IsActionButtonItem(slot) return false; end
		function GetActionButtonItem(slot) return nil; end
		function IsActionButtonEmote(slot) return false; end
		function GetActionButtonEmote(slot) return nil; end
		function IsActionButtonUsable(slot) return true; end
		function IsActionButtonActive(slot) return false; end

		function GameTooltip_SetSpell(spell) Record("SetSpell", spell.name); end

		ActionButton1 = { id = 1 };
		function ActionButton1:IsHovered() return hovered; end
		function ActionButton1:SetOpacity() end
		function ActionButton1:SetProperty() end
		function ActionButton1:SetText() end
		function ActionButton1:GetChild(index) return nil; end
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

	/// Opens a lua state with the recorder, the given stubs and the given shipped script loaded.
	lua_State* OpenState(const char* stubs, const std::string& script)
	{
		lua_State* state = luaL_newstate();
		REQUIRE(state != nullptr);
		luaL_openlibs(state);

		RunChunk(state, g_recorder);
		RunChunk(state, stubs);
		RunFile(state, script);
		return state;
	}
}

TEST_CASE("TooltipRefresh_Scripts_Are_Available", "[tooltip_refresh]")
{
	// Names the cause up front: without the data/client submodule the other cases can only
	// report "cannot open file", which looks like a broken test rather than a missing checkout.
	REQUIRE(std::filesystem::exists(g_inventoryScript));
	REQUIRE(std::filesystem::exists(g_actionBarScript));
}

TEST_CASE("InventoryButton_Tooltip_Follows_Slot_Content_While_Hovered", "[tooltip_refresh]")
{
	lua_State* state = OpenState(g_inventoryStubs, g_inventoryScript);

	// The cursor comes to rest on a slot holding the item that is about to be equipped.
	RunChunk(state, "slotItem = MakeItem(\"Old\"); InventoryItemButton_OnEnter(button);");
	REQUIRE(Collect(state, "SetItem") == "Old;");

	// Equipping swaps the slot's content without the mouse moving, so the next frame update must
	// refill the tooltip from the item that is in the slot now.
	RunChunk(state, "slotItem = MakeItem(\"New\"); InventoryItemButton_OnUpdate(button, 0.016);");
	CHECK(Collect(state, "SetItem") == "Old;New;");

	// An unchanged slot must not rebuild the tooltip on every frame.
	RunChunk(state, "InventoryItemButton_OnUpdate(button, 0.016); InventoryItemButton_OnUpdate(button, 0.016);");
	CHECK(Collect(state, "SetItem") == "Old;New;");

	lua_close(state);
}

TEST_CASE("InventoryButton_Tooltip_Hides_When_Hovered_Slot_Empties", "[tooltip_refresh]")
{
	lua_State* state = OpenState(g_inventoryStubs, g_inventoryScript);

	RunChunk(state, "slotItem = MakeItem(\"Old\"); InventoryItemButton_OnEnter(button);");
	REQUIRE(Collect(state, "Hide") == "");

	// Equipping the slot's last item leaves it empty: the tooltip has nothing left to show and
	// must go away instead of lingering with the previous item.
	RunChunk(state, "slotItem = nil; InventoryItemButton_OnUpdate(button, 0.016);");
	CHECK(Collect(state, "Hide") == "tooltip;");

	lua_close(state);
}

TEST_CASE("InventoryButton_Tooltip_Is_Not_Refreshed_Without_Hover", "[tooltip_refresh]")
{
	lua_State* state = OpenState(g_inventoryStubs, g_inventoryScript);

	// A slot the cursor is not on must never touch the tooltip, whatever happens to its item.
	RunChunk(state, "hovered = false; slotItem = MakeItem(\"Old\"); InventoryItemButton_OnUpdate(button, 0.016);");
	RunChunk(state, "slotItem = MakeItem(\"New\"); InventoryItemButton_OnUpdate(button, 0.016);");

	CHECK(Collect(state, "SetItem") == "");
	CHECK(Collect(state, "Hide") == "");

	lua_close(state);
}

TEST_CASE("ActionButton_Tooltip_Follows_Slot_Content_While_Hovered", "[tooltip_refresh]")
{
	lua_State* state = OpenState(g_actionBarStubs, g_actionBarScript);

	// The cursor rests on an action button which already holds a spell.
	RunChunk(state, "slotSpell = MakeSpell(10, \"Old\"); ActionButton_OnEnter(ActionButton1);");
	REQUIRE(Collect(state, "SetSpell") == "Old;");

	// Dropping another spell onto the button raises ACTION_BAR_CHANGED, which runs
	// ActionBar_UpdateButtons. The tooltip under the unmoved cursor must follow.
	RunChunk(state, "slotSpell = MakeSpell(20, \"New\"); ActionBar_UpdateButtons(ActionBar);");
	CHECK(Collect(state, "SetSpell") == "Old;New;");

	lua_close(state);
}

TEST_CASE("ActionButton_Tooltip_Is_Not_Refreshed_Without_Hover", "[tooltip_refresh]")
{
	lua_State* state = OpenState(g_actionBarStubs, g_actionBarScript);

	RunChunk(state, "hovered = false; slotSpell = MakeSpell(10, \"Old\"); ActionBar_UpdateButtons(ActionBar);");

	CHECK(Collect(state, "SetSpell") == "");

	lua_close(state);
}
