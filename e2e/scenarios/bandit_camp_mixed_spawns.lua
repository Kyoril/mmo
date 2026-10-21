-- e2e-timeout: 240
-- Regression: the Forest Bandit camps spawn from mixed spawn points.
--
--   1. Every spawn point of both camps is up: 16 at the Forest Bandit grounds near Oakenshire,
--      19 at the Forest Bandit grounds at (-101, 137).
--   2. Mixed points (Forest Bandit 3 : Bandit Mage 1) roll Bandit Mages beyond the two fixed mage
--      points. 21 mixed points all rolling a bandit happens 0.24 % of the time, so a miss kills the
--      camp once and counts the re-rolled respawns before failing.
--   3. The Bandit Mage casts its own Frostbolt (275), never the old shared one (158), and pauses
--      between casts (cooldown 6 s from cast start).
--
-- The level-10 test character is far above the level 3-4 bandits, so they only aggro within a few
-- metres; the mage is pulled with GM.DamageTarget. Godmode keeps the character alive.

local FOREST_BANDIT = 37
local BANDIT_MAGE = 39
local BANDIT_FROSTBOLT = 275
local OLD_FROSTBOLT = 158

local CAMP_OAKENSHIRE = { name = "Forest Bandit grounds", x = 245.0, y = 16.5, z = 765.0, points = 16 }
local CAMP_WEST = { name = "Forest Bandit grounds (-101, 137)", x = -101.0, y = 5.5, z = 137.0, points = 19 }
local FIXED_MAGES = 2

local me = Me()
local startX, startY, startZ = GetPosX(me), GetPosY(me), GetPosZ(me)

local function aliveBandits()
	return CountUnitsByEntry(FOREST_BANDIT) + CountUnitsByEntry(BANDIT_MAGE)
end

local function visitCamp(camp)
	GM.Worldport(0, camp.x, camp.y, camp.z, 0)
	Assert(WaitUntil(function() return aliveBandits() == camp.points end, 15000, camp.name .. " is up"),
		camp.name .. ": expected " .. camp.points .. " bandits, saw " .. CountUnitsByEntry(FOREST_BANDIT)
		.. " Forest Bandits and " .. CountUnitsByEntry(BANDIT_MAGE) .. " Bandit Mages")
	local mages = CountUnitsByEntry(BANDIT_MAGE)
	Log(camp.name .. ": " .. CountUnitsByEntry(FOREST_BANDIT) .. " Forest Bandits, " .. mages .. " Bandit Mages")
	return mages
end

local success, errorMsg = pcall(function()
	GM.Godmode(true)

	local mages = visitCamp(CAMP_OAKENSHIRE) + visitCamp(CAMP_WEST)
	if mages <= FIXED_MAGES then
		-- Every mixed point rolled a bandit: kill the west camp and count its respawns, which roll again.
		Log("no mixed point rolled a mage; re-rolling the west camp")
		while aliveBandits() > 0 do
			local unit = FindUnitByEntry(FOREST_BANDIT) or FindUnitByEntry(BANDIT_MAGE)
			TargetUnit(unit)
			GM.KillTarget()
			WaitUntil(function() return not IsAlive(unit) end, 5000, "bandit dies")
		end
		Assert(WaitUntil(function() return aliveBandits() == CAMP_WEST.points end, 60000, "west camp respawns"),
			"the west camp should respawn all " .. CAMP_WEST.points .. " points")
		mages = CountUnitsByEntry(BANDIT_MAGE)
		Log("after the respawn: " .. mages .. " Bandit Mages")
		Assert(mages > 1, "mixed spawn points never rolled a Bandit Mage (fixed points only)")
	end

	-- 3. The mage's Frostbolt.
	local mage = FindUnitByEntry(BANDIT_MAGE)
	Assert(mage ~= nil, "a Bandit Mage should be visible")
	TargetUnit(mage)
	GM.DamageTarget(1)

	Assert(WaitUntil(function() return SpellGoCount(mage, BANDIT_FROSTBOLT) >= 1 end, 20000, "first frostbolt"),
		"the Bandit Mage should cast Frostbolt " .. BANDIT_FROSTBOLT)
	local first = os.time()
	Assert(WaitUntil(function() return SpellGoCount(mage, BANDIT_FROSTBOLT) >= 2 end, 20000, "second frostbolt"),
		"the Bandit Mage should cast Frostbolt again once its cooldown ends")
	local gap = os.time() - first
	Log("seconds between the first two frostbolts: " .. gap)
	Assert(gap >= 5, "frostbolts should be about 6 s apart (cooldown), saw " .. gap .. " s")
	Assert(SpellGoCount(mage, OLD_FROSTBOLT) == 0, "the Bandit Mage must no longer cast the shared Frostbolt " .. OLD_FROSTBOLT)
end)

TargetUnit("0x0")
GM.Godmode(false)
GM.Worldport(0, startX, startY, startZ, 0)

if not success then
	error(errorMsg)
end
