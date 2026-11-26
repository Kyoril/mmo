-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
-- Item tooltip of a glider with all four glide stats (item stat types 32 - 35).
dofile(UiTest.scenarioDir .. "bluegold_fixture.lua")
InventoryFrame:Show()

local stats = {
	{ type = "GLIDE_SPEED", value = 20 },
	{ type = "GLIDE_AGILITY", value = 50 },
	{ type = "GLIDE_LIFT", value = 30 },
	{ type = "GLIDE_ENDURANCE", value = 25 },
}

function GetComparisonInventorySlot() return -1 end

local glider = {}
function glider:GetName() return "Swift Test Glider" end
function glider:GetQuality() return 3 end
function glider:IsBound() return true end
function glider:GetBonding() return 1 end
function glider:GetInventoryType() return "GLIDER" end
function glider:GetDescription() return "" end
function glider:GetStatAttribute() return -1 end
function glider:GetStatType(i) return stats[i + 1] and stats[i + 1].type or nil end
function glider:GetStatValue(i) return stats[i + 1] and stats[i + 1].value or 0 end
function glider:GetSpell() return nil end
function glider:GetSpellTriggerType() return nil end
function glider:GetStartQuestId() return 0 end
function glider:GetProficiency() return nil end
function glider:GetStackCount() return 1 end
setmetatable(glider, { __index = function() return function() return 0 end end })

GameTooltip_SetItem(glider)
GameTooltip:ClearAnchors()
GameTooltip:SetAnchor(AnchorPoint.TOP, AnchorPoint.TOP, InventoryFrame, 0)
GameTooltip:SetAnchor(AnchorPoint.RIGHT, AnchorPoint.LEFT, InventoryFrame, -24)
GameTooltip:Show()
