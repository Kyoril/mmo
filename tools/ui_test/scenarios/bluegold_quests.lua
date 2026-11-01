-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
-- Quest list states, long text, scrolling, objectives and every dialog panel.
dofile(UiTest.scenarioDir .. "bluegold_fixture.lua")
UiTest.questSelection = 101
local story = "You have carried yourself well enough to reach this point. But strength alone does not make a warrior.\n\nA true warrior knows when to strike, when to stand firm, and when to place themselves between danger and those who cannot hold the line themselves. Steel is easy to swing. Duty is harder to bear.\n\nThere is an old armsmaster not far from here. Find him at the training hall and tell him you are ready to learn."
local titles = { "The Warrior's Trial", "Deepwood Teeth", "Broken Ground", "Sparks of Talent", "A Shepherd's Mercy", "Thinning the Roadside Packs", "A Different Calling", "Before Nightfall" }
local quests = {}
for index, title in ipairs(titles) do
	local quest = { id = 100 + index, title = title, rewardMoney = 325, rewardXp = 450, rewardClassXp = 100,
		details = index == 1 and story or "The roads around the watch are no longer safe. Help the travelers reach the village, then return with news of your success.",
		objectives = "Return to the watch and speak with the captain." }
	quests[index] = { id = quest.id, quest = quest, status = (index == 1 or index == 6) and 1 or (index == 3 and 5 or 3) }
end
function GetNumQuestLogEntries() return #quests end
function GetQuestLogEntry(index) return quests[index + 1] end
function GetQuestLogSelection() return UiTest.questSelection end
function QuestLogSelectQuest(id) UiTest.questSelection = id end
function GetQuestDetailsText(quest) return quest.details end
function GetQuestObjectivesText(quest) return quest.objectives end
function GetQuestLogTimeLeft(id) return id == 108 and 180 or 0 end
function IsClassUnlockQuest(quest) return quest.id == 104 or quest.id == 105 end
function IsQuestAllowedForClass(quest) return quest.id ~= 107 end
function GetQuestObjectiveCount() return 2 end
function GetQuestObjectiveText(index) return index == 0 and "Travelers escorted: 2/3" or "Roadside supplies recovered: 4/4" end
function AbandonQuest(id) UiTest.abandonedQuest = id end

local items = {
	[901] = { name = "Watchman's Supplies", quality = 1, icon = "acolyte_robe_icon" },
	[902] = { name = "Deepwood Ward", quality = 3, icon = "cadet_shield_icon" },
	[903] = { name = "Scout's Boots", quality = 2, icon = "boots_of_the_templar_icon" }
}
local originalCachedItem = GetCachedItemInfo
function GetCachedItemInfo(id)
	local item = items[id]
	if not item then return originalCachedItem(id) end
	function item:GetIcon() return "Interface/Icons/Items/" .. self.icon .. ".htex" end
	return item
end
function GetItemDisplayIcon(id) return GetCachedItemInfo(id):GetIcon() end
function GetQuestInfoChoiceItemCount() return 2 end
function GetQuestInfoChoiceItem(quest, index) return { itemId = 902 + index, count = 1 } end
function GetQuestInfoRewardItemCount() return 1 end
function GetQuestInfoRewardItem() return { itemId = 901, count = 3 } end
function GetQuestInfoRewardSpell() return nil end
function GetQuestInfoRewardEmote() return nil end
function GetQuestRewardChoiceItemCount() return 2 end
function GetQuestRewardChoiceItem(index) return { itemId = 902 + index, displayId = 902 + index, count = 1 } end
function GetQuestRewardItemCount() return 1 end
function GetQuestRewardItem() return { itemId = 901, displayId = 901, count = 3 } end
function GetQuestRewardEmote() return nil end
function GetQuestReward(choice) UiTest.questRewardChoice = choice end
UiTest.questDialog = { id = 101, title = titles[1], details = story, objectives = "Meet the armsmaster at the training hall.",
	requestItems = "Have you recovered the supplies? The watch is counting on you.", offerReward = "You have done well. Choose the equipment that will serve you best.",
	rewardedMoney = 325, rewardedXp = 450, rewardedClassXp = 100 }
function GetQuestDetails() return UiTest.questDialog end
function GetGreetingText() return "The watch has work for willing hands. Take a look at the tasks below." end
function GetNumAvailableQuests() return 3 end
function GetAvailableQuest(index) return { id = 101 + index, title = titles[index + 1], icon = index == 0 and 7 or 6 } end
function GetNumGossipActions() return 2 end
function GetGossipAction(index) return { text = index == 0 and "Tell me about the watch." or "I would like to browse your wares." } end
function QueryQuestDetails(id) UiTest.queriedQuest = id end
function GossipAction(id) UiTest.gossipAction = id end
UiTest.data.units.target.name = "Captain Theobald"

QuestLog_Update()
ShowUIPanel(QuestLogFrame)
