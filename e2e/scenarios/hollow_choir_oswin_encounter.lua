-- e2e-own-character: osw
-- e2e-timeout: 240
--
-- Brother Oswin's encounter in the Hollow Choir (map 1), driven through its triggers:
--
--   * at 50 % he casts Last Vigil and two Risen Novices rise (trigger 55),
--   * his death opens the G1 seal to the nave (trigger 56),
--   * and his novices leave once the fight is over (cleanup flag, trigger 58).
--
-- The character is a fresh level-1 test character, so every creature in the hall aggroes on
-- sight; godmode keeps it alive. Damage for the threshold comes from GM.DamageTarget.

local OSWIN = 86
local RISEN_NOVICE = 89
local SEAL = 14
local G1_X, G1_Y, G1_Z = -27.0, 1.2, 0.0

local SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z = 0, 292.267, 5.33, 552.571

GM.Godmode(true)
GM.Worldport(SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z, 0)
Assert(WaitUntil(function() return GetPosX(Me()) > 0 end, 30000, "at the spawn point"),
	"player should start from the default spawn")

-- Land in the Wake of the Dead, south-east of Oswin, clear of melee range.
GM.Worldport(1, 4.0, 0.5, -24.0, 0)
Assert(WaitUntil(function() return GetPosX(Me()) < 100 end, 30000, "arrived in the Wake"),
	"the worldport into the Hollow Choir should land")
Sleep(3000)
-- Godmode does not survive the map change.
GM.Godmode(true)

local oswin = nil
Assert(WaitUntil(function() oswin = FindUnitByEntry(OSWIN); return oswin ~= nil end, 30000, "Oswin visible"),
	"Brother Oswin should be spawned in the Wake")
local maxHealth = GetMaxHealth(oswin)
Assert(WaitUntil(function() return GetHealth(oswin) > 0 end, 30000, "health arrives"),
	"Oswin's current health never replicated")
Assert(WaitUntil(function() return GetHealth(oswin) >= maxHealth end, 60000, "Oswin at full health"),
	"Oswin should be at full health before the pull")
Assert(CountUnitsByEntry(RISEN_NOVICE) == 0, "no novices before the fight")

Assert(MoveTo(GetPosX(oswin), GetPosY(oswin), GetPosZ(oswin), 25000) or GetDistance(Me(), oswin) <= 5.0,
	"should be able to walk up to Oswin")
TargetUnit(oswin)
FaceUnit(oswin)
StartAttack(oswin)
Assert(WaitUntil(function() return GetHealth(oswin) < maxHealth end, 30000, "Oswin pulled"),
	"auto attacks should open the encounter")
Sleep(6000)

-- Just under 50 %: Last Vigil raises two novices.
local target = math.floor(maxHealth * 0.49)
local current = GetHealth(oswin)
Assert(current > target, "Oswin should still be above 50 %")
GM.DamageTarget(current - target)
Assert(WaitUntil(function() return CountUnitsByEntry(RISEN_NOVICE) == 2 end, 15000, "novices rise"),
	"Last Vigil at 50 % should raise two Risen Novices (saw " .. CountUnitsByEntry(RISEN_NOVICE) .. ")")
Log("Last Vigil raised two novices")

-- Death: the G1 seal opens and the novices leave.
TargetUnit(oswin)
GM.DamageTarget(GetHealth(oswin) + 100)
Assert(WaitUntil(function() return not IsAlive(oswin) end, 20000, "Oswin dies"), "the killing blow should land")
StopAttack()
Assert(WaitUntil(function() return CountUnitsByEntry(RISEN_NOVICE) == 0 end, 15000, "novices leave"),
	"the Risen Novices should not outlive Oswin's fight")

GM.Worldport(1, -14.0, 1.7, 3.0, 0)
Sleep(3000)
local seal = nil
Assert(WaitUntil(function() seal = FindNearestObjectByEntry(SEAL, G1_X, G1_Y, G1_Z); return seal ~= nil end,
	20000, "G1 seal visible"), "the G1 seal should be spawned")
Assert(WaitUntil(function() return GetObjectState(seal) == 1 end, 10000, "G1 open"),
	"Oswin's death should open the G1 seal")
Log("G1 is open")

GM.Worldport(SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z, 0)
Assert(WaitUntil(function() return GetPosX(Me()) > 0 end, 30000, "back at the spawn point"),
	"player should be back at the default spawn")
GM.Godmode(false)
Assert(IsAlive(Me()), "the character should survive the scenario")
