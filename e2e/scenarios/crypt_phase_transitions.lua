-- e2e-own-character: wax
-- e2e-timeout: 300
--
-- Coverage for the Sevrin Wax encounter's phase transitions, deferred when the encounter shipped.
--
-- The encounter's two transitions are edge-triggered on health percentage (OnHealthDroppedBelow 65
-- and 30), and each one is the head of a multi-action chain that has to run to completion. Reading
-- the triggers back out of the proto only proves what was authored; it says nothing about whether
-- the chains actually fire, fire once, or leave the boss stuck. The original attempt at this
-- scenario could not get far enough to find out: every spell-based auto-attack swing armed the
-- swing timer twice, and the fight froze at the phase-2 transition in four runs out of four.
--
-- Damage comes from GM.DamageTarget rather than the character's own output. A level-10 test
-- character cannot chew through a 2670-health elite quickly or predictably, and the phases are
-- gated on percentages -- so the scenario needs to land on a chosen side of a threshold, not
-- grind towards it. The cheat routes through the normal damage path, so the triggers see exactly
-- what a real fight would produce.

local SEVRIN_WAX = 81
local WAX_SEALED_HUSK = 82
local CHOIRBOUND_ACOLYTE = 83

-- Self-buffs the boss applies to himself at each transition; these are the scenario's window onto
-- phase state, since instance variables can be written from Lua but not read back.
local RITE_OF_RISING = 240
local UNHALLOWED_FERVOR = 241

local SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z = 0, 292.267, 5.33, 552.571

-- He stands at (-24, 1, 0) facing east down the nave. Stand inside melee reach of that: the pull
-- has to come from a real attack, not from the damage cheat. Trigger 30 opens the Rite with
-- StopAutoAttack, which nulls the boss's victim, and the AI resets outright unless its threat list
-- is still populated. Cheat damage cannot populate it -- the combat state connects its `threatened`
-- listener as it enters combat, which races the cheat's emit, so the threat lands on the floor and
-- the boss resets to full health four seconds into the Rite.
local CRYPT_X, CRYPT_Y, CRYPT_Z = -21, 1, 0

GM.Godmode(true)

-- Normalise the starting position rather than asserting it. The harness retries a timed-out
-- scenario, and a retry that began inside the crypt would make the arrival check pass without the
-- worldport having moved anything.
GM.Worldport(SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z, 0)
Assert(WaitUntil(function() return GetPosX(Me()) > 0 end, 30000, "at the spawn point"),
	"player should start from the default spawn")

GM.Worldport(1, CRYPT_X, CRYPT_Y, CRYPT_Z, 0)
Assert(WaitUntil(function() return GetPosX(Me()) < -10 end, 30000, "arrived in the crypt"),
	"player should be standing in the nave")

-- Give the fresh dungeon instance a moment to finish establishing visibility.
Sleep(3000)

-- Re-arm godmode now that we are on map 1. A cross-map teleport spawns the character into a new
-- world instance -- the world log shows a second "wants to join world" -- and the godmode flag
-- does not survive that. Without this the character is killed by the boss within a couple of
-- seconds; its death drops it from the threat list, which empties the list and resets the
-- encounter to full health long before any threshold is reached.
GM.Godmode(true)

local boss = nil
Assert(WaitUntil(function()
		boss = FindUnitByEntry(SEVRIN_WAX)
		return boss ~= nil
	end, 30000, "Sevrin Wax visible"),
	"the boss should be spawned in the crypt")

TargetUnit(boss)

local maxHealth = GetMaxHealth(boss)
Assert(maxHealth > 0, "boss max health should be known before the pull")
Log("Sevrin Wax is up with " .. maxHealth .. " health")

Assert(not HasAura(boss, RITE_OF_RISING), "the Rite should not be up before the pull")
Assert(not HasAura(boss, UNHALLOWED_FERVOR), "Unhallowed Fervor should not be up before the pull")

-- Drives the boss's health down to `targetPercent` of maximum in a single hit. Reads current
-- health immediately before firing so the boss's own regeneration cannot make the step overshoot
-- into the next threshold -- one hit crossing both 65 and 30 fires both chains at once, which the
-- encounter handles deliberately but which would stop this scenario from testing them apart.
local function DamageDownTo(targetPercent)
	local target = math.floor(maxHealth * targetPercent)
	local current = GetHealth(boss)
	Assert(current > target,
		"boss is already at or below " .. (targetPercent * 100) .. "% (" .. current .. " of " .. maxHealth .. ")")
	GM.DamageTarget(current - target)
	Assert(WaitUntil(function() return GetHealth(boss) <= target end, 15000, "health reaches target"),
		"damage should bring the boss to " .. (targetPercent * 100) .. "%")
end

-- Pull with real auto-attacks so the boss builds a genuine threat list. The character stays on
-- him for the whole fight: every swing keeps threat topped up, and a fight this long is also what
-- exposed the swing timer arming itself twice per swing.
FaceUnit(boss)
StartAttack(boss)
Assert(WaitUntil(function() return GetHealth(boss) < maxHealth end, 30000, "boss took the pull damage"),
	"auto-attacks should connect and open the encounter")

-- Stay in phase 1 long enough for several swings to land before touching the thresholds. The
-- swing that opens combat contributes no threat at all: the boss's combat state connects its
-- `threatened` listener while entering combat, which is after that first swing has already
-- emitted. Racing straight to 65% off the back of one swing therefore reaches the Rite with an
-- empty threat list, and the Rite's StopAutoAttack then resets the boss to full health.
Sleep(10000)
Assert(GetHealth(boss) < maxHealth,
	"the boss should still be engaged after the opening swings, not reset to full health")
Log("Pulled; phase 1 with threat established")

-- === Phase 2 at 65% ===
DamageDownTo(0.64)

Assert(WaitUntil(function() return HasAura(boss, RITE_OF_RISING) end, 20000, "Rite of Rising applied"),
	"crossing 65% should start the Rite of Rising")
Log("Phase 2: the Rite is channelling")

-- The soft-lock risk. The Rite grants 90% damage reduction for its 8s channel; if the aura were
-- ever applied without being removed the boss would stay near-immune for the rest of the fight.
Assert(WaitUntil(function() return not HasAura(boss, RITE_OF_RISING) end, 40000, "Rite of Rising ends"),
	"the Rite must end rather than leaving the damage reduction up permanently")
Log("Phase 2 resolved: the Rite ended and the boss is vulnerable again")

-- It is a one-shot transition, so it must not re-arm while the boss stays inside phase 2.
Sleep(5000)
Assert(not HasAura(boss, RITE_OF_RISING), "the Rite should fire once per crossing, not repeat")

-- === Phase 3 at 30% ===
DamageDownTo(0.29)

Assert(WaitUntil(function() return HasAura(boss, UNHALLOWED_FERVOR) end, 20000, "Unhallowed Fervor applied"),
	"crossing 30% should apply Unhallowed Fervor")
Log("Phase 3: Unhallowed Fervor is up")

-- Unlike the Rite this one is meant to last the rest of the fight.
Sleep(8000)
Assert(HasAura(boss, UNHALLOWED_FERVOR), "Unhallowed Fervor should persist for the rest of the fight")
Assert(IsAlive(boss), "the boss should still be alive at this point in the scenario")

-- === Death and add cleanup ===
local remaining = GetHealth(boss)
Assert(remaining > 0, "boss should still have health before the killing blow")
GM.DamageTarget(remaining + 100)

Assert(WaitUntil(function() return not IsAlive(boss) end, 20000, "boss dies"),
	"the killing blow should register")
Log("Sevrin Wax is down")

-- Trigger 39 despawns whatever the encounter left standing once the boss is no longer alive.
Assert(WaitUntil(function()
		return CountUnitsByEntry(WAX_SEALED_HUSK) == 0 and CountUnitsByEntry(CHOIRBOUND_ACOLYTE) == 0
	end, 20000, "summons despawn"),
	"no husks or acolytes should be left standing after the boss dies")
Log("Adds cleaned up after the kill")

StopAttack()

-- Leave the character on the default map. This scenario owns its character, but the harness
-- retries a timed-out scenario against the same world, and the opening worldport is what that
-- restore is for.
GM.Worldport(SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z, 0)
Assert(WaitUntil(function() return GetPosX(Me()) > 0 end, 30000, "returned to the spawn point"),
	"player should be back at the default spawn")
GM.Godmode(false)

Assert(not IsDisconnected(), "world server should still be alive")
