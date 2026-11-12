-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
-- Action bar costs (interface option on), cooldown countdowns and a damage trail on the player bar.
dofile(UiTest.scenarioDir .. "wayfinder.lua")

SetCVar("ActionBarShowCost", "1");

-- Slot -> spell: icon, name, cost, power type (0 mana, 1 rage, 2 energy), cooldown remaining/total.
local spells = {
	[1] = { "S_Class_Mage", "Frostbolt", 45, 0 },
	[2] = { "T_Icon_Fire_04", "Fireball", 120, 0, 12.4, 20.0 },
	[3] = { "S_Class_Warrior", "Heroic Strike", 20, 1 },
	[4] = { "S_Class_Warrior", "Shield Slam", 35, 1, 2.3, 6.0 },
	[5] = { "S_Class_Scout", "Backstab", 35, 2 },
	[6] = { "S_Class_Scout", "Evasion", 25, 2, 95.0, 180.0 },
	[7] = { "S_Class_Cleric", "Guard", 0, 0 },
};

local function spellAt(index)
	local spell = spells[index + 1];
	if spell == nil then
		return nil;
	end
	return { id = 1000 + index, name = spell[2], icon = "Interface/Icons/Spells/" .. spell[1] .. ".htex", cost = spell[3], powertype = spell[4] };
end

local function spellById(id)
	return spells[id - 1000 + 1];
end

local previousIsSpell = IsActionButtonSpell;
local previousGetSpell = GetActionButtonSpell;
function IsActionButtonSpell(index)
	return spells[index + 1] ~= nil or previousIsSpell(index);
end
function GetActionButtonSpell(index)
	return spellAt(index) or previousGetSpell(index);
end
function GetSpellEffectiveCost(id)
	local spell = spellById(id);
	return spell and spell[3] or 0;
end
function IsSpellOnCooldown(id)
	local spell = spellById(id);
	return spell ~= nil and spell[5] ~= nil;
end
function GetSpellCooldownRemaining(id)
	local spell = spellById(id);
	return (spell and spell[5]) or 0;
end
function GetSpellCooldownProgress(id)
	local spell = spellById(id);
	if spell == nil or spell[5] == nil then
		return 1.0;
	end
	return 1.0 - spell[5] / spell[6];
end

-- Not enough rage for Shield Slam: its cost turns grey.
UiTest.data.units.player.power[1] = 25;

ActionBar_UpdateButtons(ActionBar);
ActionBar_UpdateButtons(ActionBar2);

-- A hit right after load: the health bar keeps a trail where it was.
PlayerHealthBar:SetProgress(0.92);
PlayerHealthBar:SetProgressInstant(0.92);
PlayerHealthBar:SetProgress(0.55);
