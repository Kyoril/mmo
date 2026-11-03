-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
-- Exercise the actual spell-tooltip layout and its themed surface.
dofile(UiTest.scenarioDir .. "bluegold_fixture.lua")
ShowUIPanel(CharacterWindow)
CharacterWindowTitle:SetText(GetUnit("player"):GetName())
InventoryFrame:Show()

function GetSpellEffectiveCost() return 20 end
function GetSpellEffectiveCastTime() return 0 end
function GetSpellEffectiveCooldown() return 20000 end
function GetSpellDescription() return "Increases your Dodge chance by 25% for 8 seconds." end
function IsPassiveSpell() return false end
function IsChanneledSpell() return false end
function IsRacialSpell() return false end

local previousActionSpell = GetActionButtonSpell
local spell = { id = 6, name = "Evasive Step", rank = 0, cost = 20, powertype = 2, level = 0,
	icon = previousActionSpell(5).icon }
function spell:GetCreatedItem() return 0 end
function spell:GetNumReagents() return 0 end
function spell:GetRequiredSkill() return 0, 0, "" end
function GetActionButtonSpell(index)
	return index == 5 and spell or previousActionSpell(index)
end
UiTest.data.units.player.power[2] = 100
GameTooltip_SetSpell(spell)
GameTooltip:ClearAnchors()
GameTooltip:SetAnchor(AnchorPoint.BOTTOM, AnchorPoint.TOP, ActionButton6, -16)
GameTooltip:SetAnchor(AnchorPoint.LEFT, AnchorPoint.LEFT, ActionButton6, 0)
GameTooltip:Show()
