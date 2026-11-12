-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
--
-- A summoned persistent pet (mocks/50_pet.lua): pet frame and pet action bar, with one ability
-- on cooldown. The pet unit is the player's mock unit.

dofile(UiTest.scenarioDir .. "world.lua")

local playerUnit = GetUnit
function GetUnit(unit)
	if unit == "pet" then
		return playerUnit("player")
	end
	return playerUnit(unit)
end

-- Fields and methods GameTooltip_SetSpell reads.
local function PetSpell(spell)
	spell.rank = 0
	spell.cost = 0
	spell.powertype = 0
	spell.level = 0
	function spell:GetCreatedItem() return 0 end
	function spell:GetNumReagents() return 0 end
	function spell:GetRequiredSkill() return 0, 0, "" end
	return spell
end

function GetSpellEffectiveCost() return 0 end
function GetSpellEffectiveCastTime() return 0 end
function GetSpellEffectiveCooldown() return 10000 end
function GetSpellDescription() return "" end
function IsPassiveSpell() return false end
function IsChanneledSpell() return false end
function IsRacialSpell() return false end

UiTest.data.pet = {
	persistent = true,
	strategy = 0,
	levelMode = 0,
	xp = 120,
	nextLevelXp = 400,
	spells = {
		PetSpell({ id = 1, name = "Bite", icon = "Interface/Icons/fg4_icons_daggers_result.htex", cooldownRemaining = 4000, cooldownDuration = 10000 }),
		PetSpell({ id = 2, name = "Growl", icon = "Interface/Icons/fg4_icons_shield_result.htex" }),
	},
}

UiTest.FireEvent("PET_CHANGED")
