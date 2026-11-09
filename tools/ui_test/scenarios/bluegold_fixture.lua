-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
-- Shared populated fixtures for the BlueGold visual review scenarios, loaded after the UI.
-- Keep these overrides scoped to scenarios, never to production Lua or the default mocks.
local names = { "Stamina", "Strength", "Agility", "Intellect", "Spirit" }
function GetNumAttributes() return #names end
function GetAttributeInfo(index) return index, names[index + 1], "Attribute " .. tostring(index) end
function GetAttributeName(id) return names[id + 1] or "" end
function UnitAttributeCost() return 1 end
function IsInGuild() return false end
function GetPartyLeaderIndex() return -1 end
function HasPartyMember() return false end

UiTest.data.units.player.name = "Kaelith"
UiTest.data.units.player.level = 10
UiTest.data.units.player.health = 245
UiTest.data.units.player.maxHealth = 245
UiTest.data.units.player.powerType = 0
UiTest.data.units.player.power = { [0] = 880 }
UiTest.data.units.player.maxPower = { [0] = 880 }
UiTest.data.units.player.armor = 181
UiTest.data.units.player.attackPower = 21
UiTest.data.units.player.minDamage = 28
UiTest.data.units.player.maxDamage = 44
UiTest.data.units.player.attackTime = 3000
UiTest.data.units.player.availableAttributePoints = 1
UiTest.data.units.player.stats = { [0] = 38, 21, 22, 55, 45 }
UiTest.data.units.player.knownClasses = { { name = "Mage", level = 10, maxLevel = 20, xp = 271, xpToNext = 400, active = true } }
UiTest.data.xp = { current = 480, nextLevel = 2800 }
UiTest.data.money = 1284735
UiTest.data.bags = { [0] = 0, [1] = 0, [2] = 0, [3] = 0 }
UiTest.data.units.target = UiTest.MakeUnit({ name = "Training Dummy", level = 10, health = 1000, maxHealth = 1000 })

local icons = { "acolyte_robe_icon", "acolyte_staff_icon", "arcane_cord_icon", "ashen_quarterstaff_icon", "barrowfront_mail_hauberk_icon", "belt_of_sanctity_icon", "berserker_ring_icon", "boots_of_the_templar_icon", "cadet_shield_icon", "cloak_of_radiant_devotion_icon" }
local itemNames = { "Acolyte Robe", "Acolyte Staff", "Arcane Cord", "Ashen Quarterstaff",
	"Barrowfront Mail Hauberk", "Belt of Sanctity", "Berserker Ring", "Boots of the Templar",
	"Cadet's Shield", "Cloak of Radiant Devotion" }
local function item(index, quality)
	local object = {}
	function object:GetIcon() return "Interface/Icons/Items/" .. icons[index] .. ".htex" end
	function object:GetName() return itemNames[index] end
	function object:GetStackCount() return index == 3 and 4 or 1 end
	function object:GetQuality() return quality end
	function object:IsUsable() return true end
	return object
end
local equipment = { [65283] = item(1, 3), [65295] = item(2, 3), [65296] = item(9, 3), [65285] = item(3, 1), [65287] = item(8, 2), [65294] = item(10, 2) }
local backpack = {}
for i = 0, 11 do backpack[GetBackpackSlot(i)] = item(i % #icons + 1, i % 3 == 0 and 3 or 1) end
function GetInventorySlotItem(unit, slot) return equipment[slot] or backpack[slot] end

local spells = { "S_Fireball", "T_Icon_Fire_02", "S_FireNova", "T_Icon_Fire_01", "T_Icon_Fire_03", "T_Icon_Fire_04", "T_Icon_Fire_05", "T_Icon_Fire_06", "T_Icon_Fire_07", "T_Icon_Fire_08" }
function IsActionButtonSpell(index) return index >= 0 and index < #spells end
function IsActionButtonItem() return false end
function IsActionButtonEmote() return false end
function IsActionButtonUsable() return true end
function IsActionButtonActive() return false end
function IsSpellOnCooldown() return false end
function GetSpellCooldownProgress() return 1 end
function GetActionButtonSpell(index)
	local icon = spells[index + 1]
	return icon and { id = index + 1, name = icon, icon = "Interface/Icons/Spells/" .. icon .. ".htex" } or nil
end
local previousKeysForBinding = GetKeysForBinding
function GetKeysForBinding(binding)
	local index = tonumber(binding:match("^ACTIONBUTTON(%d+)$"))
	if not index then return previousKeysForBinding(binding) end
	local keys = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "+" }
	return { (index > 12 and "CTRL-" or "") .. keys[(index - 1) % 12 + 1] }
end

-- These values mirror the public client API shapes used by OptionsFrame.lua.
function GetDisplayMonitors() return { { name = "Primary Monitor", width = 2560, height = 1440, primary = true } } end
function GetScreenResolutions() return { { width = 1920, height = 1080, label = "1920x1080" }, { width = 2560, height = 1440, label = "2560x1440" } } end
function IsFullscreenWindow() return true end
function GetWindowSize() return 2560, 1440 end
function GetGraphicsHardwareInfo() return { gpu = "NVIDIA GeForce RTX 2080 Ti", videoMemoryMB = 11264, integrated = false, recommendedQuality = 2 } end
function GetGraphicsPresetValues() return {} end
local values =
{
	gxMonitor = "0", gxWindow = "1", gxResolution = "2560x1440", gxRenderScale = "1", gxUpscaleFilter = "3", gxVSync = "1",
	gxMaxFpsEnabled = "1", gxMaxFps = "60", gxMaxFpsBkEnabled = "1", gxMaxFpsBk = "30",
	gxTargetFpsEnabled = "0", gxTargetFps = "60", gxExposure = "1", gxQuality = "2", ShowActionBar2 = "0",
}
for k, v in pairs(values) do UiTest.data.cvars[k] = v end

CharacterWindow_CreateAttributeRows()
dofile(UiTest.scenarioDir .. "world.lua")
UiTest.FireEvent("PLAYER_TARGET_CHANGED")
ActionBar_UpdateShortcutLabels(1)
ZoneText:Hide(); ZoneSubText:Hide()

