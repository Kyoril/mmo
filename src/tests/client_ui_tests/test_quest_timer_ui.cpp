// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <filesystem>
#include <string>

#include "lua/lua.hpp"

// A timed quest without objectives is complete (quest_status::Complete) from the moment it is
// accepted, yet its countdown keeps running on the server and still fails the quest when it
// expires (GamePlayerS::ArmQuestTimer / InitializeQuestTimers). The quest tracker must therefore
// keep showing the remaining time while the quest is complete but not turned in.

namespace
{
	/// The shipped UI script under test.
	const std::string g_scriptPath = std::string(MMO_UI_SCRIPT_DIR) + "/QuestTracker.lua";

	/// Stand-ins for the frames and the client functions the quest tracker uses. Frames are
	/// created on first use from their global name, like the XML layout creates them, and record
	/// what the script does to them. The quest log is driven by the `quests` table.
	const char* g_stubs = R"lua(
		local Frame = {};
		Frame.__index = Frame;

		function NewFrame(name)
			return setmetatable({ name = name, visible = false, text = "", props = {}, children = {} }, Frame);
		end

		function Frame:Show() self.visible = true; end
		function Frame:Hide() self.visible = false; end
		function Frame:IsVisible() return self.visible; end
		function Frame:IsHovered() return false; end
		function Frame:SetText(text) self.text = text; end
		function Frame:GetText() return self.text; end
		function Frame:SetProperty(name, value) self.props[name] = value; end
		function Frame:SetWidth(width) self.width = width; end
		function Frame:GetWidth() return self.width or 400; end
		function Frame:SetHeight(height) self.height = height; end
		function Frame:GetHeight() return self.height or 0; end
		function Frame:GetTextHeight() return 20; end
		function Frame:GetChildCount() return 0; end
		function Frame:ClearAnchors() end
		function Frame:SetAnchor() end
		function Frame:RegisterEvent() end

		-- Every tracker frame of the XML layout comes into being on first access.
		setmetatable(_G, { __index = function(globals, name)
			if type(name) ~= "string" or name:sub(1, 9) ~= "QuestTrac" then
				return nil;
			end
			local frame = NewFrame(name);
			rawset(globals, name, frame);
			return frame;
		end });

		QuestLogFrame = NewFrame("QuestLogFrame");

		AnchorPoint = { TOP = 0, BOTTOM = 1, LEFT = 2, RIGHT = 3, H_CENTER = 4, V_CENTER = 5 };

		function Localize(key) return "L:" .. key; end

		-- questId -> remaining seconds, 0 for quests without a deadline.
		timeLeft = {};
		quests = {};

		function MakeQuest(id, status)
			return { id = id, status = status, quest = { id = id, title = "Quest" .. id, requiredClasses = 0, flags = 0 } };
		end

		function GetNumQuestLogEntries() return #quests; end
		function GetQuestLogEntry(index) return quests[index + 1]; end
		function GetQuestLogSelection() return selection or 0; end
		function QuestLogSelectQuest(id) selection = id; end
		function GetQuestLogTimeLeft(id) return timeLeft[id] or 0; end
		function IsQuestAllowedForClass(quest) return true; end
		function IsClassUnlockQuest(quest) return false; end
		function GetQuestObjectiveCount() return 0; end
		function GetQuestObjectiveText(index) return ""; end

		-- The title of the first tracked quest after a refresh.
		function TrackedTitle() return QuestTrackerTitle1:GetText(); end
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

	/// Loads the stubs and the quest tracker, runs its OnLoad handler and then the test chunk,
	/// whose assert() calls decide the result. Returns "ok" or the first error.
	std::string RunQuestTracker(const char* test)
	{
		lua_State* state = luaL_newstate();
		REQUIRE(state != nullptr);
		luaL_openlibs(state);

		std::string result = RunChunk(state, g_stubs);

		if (result.empty())
		{
			if (luaL_loadfile(state, g_scriptPath.c_str()) != LUA_OK || lua_pcall(state, 0, 0, 0) != LUA_OK)
			{
				result = lua_tostring(state, -1);
			}
		}

		if (result.empty())
		{
			result = RunChunk(state, "QuestTracker_OnLoad(QuestTrackerFrame);");
		}

		if (result.empty())
		{
			result = RunChunk(state, test);
		}

		lua_close(state);
		return result.empty() ? "ok" : result;
	}
}

TEST_CASE("Quest tracker script is available", "[quests][quest_ui]")
{
	REQUIRE(std::filesystem::exists(g_scriptPath));
}

TEST_CASE("Quest tracker counts down a timed quest that has no objectives", "[quests][quest_ui]")
{
	CHECK(RunQuestTracker(R"lua(
		-- A quest without objectives is complete from the moment it is accepted, but its
		-- deadline still runs: the countdown has to be visible, or the quest fails unannounced.
		quests = { MakeQuest(81, 1) };
		timeLeft[81] = 240;
		QuestTracker_Refresh();
		assert(TrackedTitle() == "Quest81  (4:00)", "complete timed quest shows '" .. TrackedTitle() .. "'");
	)lua") == "ok");
}

TEST_CASE("Quest tracker counts down an incomplete timed quest", "[quests][quest_ui]")
{
	CHECK(RunQuestTracker(R"lua(
		quests = { MakeQuest(81, 3) };
		timeLeft[81] = 95;
		QuestTracker_Refresh();
		assert(TrackedTitle() == "Quest81  (1:35)", "incomplete timed quest shows '" .. TrackedTitle() .. "'");
	)lua") == "ok");
}

TEST_CASE("Quest tracker drops the countdown once the quest failed", "[quests][quest_ui]")
{
	CHECK(RunQuestTracker(R"lua(
		quests = { MakeQuest(81, 5) };
		timeLeft[81] = 0;
		QuestTracker_Refresh();
		assert(TrackedTitle() == "Quest81", "failed timed quest shows '" .. TrackedTitle() .. "'");
	)lua") == "ok");
}

TEST_CASE("Quest tracker leaves untimed quests alone", "[quests][quest_ui]")
{
	CHECK(RunQuestTracker(R"lua(
		quests = { MakeQuest(42, 1) };
		QuestTracker_Refresh();
		assert(TrackedTitle() == "Quest42", "untimed quest shows '" .. TrackedTitle() .. "'");
	)lua") == "ok");
}
