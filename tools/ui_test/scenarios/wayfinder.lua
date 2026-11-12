-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
-- Populated Social and HUD preview for the Wayfinder theme.
dofile(UiTest.scenarioDir .. "bluegold_fixture.lua")

UiTest.data.units.target = UiTest.MakeUnit({
	name = "Kingsroad Brigand", level = 11, health = 640, maxHealth = 900,
	hostile = true, friendly = false,
});
UiTest.FireEvent("PLAYER_TARGET_CHANGED");
UiTest.data.units.player.power[0] = 660;
UiTest.FireEvent("PLAYER_POWER_CHANGED");
SetCVar("ShowActionBar2", "1");

local getSpell = GetActionButtonSpell;
local quickbar = {
	[13] = { "S_Class_Cleric", "Guard" },
	[14] = { "T_Icon_Fire_04", "Kindle" },
	[15] = { "S_Class_Scout", "Trailcraft" },
	[19] = { "S_Class_Mage", "Arcane Focus" },
};
function IsActionButtonSpell(index)
	return getSpell(index) ~= nil or quickbar[index + 1] ~= nil;
end
function GetActionButtonSpell(index)
	local spell = quickbar[index + 1];
	if spell then
		return { id = index + 1, name = spell[2], icon = "Interface/Icons/Spells/" .. spell[1] .. ".htex" };
	end
	return getSpell(index);
end
ActionBar_UpdateSecondBarVisibility();
ActionBar_UpdateButtons(ActionBar);
ActionBar_UpdateButtons(ActionBar2);
ActionBar_UpdateShortcutLabels(1);

local quests = {
	{ id = 101, status = 3, quest = { title = "Wolves at the Watch" } },
	{ id = 102, status = 3, quest = { title = "Good Cloth" } },
	{ id = 103, status = 1, quest = { title = "Thinning the Roadside Packs" } },
};
local objectives = {
	[101] = { "Duskfang stalkers slain: 2/6", "Report to the Briarwatch sentry: 1/1" },
	[102] = { "Linen cloth: 4/6" },
};
local selected = 0;
function GetNumQuestLogEntries() return #quests; end
function GetQuestLogEntry(index) return quests[index + 1]; end
function QuestLogSelectQuest(id) selected = id; end
function GetQuestLogSelection() return selected; end
function GetQuestObjectiveCount() return #(objectives[selected] or {}); end
function GetQuestObjectiveText(index) return objectives[selected][index + 1]; end
function GetQuestLogTimeLeft() return 0; end
function IsQuestAllowedForClass() return true; end
function IsClassUnlockQuest() return false; end
UiTest.FireEvent("QUEST_LOG_UPDATE");

ShowUIPanel(FriendsFrame);
FriendsList_SelectFriend(FriendListContent:GetChild(0));
ChatFrame:AddMessage("[General] Taren: Brigands again at the abbey?", 0.92, 0.84, 0.68);
ChatFrame:AddMessage("[General] Elowen: Aye. Fourth time this moon.", 0.92, 0.84, 0.68);
FriendsFrame_WhisperClicked();
