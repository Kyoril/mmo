// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <filesystem>
#include <string>

#include "lua/lua.hpp"

namespace
{
	/// The shipped UI scripts under test, loaded in this order.
	const char* g_scripts[] =
	{
		"GameParent.lua",
		"QuestFrame.lua"
	};

	/// Stand-ins for the frames and the client functions the NPC dialog uses. The important
	/// detail this harness reproduces is that the client hands every event handler a brand new
	/// wrapper of the frame (FrameManager::TriggerLuaEvent converts the raw Frame* again for
	/// every call), so fields a script stores on `self` are gone by the next call. A script that
	/// caches a created frame there therefore creates it again on every interaction.
	const char* g_stubs = R"lua(
		local cloneCounter = 0;

		local Frame = {};
		Frame.__index = Frame;

		function NewFrame(name)
			return setmetatable({ name = name, visible = false, text = "", props = {},
				lazyChildren = {}, children = {}, enabled = true, height = 0, width = 0,
				value = 0, step = 1, minimum = 0, maximum = 0 }, Frame);
		end

		function Frame:Clone()
			cloneCounter = cloneCounter + 1;
			return NewFrame((self.name or "Frame") .. "_" .. cloneCounter);
		end

		function Frame:GetName() return self.name; end
		function Frame:Show() self.visible = true; end
		function Frame:Hide() self.visible = false; end
		function Frame:IsVisible() return self.visible; end
		function Frame:SetText(text) self.text = text; end
		function Frame:GetText() return self.text; end
		function Frame:SetProperty(key, value) self.props[key] = value; end
		function Frame:GetProperty(key) return self.props[key]; end
		function Frame:SetHeight(height) self.height = height; end
		function Frame:GetHeight() return self.height; end
		function Frame:SetWidth(width) self.width = width; end
		function Frame:GetWidth() return self.width; end
		function Frame:GetTextWidth() return self.text ~= "" and 40 or 0; end
		function Frame:GetTextHeight() return self.text ~= "" and 20 or 0; end
		function Frame:SetAnchor() end
		function Frame:ClearAnchor() end
		function Frame:ClearAnchors() end
		function Frame:SetEnabled(enabled) self.enabled = enabled; end
		function Frame:Enable() self.enabled = true; end
		function Frame:Disable() self.enabled = false; end
		function Frame:IsEnabled() return self.enabled; end
		function Frame:SetMinimum(value) self.minimum = value; end
		function Frame:SetMaximum(value) self.maximum = value; end
		function Frame:SetStep(value) self.step = value; end
		function Frame:GetStep() return self.step; end
		function Frame:SetValue(value) self.value = value; end
		function Frame:GetValue() return self.value; end
		function Frame:SetOnValueChangedHandler(handler) self.onValueChanged = handler; end
		function Frame:SetOnMouseWheelHandler(handler) self.onMouseWheel = handler; end
		function Frame:SetClickedHandler(handler) self.onClick = handler; end
		function Frame:GetRect() return { left = 0, top = 0, right = 500, bottom = 1000 }; end
		function Frame:GetParent() return self.parent; end

		-- Children added by a script are counted; the layout children an XML file declares come
		-- into being on first access, like GetChild() on a real frame.
		function Frame:AddChild(child)
			table.insert(self.children, child);
			child.parent = self;
		end
		function Frame:RemoveAllChildren() self.children = {}; end
		function Frame:GetChildCount() return #self.children; end
		function Frame:GetAddedChild(index) return self.children[index]; end
		function Frame:GetChild(index)
			if not self.lazyChildren[index] then
				self.lazyChildren[index] = NewFrame((self.name or "Frame") .. "_child" .. index);
			end
			return self.lazyChildren[index];
		end

		-- The wrapper an event handler receives: only the frame's own state reaches the frame,
		-- anything else the script assigns dies with the wrapper.
		function Wrap(frame)
			return setmetatable({}, {
				__index = frame,
				__newindex = function(wrapper, key, value) rawset(wrapper, key, value); end });
		end

		local frameNames = { "QuestFrame", "QuestFramePanels", "QuestFrameGreetingPanel",
			"GreetingText", "AvailableQuestList", "GossipActionList", "GreetingPanelScrollBar",
			"TextSmall", "VerticalScrollBar", "QuestMenuButtonTemplate",
			"ThemeNpcGreetingPanelTemplate", "ThemeNpcGreetingCardTemplate",
			"ThemeNpcGreetingChoiceTemplate" };
		for _, name in ipairs(frameNames) do
			_G[name] = NewFrame(name);
		end

		AnchorPoint = { NONE = 0, TOP = 1, RIGHT = 2, BOTTOM = 3, LEFT = 4, H_CENTER = 5, V_CENTER = 6 };

		-- The themed NPC dialog layout, as BlueGold/Metrics.lua configures it.
		GameUITheme = { RefinedNpcGreeting = true, NpcDialogNameFont = "InventoryTitleFont",
			NpcDialogSubtitleFont = "SmallLabelFont", NpcGreetingTextColor = "FFF0E6D2" };

		function Localize(key) return "L:" .. key; end
		function PlaySound() end
		function GetUIScale() return { x = 1, y = 1 }; end
		function GetGreetingText() return "Greetings, traveller."; end
		function GetNumAvailableQuests() return 0; end
		function GetAvailableQuest() return nil; end
		function GetNumGossipActions() return 0; end
		function GetGossipAction() return nil; end

		local target = {};
		function target:GetName() return "Innkeeper Allison"; end
		function target:GetSubtitle() return "Innkeeper"; end

		function GetUnit(name)
			if name == "target" then
				return target;
			end
			return nil;
		end

		-- Every NPC dialog the client opens fires QUEST_GREETING on the quest frame.
		function FireGreeting()
			QuestFrame_OnQuestGreeting(Wrap(QuestFrame));
		end
	)lua";

	/// Runs a chunk and returns an empty string, or the Lua error it raised.
	std::string RunChunk(lua_State* state, const char* chunk)
	{
		if (luaL_loadstring(state, chunk) != LUA_OK || lua_pcall(state, 0, 0, 0) != LUA_OK)
		{
			return lua_tostring(state, -1);
		}

		return {};
	}

	/// Loads the stubs and the shipped scripts, then runs the test chunk whose assert() calls
	/// decide the result. Returns "ok" or the first error.
	std::string RunNpcDialogUi(const char* test)
	{
		lua_State* state = luaL_newstate();
		REQUIRE(state != nullptr);
		luaL_openlibs(state);

		std::string result = RunChunk(state, g_stubs);
		for (const char* script : g_scripts)
		{
			if (!result.empty())
			{
				break;
			}

			const std::string path = std::string(MMO_UI_SCRIPT_DIR) + "/" + script;
			if (luaL_loadfile(state, path.c_str()) != LUA_OK || lua_pcall(state, 0, 0, 0) != LUA_OK)
			{
				result = lua_tostring(state, -1);
			}
		}

		if (result.empty())
		{
			// The panel stack is not part of this test; showing the frame is enough.
			result = RunChunk(state, "function ShowUIPanel(frame) frame:Show(); end");
		}

		if (result.empty())
		{
			result = RunChunk(state, test);
		}

		lua_close(state);
		return result.empty() ? "ok" : result;
	}
}

TEST_CASE("NPC dialog scripts are available", "[ui][npc_dialog]")
{
	for (const char* script : g_scripts)
	{
		REQUIRE(std::filesystem::exists(std::string(MMO_UI_SCRIPT_DIR) + "/" + script));
	}
}

TEST_CASE("NPC dialog reuses its greeting layout between interactions", "[ui][npc_dialog]")
{
	CHECK(RunNpcDialogUi(R"lua(
		FireGreeting();
		assert(QuestFrameGreetingPanel:GetChildCount() == 1,
			"the first greeting did not build the greeting layout");

		for i = 1, 4 do
			FireGreeting();
		end

		assert(QuestFrameGreetingPanel:GetChildCount() == 1,
			"every greeting stacked another greeting panel: " .. QuestFrameGreetingPanel:GetChildCount());
	)lua") == "ok");
}

TEST_CASE("NPC dialog reuses its title bar name label between interactions", "[ui][npc_dialog]")
{
	CHECK(RunNpcDialogUi(R"lua(
		local titleBar = QuestFrame:GetChild(0);

		for i = 1, 5 do
			FireGreeting();
		end

		assert(titleBar:GetChildCount() == 1,
			"the NPC name label was added more than once: " .. titleBar:GetChildCount());
		assert(titleBar:GetAddedChild(1):GetText() == "Innkeeper Allison",
			"the NPC name label does not carry the NPC name");
	)lua") == "ok");
}
