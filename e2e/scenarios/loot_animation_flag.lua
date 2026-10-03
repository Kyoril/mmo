-- Regression: the Looting unit flag drives the client's kneeling loot pose.
--
-- Three things must hold, and the client animation is only correct if all three do:
--   1. Opening a loot window sets unit_flags::Looting on the looting character.
--   2. Position-changing movement closes the window and clears the flag (otherwise the
--      character slides across the floor mid-kneel).
--   3. An explicit release clears it too.
--
-- Uses Forest Bandit (entry 37): one of its loot tables always drops gold. The server only
-- creates a loot instance -- and with it the Lootable flag -- when the roll yields gold or
-- items, so a creature whose drops are pure chance (e.g. Forest Wolf) is not deterministic.

local FOREST_BANDIT = 37
local UNIT_FLAG_LOOTABLE = 2
local UNIT_FLAG_LOOTING = 4

local function hasFlag(guid, flag)
	local flags = GetUnitFlags(guid)
	Assert(flags >= 0, "GetUnitFlags returned -1 for guid " .. tostring(guid))
	return math.floor(flags / flag) % 2 == 1
end

-- Save the character's starting position before the scenario body runs.
local startX, startY, startZ = GetPosX(Me()), GetPosY(Me()), GetPosZ(Me())

local bandit = GM.CreateMonster(FOREST_BANDIT)
-- Declared out here so the cleanup below can destroy it even if the body fails first.
local second = nil

-- Wrap the scenario body in pcall so cleanup always runs, even on assertion failure.
local success, errorMsg = pcall(function()
	Assert(IsAlive(bandit), "bandit should spawn alive")

	TargetUnit(bandit)
	GM.KillTarget()

	Assert(WaitUntil(function() return not IsAlive(bandit) end, 10000, "bandit dies"),
		"bandit should be dead after GM.KillTarget")

	Assert(WaitUntil(function() return hasFlag(bandit, UNIT_FLAG_LOOTABLE) end, 10000,
		"corpse becomes lootable"),
		"the corpse should carry unit_flags::Lootable after the kill")

	-- 1. Opening the loot window flags the looter.
	LootUnit(bandit)
	Assert(WaitUntil(function() return hasFlag(Me(), UNIT_FLAG_LOOTING) end, 10000,
		"looter is flagged"),
		"unit_flags::Looting should be set on the character while the loot window is open")

	Log("Looting flag set after LootUnit")

	-- 2. Moving cancels it. Step a couple of units away from where the corpse is.
	local x, y, z = GetPosX(Me()), GetPosY(Me()), GetPosZ(Me())
	Assert(MoveTo(x + 3.0, y, z, 15000), "character should be able to step away from the corpse")

	Assert(WaitUntil(function() return not hasFlag(Me(), UNIT_FLAG_LOOTING) end, 10000,
		"movement cancels looting"),
		"moving should close the loot window and clear unit_flags::Looting")

	Log("Looting flag cleared by movement")

	-- 3. An explicit release clears it too. Use a fresh corpse so this step does not depend on
	-- what the first loot window left behind.
	second = GM.CreateMonster(FOREST_BANDIT)
	Assert(IsAlive(second), "second bandit should spawn alive")

	TargetUnit(second)
	GM.KillTarget()

	Assert(WaitUntil(function() return not IsAlive(second) end, 10000, "second bandit dies"),
		"second bandit should be dead after GM.KillTarget")

	LootUnit(second)
	Assert(WaitUntil(function() return hasFlag(Me(), UNIT_FLAG_LOOTING) end, 10000,
		"looter is flagged again"),
		"unit_flags::Looting should be set on the second corpse's loot")

	ReleaseLoot(second)
	Assert(WaitUntil(function() return not hasFlag(Me(), UNIT_FLAG_LOOTING) end, 10000,
		"release clears looting"),
		"releasing the loot window should clear unit_flags::Looting")

	Log("Looting flag cleared by explicit release")
end)

-- Always clean up, regardless of whether the scenario body succeeded or failed.
GM.DestroyMonster(bandit)
if second then
	GM.DestroyMonster(second)
end
MoveTo(startX, startY, startZ, 15000)

-- If the scenario body failed, re-raise the error so the scenario still fails.
if not success then
	error(errorMsg)
end
