-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
--
-- Units: GetUnit(name) returns a mock of the client's UnitHandle. A unit is plain data; its methods
-- are derived from the field names, so "level = 12" answers unit:GetLevel() and "alive = true"
-- answers unit:IsAlive(). A field holding a function is called with the method's arguments.
-- Methods without a field are reported as unmocked ("UnitHandle:GetFoo") and return nil.
--
-- Scenarios change units through UiTest.data.units, e.g.
--   UiTest.data.units.target = UiTest.MakeUnit({ name = "Forest Wolf", level = 5, hostile = true })
--   UiTest.FireEvent("PLAYER_TARGET_CHANGED")

local UnitMethods = {}

local function FieldForMethod(method)
	local stem = method:match("^Get(.+)$") or method:match("^(Is.+)$") or method:match("^(Has.+)$")
	if not stem then
		return nil
	end
	if stem:find("^Is") or stem:find("^Has") then
		-- IsAlive -> alive, HasProficiency -> proficiency
		stem = stem:gsub("^Is", ""):gsub("^Has", "")
	end
	return stem:sub(1, 1):lower() .. stem:sub(2)
end

local UnitMeta = {
	__index = function(unit, method)
		if UnitMethods[method] then
			return UnitMethods[method]
		end

		local field = FieldForMethod(method)
		return function(self, ...)
			local value = field and rawget(self, field)
			if value == nil then
				UiTest.ReportUnmocked("UnitHandle:" .. method)
				return nil
			end
			if type(value) == "function" then
				return value(self, ...)
			end
			return value
		end
	end
}

-- Power and stat lookups take an index argument.
function UnitMethods:GetPower(powerType)
	local power = rawget(self, "power") or {}
	return power[powerType or self:GetPowerType()] or 0
end

function UnitMethods:GetMaxPower(powerType)
	local maxPower = rawget(self, "maxPower") or {}
	return maxPower[powerType or self:GetPowerType()] or 0
end

function UnitMethods:GetStat(statId)
	return (rawget(self, "stats") or {})[statId] or 0
end

function UnitMethods:GetPosStat(statId)
	return (rawget(self, "posStats") or {})[statId] or 0
end

function UnitMethods:GetNegStat(statId)
	return (rawget(self, "negStats") or {})[statId] or 0
end

function UnitMethods:GetAuraCount()
	return #(rawget(self, "auras") or {})
end

function UnitMethods:GetAura(index)
	return (rawget(self, "auras") or {})[index + 1]
end

-- The spell a unit casts: castSpellName (nil while idle), castProgress and castInterrupted fields.
function UnitMethods:GetCastSpellName()
	return rawget(self, "castSpellName")
end

function UnitMethods:GetCastProgress()
	return rawget(self, "castProgress") or 0
end

function UnitMethods:IsCastInterrupted()
	return rawget(self, "castInterrupted") == true
end

function UnitMethods:GetKnownClassCount()
	return #(rawget(self, "knownClasses") or {})
end

local function KnownClass(self, index)
	return (rawget(self, "knownClasses") or {})[index + 1] or {}
end

function UnitMethods:GetKnownClassName(index) return KnownClass(self, index).name end
function UnitMethods:GetKnownClassLevel(index) return KnownClass(self, index).level or 0 end
function UnitMethods:GetKnownClassMaxLevel(index) return KnownClass(self, index).maxLevel or 10 end
function UnitMethods:GetKnownClassXp(index) return KnownClass(self, index).xp or 0 end
function UnitMethods:GetKnownClassXpToNextLevel(index) return KnownClass(self, index).xpToNext or 1000 end
function UnitMethods:IsKnownClassUnlocked(index) return KnownClass(self, index).unlocked ~= false end
function UnitMethods:IsKnownClassActive(index) return KnownClass(self, index).active == true end
function UnitMethods:GetKnownClassChangeSpell(index) return KnownClass(self, index).changeSpell end

--- Creates a unit from plain data, filling in what every unit has.
function UiTest.MakeUnit(data)
	local unit = {
		guid = 1,
		guidString = "0x1",
		entry = 0,
		name = "Unit",
		subtitle = "",
		level = 1,
		class = "Warrior",
		type = "Humanoid",
		health = 100,
		maxHealth = 100,
		powerType = 0,
		power = { [0] = 50 },
		maxPower = { [0] = 100 },
		alive = true,
		friendly = true,
		hostile = false,
		guildName = "",
		auras = {},
		knownClasses = {},
	}
	for key, value in pairs(data) do
		unit[key] = value
	end
	unit.guidString = unit.guidString ~= "0x1" and unit.guidString or string.format("0x%X", unit.guid)
	return setmetatable(unit, UnitMeta)
end

--- Creates an aura for unit.auras (AuraHandle mock). spell is a spell table, see 30_spells.lua.
function UiTest.MakeAura(data)
	local aura = {
		duration = 0,
		stackCount = 1,
		negative = false,
		canExpire = data.duration ~= nil and data.duration > 0,
		expired = false,
		casterId = 1,
	}
	for key, value in pairs(data) do
		aura[key] = value
	end
	return setmetatable(aura, UnitMeta)
end

UiTest.data.units = {
	player = UiTest.MakeUnit({
		guid = 0x10001,
		name = "Aldric",
		level = 12,
		class = "Warrior",
		health = 842,
		maxHealth = 1120,
		powerType = 1,
		power = { [0] = 0, [1] = 35 },
		maxPower = { [0] = 0, [1] = 100 },
		guildName = "Wardens of Briarwatch",
		armor = 412,
		attackPower = 186,
		minDamage = 38,
		maxDamage = 57,
		attackTime = 2600,
		meleeCritChance = 6.2,
		spellCritChance = 0,
		spellDamage = 0,
		spellHealing = 0,
		manaRegen = 0,
		manaRegenCasting = 0,
		talentPoints = 3,
		availableAttributePoints = 2,
		armorReductionFactor = 0.18,
		activeClassLevel = 12,
		stats = { [0] = 54, [1] = 31, [2] = 48, [3] = 18, [4] = 22 },
		knownClasses = {
			{ name = "Warrior", level = 12, maxLevel = 20, xp = 3400, xpToNext = 5200, active = true },
		},
	}),
}

function GetUnit(name)
	return UiTest.data.units[name]
end

function UnitExists(name)
	return UiTest.data.units[name] ~= nil
end

UiTest.data.money = 123456
UiTest.data.xp = { current = 3400, nextLevel = 5200 }

function UnitMoney(name)
	return name == "player" and UiTest.data.money or 0
end

function PlayerXp()
	return UiTest.data.xp.current
end

function PlayerNextLevelXp()
	return UiTest.data.xp.nextLevel
end

function UnitStat(name, statId)
	local unit = UiTest.data.units[name]
	if not unit then
		return 0, 0
	end
	return unit:GetStat(statId), 0
end

function UnitArmor(name)
	local unit = UiTest.data.units[name]
	return unit and unit:GetArmor() or 0, 0
end

function UnitDisplayId(name)
	return 0
end
