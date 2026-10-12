-- e2e-own-character: hcc
-- e2e-timeout: 300
--
-- The Hollow Choir's bosses are melee fighters: once pulled they must walk to their victim and
-- follow it when it moves away. Each boss has one real ability. With a long maximum range on it
-- the creature AI counted it as a ranged spell and made the boss a Caster; Casters stand off while
-- their spell reaches the victim, so the bosses cast but never closed in or meleed.
--
-- For each boss: land a few metres off, pull it from there with a GM hit, expect it to come into
-- melee range, then walk away over flat floor and expect it to follow into melee range again.
-- The routes avoid stepping off the apse podium: the headless client takes such steps with a
-- FALLING flag the server rejects without a landing packet, which freezes our position server-side.

local SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z = 0, 292.267, 5.33, 552.571

-- entry, label, spawn (x, z) from tools/hollow_choir/author_spawns.py, landing point, where to
-- walk after the boss has come up. A boss may notice us on landing and start walking before we
-- can look, so distances are measured from its spawn.
local BOSSES = {
	{ 86, "Oswin",  { -6.5, -40.0 },  { -6.5, 0.5, -30.0 },  { -6.0, 0.2, -20.0 } },
	{ 87, "Mereth", { -127.0, -3.0 }, { -127.0, -2.5, 8.0 }, { -127.0, -3.0, 21.0 } },
	-- Veyr's room is the podium and two short passages; the headless client cannot walk off
	-- either way without a fall (see above), so his part is the walk up from the podium alone.
	{ 88, "Veyr",   { -92.0, 0.0 },   { -91.0, 5.7, -15.0 }, nil },
}

local function Flat(x1, z1, x2, z2)
	return math.sqrt((x1 - x2) ^ 2 + (z1 - z2) ^ 2)
end

local function WaitInMelee(boss, label, what)
	Assert(WaitUntil(function() FaceUnit(boss); return GetDistance(Me(), boss) <= 5.0 end, 15000, label .. " " .. what),
		label .. " should " .. what .. " (still " .. string.format("%.1f", GetDistance(Me(), boss)) .. " away)")
end

local function Chase(entry, label, spawn, landing, away)
	GM.Worldport(1, landing[1], landing[2], landing[3], 0)
	Assert(WaitUntil(function() return GetPosX(Me()) < 100 end, 30000, label .. ": arrived"),
		label .. ": the worldport into the Hollow Choir should land")
	Sleep(3000)
	-- Godmode does not survive the map change.
	GM.Godmode(true)

	local boss = nil
	Assert(WaitUntil(function() boss = FindUnitByEntry(entry); return boss ~= nil end, 30000, label .. " visible"),
		label .. " should be spawned")
	local maxHealth = GetMaxHealth(boss)
	Assert(WaitUntil(function() return GetHealth(boss) > 0 end, 30000, label .. " health arrives"),
		label .. ": current health never replicated")
	local homeX, homeZ = spawn[1], spawn[2]

	-- Pull from where we stand. A level-1 test character mostly misses a level-13 elite, so the
	-- opening hit comes from the GM command.
	TargetUnit(boss)
	FaceUnit(boss)
	GM.DamageTarget(50)
	StartAttack(boss)
	Assert(WaitUntil(function() return GetHealth(boss) < maxHealth end, 10000, label .. " pulled"),
		label .. ": the hit should open the encounter")
	WaitInMelee(boss, label, "walk up to its victim")
	local approached = Flat(GetPosX(boss), GetPosZ(boss), homeX, homeZ)
	Log(label .. " walked " .. string.format("%.1f", approached) .. " units to its victim")

	if not away then
		Assert(approached > 8.0, label .. " should have walked up to its victim (walked "
			.. string.format("%.1f", approached) .. ")")
		return
	end

	-- Walk off and expect it to follow.
	local fromX, fromZ = GetPosX(boss), GetPosZ(boss)
	StopAttack()
	Assert(MoveTo(away[1], away[2], away[3], 15000), label .. ": the walk away should arrive")
	WaitInMelee(boss, label, "follow its victim")
	local followed = Flat(GetPosX(boss), GetPosZ(boss), fromX, fromZ)
	Log(label .. " followed " .. string.format("%.1f", followed) .. " units")
	Assert(approached + followed > 8.0, label .. " should have walked after its victim (approached "
		.. string.format("%.1f", approached) .. ", followed " .. string.format("%.1f", followed) .. ")")
end

GM.Godmode(true)
GM.Worldport(SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z, 0)
Assert(WaitUntil(function() return GetPosX(Me()) > 0 end, 30000, "at the spawn point"),
	"player should start from the default spawn")

for _, b in ipairs(BOSSES) do
	Chase(b[1], b[2], b[3], b[4], b[5])
end
