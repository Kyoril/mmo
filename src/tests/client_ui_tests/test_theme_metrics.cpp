// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "lua/lua.hpp"

#include <string>

namespace
{
	void RunThemeChunk(lua_State* state, const char* source)
	{
		if (luaL_dostring(state, source) != LUA_OK)
		{
			FAIL(lua_tostring(state, -1));
		}
	}

	void LoadThemeScript(lua_State* state, const std::string& file)
	{
		const std::string path = std::string(MMO_UI_SCRIPT_DIR) + "/" + file;
		if (luaL_dofile(state, path.c_str()) != LUA_OK)
		{
			FAIL(lua_tostring(state, -1));
		}
	}

	double ThemeNumber(lua_State* state, const char* expression)
	{
		const std::string source = std::string("return ") + expression;
		const int result = luaL_dostring(state, source.c_str());
		REQUIRE(result == LUA_OK);
		const double value = lua_tonumber(state, -1);
		lua_pop(state, 1);
		return value;
	}

	std::string ThemeText(lua_State* state, const char* global)
	{
		lua_getglobal(state, global);
		REQUIRE(lua_isstring(state, -1));
		const std::string result = lua_tostring(state, -1);
		lua_pop(state, 1);
		return result;
	}
}

TEST_CASE("Theme metrics preserve legacy defaults and item quality semantics", "[theme_metrics]")
{
	const std::string theme = GENERATE("", "Legacy", "BlueGold");
	INFO("Theme: " << (theme.empty() ? "missing metrics fallback" : theme));
	lua_State* state = luaL_newstate();
	REQUIRE(state != nullptr);
	luaL_openlibs(state);
	if (!theme.empty())
	{
		LoadThemeScript(state, "Themes/" + theme + "/Metrics.lua");
	}
	LoadThemeScript(state, "ActionBar.lua");
	LoadThemeScript(state, "InventoryButton.lua");
	CHECK(ThemeText(state, "ACTION_SOCKET_NORMAL_TINT") == (theme == "BlueGold" ? "FFBEB7AC" : "FF998D7C"));
	CHECK(ThemeText(state, "ACTION_EMPTY_SOCKET_TINT") == (theme == "BlueGold" ? "304C607C" : "4D998D7C"));
	RunThemeChunk(state, R"lua(
		ItemQualityColors = { [3] = "FF0070DD" };
		slot = { properties = { QualityBorder = "true" } };
		function slot:GetProperty(name) return self.properties[name]; end
		function slot:SetProperty(name, value) self.properties[name] = value; end
		InventoryItemButton_UpdateQualityBorder(slot, { GetQuality = function() return 3; end });
		itemColor = slot.properties.QualityColor;
		itemBackground = slot.properties.BackgroundColor;
		InventoryItemButton_UpdateQualityBorder(slot, nil);
		emptyColor = slot.properties.QualityColor;
		emptyBackground = slot.properties.BackgroundColor;
	)lua");
	CHECK(ThemeText(state, "itemColor") == "FF0070DD");
	CHECK(ThemeText(state, "itemBackground") == "00FFFFFF");
	CHECK(ThemeText(state, "emptyColor") == (theme == "BlueGold" ? "FF8793A3" : "FF505050"));
	CHECK(ThemeText(state, "emptyBackground") == "FF909090");
	lua_close(state);
}

TEST_CASE("Experience dock reserves the active theme height for every visible bar", "[theme_metrics]")
{
	const std::string theme = GENERATE("", "Legacy", "BlueGold");
	const bool characterVisible = GENERATE(false, true);
	const bool classVisible = GENERATE(false, true);
	INFO("Theme: " << theme << ", character: " << characterVisible << ", class: " << classVisible);
	lua_State* state = luaL_newstate();
	REQUIRE(state != nullptr);
	luaL_openlibs(state);
	if (!theme.empty())
	{
		LoadThemeScript(state, "Themes/" + theme + "/Metrics.lua");
	}
	RunThemeChunk(state, R"lua(
		function MakeFrame()
			local frame = { visible = false };
			function frame:SetHeight(value) self.height = value; end
			function frame:SetWidth(value) self.width = value; end
			function frame:IsVisible() return self.visible; end
			function frame:Show() self.visible = true; end
			function frame:Hide() self.visible = false; end
			function frame:SetAnchor(point, relativePoint, parent, offset)
				self.anchors = self.anchors or {};
				self.anchors[point] = offset;
				self.relativePoints = self.relativePoints or {};
				self.relativePoints[point] = relativePoint;
			end
			return frame;
		end
		AnchorPoint = { BOTTOM = 1, TOP = 2, LEFT = 3, RIGHT = 4 };
		PlayerExperienceBar = MakeFrame(); PlayerClassExperienceBar = MakeFrame();
		GameMenuBar = MakeFrame(); ExperienceDock = MakeFrame();
		AlestiaOakLeft = MakeFrame(); AlestiaOakRight = MakeFrame();
		layoutUpdates = 0;
		function ActionBar_UpdateSecondBarLayout() layoutUpdates = layoutUpdates + 1; end
	)lua");
	lua_pushboolean(state, characterVisible);
	lua_setglobal(state, "characterVisible");
	lua_pushboolean(state, classVisible);
	lua_setglobal(state, "classVisible");
	LoadThemeScript(state, "GameMenuBar.lua");
	RunThemeChunk(state, R"lua(
		PlayerExperienceBar.visible = characterVisible;
		PlayerClassExperienceBar.visible = classVisible;
		GameMenuBar_UpdateExperienceBarLayout();
	)lua");
	const int height = theme == "BlueGold" ? 30 : 44;
	const int count = static_cast<int>(characterVisible) + static_cast<int>(classVisible);
	CHECK(ThemeNumber(state, "PlayerExperienceBar.height") == height);
	CHECK(ThemeNumber(state, "PlayerClassExperienceBar.height") == height);
	CHECK(ThemeNumber(state, "ExperienceDock.height") == count * (height + 4));
	CHECK(ThemeNumber(state, "ExperienceDock.visible and 1 or 0") == (count > 0 ? 1 : 0));
	CHECK(ThemeNumber(state, "PlayerClassExperienceBar.anchors[AnchorPoint.BOTTOM]") == (characterVisible ? -height - 4 : 0));
	CHECK(ThemeNumber(state, "ExperienceDock.anchors[AnchorPoint.BOTTOM]") == (theme == "BlueGold" ? -148 : 8));
	CHECK(ThemeNumber(state, "ExperienceDock.anchors[AnchorPoint.LEFT]") == (theme == "BlueGold" ? -8 : -56));
	CHECK(ThemeNumber(state, "ExperienceDock.anchors[AnchorPoint.RIGHT]") == (theme == "BlueGold" ? 8 : 56));
	CHECK(ThemeNumber(state, "ExperienceDock.relativePoints[AnchorPoint.BOTTOM]") == (theme == "BlueGold" ? 1 : 2));
	CHECK(ThemeNumber(state, "GameMenuBar.height") == (theme == "BlueGold" ? 176 + count * (height + 4) : 156));
	CHECK(ThemeNumber(state, "layoutUpdates") == 1);
	lua_close(state);
}
