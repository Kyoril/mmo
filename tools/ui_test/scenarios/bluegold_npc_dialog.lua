-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
-- NPC header, greeting card, quest choices and service actions.
dofile(UiTest.scenarioDir .. "bluegold_quests.lua")
UiTest.data.units.target.name = "Wayfarer Captain Aldric Vane"
UiTest.data.units.target.subtitle = "<Kingsroad Waypost>"
function GetGreetingText()
	return "The Kingsroad is no place for slow feet or dull blades, Kyoril. Make yourself useful."
end
function GetNumAvailableQuests() return 1 end
function GetAvailableQuest(index)
	return index == 0 and { id = 101, title = "Contraband Recovery", icon = 3 } or nil
end
function GetNumGossipActions() return 2 end
function GetGossipAction(index)
	return { text = index == 0 and "Tell me about the Kingsroad." or "Show me the supplies available at this waypost." }
end
HideUIPanel(QuestLogFrame)
QuestFrame_OnQuestGreeting(QuestFrame)
