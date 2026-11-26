// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <filesystem>
#include <string>

#include "lua/lua.hpp"

namespace
{
	/// The shipped UI scripts under test.
	const std::string g_chatScriptPath = std::string(MMO_UI_SCRIPT_DIR) + "/ChatFrame.lua";
	const std::string g_tooltipScriptPath = std::string(MMO_UI_SCRIPT_DIR) + "/GameTooltip.lua";
	const std::string g_lootRollScriptPath = std::string(MMO_UI_SCRIPT_DIR) + "/LootRollFrame.lua";

	/// Stand-ins for the globals ChatFrame_OnLoad touches, plus a recording item link tooltip.
	const char* g_chatStubs = R"lua(
		handlers = {};
		shownItems = "";

		eventFrame = {};
		function eventFrame:RegisterEvent(name, handler) handlers[name] = handler; end

		function Localize(key) return key; end

		function ItemRefTooltip_SetItem(itemId)
			shownItems = shownItems .. tostring(itemId) .. ";";
		end

		-- Dispatches a hyperlink click the way the frame does and returns the item ids the item
		-- link tooltip was asked to show so far.
		function ClickLink(linkType, payload)
			local handler = handlers["HYPERLINK_CLICKED"];
			if (handler == nil) then
				return "no handler";
			end

			handler(eventFrame, linkType, payload);
			return shownItems;
		end
	)lua";

	/// Stand-ins for what the loot roll window needs to announce a roll in the chat: a recording
	/// chat frame and silent roll item frames, so only the chat text is left to look at.
	const char* g_lootRollStubs = R"lua(
		chatLines = "";

		ChatFrame = {};
		function ChatFrame:AddMessage(message, r, g, b)
			chatLines = chatLines .. message .. ";";
		end

		ItemQualityColors = { [1] = "FFFFFFFF" };

		local function NewRollFrame()
			local frame = {};
			function frame:Show() end
			function frame:Hide() end
			function frame:SetText(text) end
			function frame:SetProperty(name, value) end
			function frame:GetChild(index) return NewRollFrame(); end
			function frame:SetOnEnterHandler(handler) end
			function frame:SetOnLeaveHandler(handler) end
			function frame:SetClickedHandler(handler) end
			return frame;
		end

		local rollItemFrames = {};
		function getglobal(name)
			if (rollItemFrames[name] == nil) then
				rollItemFrames[name] = NewRollFrame();
			end
			return rollItemFrames[name];
		end

		function GetItemDisplayIcon(displayId) return ""; end

		rollFrame = NewRollFrame();
	)lua";

	/// Frame stand-ins for the item link tooltip, and an item cache holding two item templates.
	/// Showing and hiding is recorded by frame name so the test can tell which window reacted.
	const char* g_tooltipStubs = R"lua(
		lines = {};
		shown = "";

		local function NewFrame(name, record)
			local frame = { name = name, visible = false, record = record };
			function frame:SetHeight(height) end
			function frame:SetWidth(width) end
			function frame:GetWidth() return 540; end
			function frame:SetOpacity(opacity) end
			function frame:GetOpacity(inherited) return 1.0; end
			function frame:Show()
				self.visible = true;
				if (self.record) then shown = shown .. self.name .. ":show;"; end
			end
			function frame:Hide()
				self.visible = false;
				if (self.record) then shown = shown .. self.name .. ":hide;"; end
			end
			function frame:IsVisible() return self.visible; end
			function frame:RemoveAllChildren() lines = {}; end
			function frame:SetClickedHandler(handler) self.clickedHandler = handler; end
			return frame;
		end

		GameTooltip = NewFrame("GameTooltip", true);
		ItemRefTooltip = NewFrame("ItemRefTooltip", true);
		ItemRefTooltipLines = NewFrame("ItemRefTooltipLines");
		ItemRefTooltipMoneyFrame = NewFrame("ItemRefTooltipMoneyFrame");
		ItemRefTooltipMoneyText = NewFrame("ItemRefTooltipMoneyText");
		ItemRefTooltipCloseButton = NewFrame("ItemRefTooltipCloseButton");

		ItemQualityColors = { [1] = "FFFFFFFF" };
		INVENTORY_TYPE_NONEQUIP = "NON_EQUIP";
		INVENTORY_TYPE_BAG = "BAG";

		gameData = { spells = { GetById = function(self, id) return nil; end } };

		function Localize(key) return key; end
		function GetUnit(name) return nil; end

		local function NewItem(name)
			local item = {
				name = name, quality = 1, bonding = 0, inventoryType = "NON_EQUIP",
				minDamage = 0, maxDamage = 0, dps = 0, attackSpeed = 0, bagSlots = 0,
				armor = 0, block = 0, proficiency = 0, requiredLevel = 0, startQuestId = 0,
				description = "", maxDurability = 0,
			};
			function item:GetSpellId(index) return 0; end
			return item;
		end

		local items = { [137] = NewItem("Mangy Fur Clump"), [139] = NewItem("Wolf Meat") };

		cachedItemQueries = "";
		function GetCachedItemInfo(itemId)
			cachedItemQueries = cachedItemQueries .. tostring(itemId) .. ";";
			return items[itemId];
		end
	)lua";

	/// Replaces the line sinks with recording ones so the test does not need the real tooltip
	/// line frames, and exposes the tooltip content as a single string.
	const char* g_tooltipRecorder = R"lua(
		function GameTooltip_AddLine(line, alignment, color, role) lines[#lines + 1] = tostring(line); end
		function GameTooltip_AddDualLine(left, right) lines[#lines + 1] = tostring(left); end
		function GameTooltip_AddIconLine(line, icon, color) lines[#lines + 1] = tostring(line); end
		function GameTooltip_AddItemStatLines(item) end

		function TooltipLines()
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

	/// Calls a global Lua function without arguments and returns its string result, or
	/// "error: <message>" when the call failed, so a failing test names the cause.
	std::string CallString(lua_State* state, const char* functionName)
	{
		lua_getglobal(state, functionName);
		if (lua_pcall(state, 0, 1, 0) != LUA_OK)
		{
			const std::string error = std::string("error: ") + lua_tostring(state, -1);
			lua_pop(state, 1);
			return error;
		}

		const std::string result = lua_tostring(state, -1) ? lua_tostring(state, -1) : "";
		lua_pop(state, 1);
		return result;
	}

	/// Runs a chunk which returns one string value.
	std::string EvalString(lua_State* state, const char* chunk)
	{
		if (luaL_loadstring(state, chunk) != LUA_OK || lua_pcall(state, 0, 1, 0) != LUA_OK)
		{
			const std::string error = std::string("error: ") + lua_tostring(state, -1);
			lua_pop(state, 1);
			return error;
		}

		const std::string result = lua_tostring(state, -1) ? lua_tostring(state, -1) : "";
		lua_pop(state, 1);
		return result;
	}
}

TEST_CASE("ItemLink_Scripts_Are_Available", "[item_link]")
{
	// Names the cause up front: without the data/client submodule the other cases can only
	// report "cannot open file", which looks like a broken test rather than a missing checkout.
	REQUIRE(std::filesystem::exists(g_chatScriptPath));
	REQUIRE(std::filesystem::exists(g_tooltipScriptPath));
}

TEST_CASE("ChatFrame_ItemLinkClick_Shows_Item_Link_Tooltip", "[item_link]")
{
	lua_State* state = luaL_newstate();
	REQUIRE(state != nullptr);
	luaL_openlibs(state);

	RunChunk(state, g_chatStubs);
	RunFile(state, g_chatScriptPath);

	// Replaces the header update of the real script, which needs the chat input frames.
	RunChunk(state, "function ChatEdit_UpdateHeader() end");
	RunChunk(state, "ChatFrame_OnLoad(eventFrame);");

	// Clicking an item link opens the item tooltip of that item ...
	CHECK(EvalString(state, "return ClickLink(\"item\", \"139\");") == "139;");

	// ... and clicking the next one updates the very same window instead of opening a second.
	CHECK(EvalString(state, "return ClickLink(\"item\", \"137\");") == "139;137;");

	lua_close(state);
}

TEST_CASE("ChatFrame_Loot_Line_Names_A_Clickable_Item", "[item_link]")
{
	lua_State* state = luaL_newstate();
	REQUIRE(state != nullptr);
	luaL_openlibs(state);

	RunChunk(state, g_chatStubs);
	RunChunk(state, "ItemQualityColors = { [1] = \"FFFFFFFF\" };");
	RunFile(state, g_chatScriptPath);

	// The item name a loot line shows is a link to the item, not plain colored text.
	CHECK(EvalString(state, "return ChatFrame_GetItemNameLinkText(\"Wolf Meat\", 139, 1);")
		== "|cFFFFFFFF|Hitem:139|h[Wolf Meat]|h|r");

	// An item without an id has nothing to link to and stays as it was.
	CHECK(EvalString(state, "return ChatFrame_GetItemNameLinkText(\"Wolf Meat\", 0, 1);")
		== "|cFFFFFFFF[Wolf Meat]|r");

	lua_close(state);
}

TEST_CASE("ItemLinkTooltip_Is_A_Single_Window_Beside_The_Hover_Tooltip", "[item_link]")
{
	lua_State* state = luaL_newstate();
	REQUIRE(state != nullptr);
	luaL_openlibs(state);

	RunChunk(state, g_tooltipStubs);
	RunFile(state, g_tooltipScriptPath);
	RunChunk(state, g_tooltipRecorder);
	RunChunk(state, "ItemRefTooltip_OnLoad(ItemRefTooltip);");

	RunChunk(state, "ItemRefTooltip_SetItem(139);");
	CHECK(CallString(state, "TooltipLines") == "Wolf Meat");
	CHECK(EvalString(state, "return tostring(ItemRefTooltip:IsVisible());") == "true");

	// The hover tooltip is a window of its own and must not be taken over by a link click.
	CHECK(EvalString(state, "return tostring(GameTooltip:IsVisible());") == "false");

	// A second link click refills the one window, it does not open another one.
	RunChunk(state, "ItemRefTooltip_SetItem(137);");
	CHECK(CallString(state, "TooltipLines") == "Mangy Fur Clump");
	CHECK(EvalString(state, "return shown;") == "ItemRefTooltip:show;ItemRefTooltip:show;");

	// The window is closable, and its close button is what closes it.
	RunChunk(state, "ItemRefTooltipCloseButton.clickedHandler();");
	CHECK(EvalString(state, "return tostring(ItemRefTooltip:IsVisible());") == "false");

	lua_close(state);
}

TEST_CASE("LootRollFrame_Chat_Lines_Name_Clickable_Items", "[item_link]")
{
	lua_State* state = luaL_newstate();
	REQUIRE(state != nullptr);
	luaL_openlibs(state);

	RunChunk(state, g_chatStubs);
	RunChunk(state, g_lootRollStubs);
	RunFile(state, g_chatScriptPath);
	RunFile(state, g_lootRollScriptPath);

	// A roll the player can see in the loot roll window ...
	RunChunk(state, "LootRollFrame_OnStartRoll(rollFrame, 7, 139, 60000, \"Wolf Meat\", 1, 0);");

	// ... names a clickable item when somebody rolls on it ...
	RunChunk(state, "LootRollFrame_OnRollResult(rollFrame, 1, 42, \"Kyo\", \"Wolf Meat\", 1, 139);");
	CHECK(EvalString(state, "return chatLines;")
		== "Kyo rolled Need - 42 on |cFFFFFFFF|Hitem:139|h[Wolf Meat]|h|r;");

	// ... and when it is won.
	RunChunk(state, "chatLines = \"\";");
	RunChunk(state, "LootRollFrame_OnRollWon(rollFrame, 7, 139, 42, 1, \"Wolf Meat\", 1, \"Kyo\");");
	CHECK(EvalString(state, "return chatLines;")
		== "Kyo won |cFFFFFFFF|Hitem:139|h[Wolf Meat]|h|r (Need - 42);");

	// The line for a roll nobody wanted names the same clickable item.
	RunChunk(state, "chatLines = \"\";");
	RunChunk(state, "LootRollFrame_OnStartRoll(rollFrame, 8, 137, 60000, \"Mangy Fur Clump\", 1, 0);");
	RunChunk(state, "LootRollFrame_OnAllPassed(rollFrame, 8, 137);");
	CHECK(EvalString(state, "return chatLines;")
		== "Everyone passed on |cFFFFFFFF|Hitem:137|h[Mangy Fur Clump]|h|r;");

	lua_close(state);
}
