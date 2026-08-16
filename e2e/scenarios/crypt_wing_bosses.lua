-- e2e-own-character: wng
-- e2e-timeout: 300
--
-- The Hollow Choir's two wing bosses, which flank the approach to Sevrin Wax.
--
-- Both are trigger-driven, and both hang their one mechanic off a health threshold: Ossuar tears
-- his bindings at 50% and Choirmistress Vell throws herself into the crescendo at 40%. Each applies
-- Grave Frenzy (244) to itself, which is the only part of the script a client can observe -- the
-- phase variables can be written from Lua but not read back. Vell additionally raises husks on a
-- timer, capped so she cannot flood the room the way the crypt adds once did.
--
-- Damage for the threshold steps comes from GM.DamageTarget; the pull is a real attack so the boss
-- builds a genuine threat list. Godmode is re-armed after the worldport -- it does not survive a
-- cross-map teleport, and a dead character drops off the threat list, which resets the encounter.

local OSSUAR = 84
local VELL = 85
local WAX_SEALED_HUSK = 82
local GRAVE_FRENZY = 244

local SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z = 0, 292.267, 5.33, 552.571

GM.Godmode(true)
GM.Worldport(SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z, 0)
Assert(WaitUntil(function() return GetPosX(Me()) > 0 end, 30000, "at the spawn point"),
	"player should start from the default spawn")

--- Pulls `entry`, walks it to just past `thresholdPct`, and checks the frenzy lands.
local function FightWingBoss(entry, label, x, y, z, thresholdPct)
	GM.Worldport(1, x, y, z, 0)
	Assert(WaitUntil(function() return GetPosZ(Me()) ~= SPAWN_Z end, 30000, "arrived in the wing"),
		label .. ": the worldport should land")
	Sleep(3000)
	GM.Godmode(true)

	local boss = nil
	Assert(WaitUntil(function() boss = FindUnitByEntry(entry); return boss ~= nil end, 30000, label .. " visible"),
		label .. " should be spawned")

	local maxHealth = GetMaxHealth(boss)
	Assert(maxHealth > 0, label .. ": max health should be known before the pull")

	-- GetMaxHealth can resolve before the current-health field has arrived, which reads as 0 and is
	-- indistinguishable from a corpse. Wait for the field rather than trusting the first sample.
	Assert(WaitUntil(function() return GetHealth(boss) > 0 end, 30000, label .. " health field arrives"),
		label .. ": current health never replicated (max was " .. maxHealth .. ")")

	-- The dungeon instance stays warm between scenarios (it only unloads after 15 idle minutes),
	-- so this boss may still be damaged, or mid-reset, from whatever ran before. Wait for the
	-- reset to restore it instead of assuming a fresh spawn -- otherwise the threshold arithmetic
	-- below starts from the wrong health and fails with "already below N%".
	Assert(WaitUntil(function() return GetHealth(boss) >= maxHealth end, 60000, label .. " at full health"),
		label .. " should be at full health before the pull (warm instance left it at "
			.. GetHealth(boss) .. " of " .. maxHealth .. ")")

	Assert(not HasAura(boss, GRAVE_FRENZY), label .. " should not be frenzied before the pull")
	Log(string.format("%s is up with %d health", label, maxHealth))

	-- Land clear of the boss so its fields replicate before it aggroes, then walk in. Teleporting
	-- into melee range instead makes it pull on arrival, and the current-health field then never
	-- catches up -- which reads as a corpse.
	Assert(MoveTo(GetPosX(boss), GetPosY(boss), GetPosZ(boss), 25000) or GetDistance(Me(), boss) <= 5.0,
		label .. ": should be able to walk into melee range")

	TargetUnit(boss)
	FaceUnit(boss)
	StartAttack(boss)
	Assert(WaitUntil(function() return GetHealth(boss) < maxHealth end, 30000, label .. " pulled"),
		label .. ": auto-attacks should connect and open the encounter")

	-- Hold the pull long enough for threat to build. The swing that opens combat contributes none:
	-- the combat state connects its `threatened` listener while entering combat, after that swing
	-- has already emitted.
	Sleep(10000)
	Assert(GetHealth(boss) < maxHealth,
		label .. " should still be engaged after the opening swings, not reset to full health")

	-- One measured hit to land just under the threshold, read immediately before firing so
	-- regeneration cannot make the step overshoot.
	local target = math.floor(maxHealth * ((thresholdPct - 1) / 100))
	local current = GetHealth(boss)
	Assert(current > target, label .. ": already below " .. thresholdPct .. "%")
	GM.DamageTarget(current - target)

	Assert(WaitUntil(function() return HasAura(boss, GRAVE_FRENZY) end, 25000, label .. " frenzies"),
		label .. ": crossing " .. thresholdPct .. "% should apply Grave Frenzy")
	Log(label .. " crossed " .. thresholdPct .. "% and frenzied")

	-- It is meant to last the rest of the fight, not tick off like Sevrin Wax's Rite.
	Sleep(6000)
	Assert(HasAura(boss, GRAVE_FRENZY), label .. ": Grave Frenzy should persist")

	local remaining = GetHealth(boss)
	Assert(remaining > 0, label .. " should still have health before the killing blow")
	GM.DamageTarget(remaining + 100)
	Assert(WaitUntil(function() return not IsAlive(boss) end, 20000, label .. " dies"),
		label .. ": the killing blow should register")

	StopAttack()
	Log(label .. " is down")
end

-- North wing. Ossuar is a melee bruiser; his timer trigger throws threat at a random player, which
-- needs no assertion here beyond the fight staying alive through it.
FightWingBoss(OSSUAR, "Ossuar", -16, 1, 4, 50)

-- South wing. Vell raises husks on a timer while she lives; they must be gone once she is not.
FightWingBoss(VELL, "Choirmistress Vell", -12, 1, -26, 40)

Assert(WaitUntil(function() return CountUnitsByEntry(WAX_SEALED_HUSK) == 0 end, 25000, "husks despawn"),
	"Vell's husks should not outlive her")
Log("Vell's husks cleaned up")

GM.Worldport(SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z, 0)
Assert(WaitUntil(function() return GetPosX(Me()) > 0 end, 30000, "returned to the spawn point"),
	"player should be back at the default spawn")
GM.Godmode(false)

Assert(not IsDisconnected(), "world server should still be alive")
