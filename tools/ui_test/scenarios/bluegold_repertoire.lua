-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
-- Dense spell/emote lists, ranks, pagination and all three repertoire categories.
dofile(UiTest.scenarioDir .. "bluegold_fixture.lua")

local entries = {}
local icons = { "S_Fireball", "T_Icon_Fire_02", "S_FireNova", "T_Icon_Fire_01", "T_Icon_Fire_03" }
local groups = {
	{ "Attack", "Block", "Dodge", "Grave Strike", "Heal", "Dual Wield", "Warrior", "Acolyte", "Scout", "Versatility", "Used to Hard Work", "Battle Training", "Arcane Training" },
	{ "Fireball", "Evasive Step", "Flame Strike", "Battle Shout", "Shield Bash" },
	{ "Two-Handed Swords", "First Aid", "Elemental Magic" }
}
for category, names in ipairs(groups) do
	for index, name in ipairs(names) do
		local spell = { id = #entries + 1, name = name, rank = index % 3 == 0 and 2 or 0,
			icon = "Interface/Icons/Spells/" .. icons[(index - 1) % #icons + 1] .. ".htex", cost = 0, powertype = 0, level = 0 }
		function spell:IsAbility() return category == 2 end
		function spell:IsSkill() return category == 3 end
		function spell:GetCreatedItem() return 0 end
		function spell:GetNumReagents() return 0 end
		function spell:GetRequiredSkill() return 0, 0, "" end
		table.insert(entries, spell)
	end
end
function GetSpell(index) return entries[index + 1] end
function IsPassiveSpell() return false end
function IsChanneledSpell() return false end
function IsRacialSpell() return false end
function GetSpellEffectiveCost() return 0 end
function GetSpellEffectiveCastTime() return 0 end
function GetSpellEffectiveCooldown() return 0 end
function GetSpellDescription() return "A repertoire preview ability." end
function PickupSpell(id) UiTest.pickedSpell = id end

local emotes = { "Wave", "Bow", "Cheer", "Dance", "Salute", "Laugh" }
function GetNumEmotes() return #emotes end
function GetEmoteInfo(index)
	local name = emotes[index]
	return name and { id = index, name = name, command = name:lower(), icon = "Interface/Icons/fg4_iconsFlat_dialogue.htex" } or nil
end
function PickupEmote(id) UiTest.pickedEmote = id end
EmoteList_UpdatePage()
SpellBook_SelectTab(SPELLBOOK_TAB_GENERAL)
SpellBook_UpdatePage()
ShowUIPanel(SpellBook)
