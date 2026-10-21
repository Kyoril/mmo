-- e2e-own-character: dms
-- e2e-timeout: 180
--
-- A closed door ends melee between the units on either side of it.
--
-- Melee reach (about 5 m for two units) easily spans a door, and auto-attack swings never checked
-- line of sight: a player could start auto-attacking through a closed Hollow Choir door and pull the
-- creatures behind it. The creatures could not reach the player and never evaded, because they stood
-- inside attack range -- and doors cannot be opened in combat, so the player was stuck in the dungeon.
--
-- Pins both halves of the fix:
--   1. A creature in melee range of its victim, but with a closed door in between, evades.
--   2. An auto-attack swing through a closed door does not land ("line_of_sight" swing error) and
--      does not pull the creature behind it.
--
-- Runs on the Hollow Choir ledge outside the Wake of the Dead's east wall (see
-- hollow_choir_unreachable_evade.lua): an island of the navigation mesh with nothing else on it.
-- Near the town at the default spawn, NPCs joined the fight and muddied who was hitting whom.
--
-- Uses: "E2E Test Door" (object 12, the 6x4 m E2E Test Wall, see door_los_autoclose.lua) and a
-- Restless Novice (unit 95, a hostile Hollow Choir trash mob).

local DOOR = 12
local RESTLESS_NOVICE = 95
local IN_COMBAT = 1

local SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z = 0, 292.267, 5.33, 552.571
local X, Y, Z = 10.2, 1.7, -35.0
-- Either side of the door, 3 m apart: well inside melee reach
local FAR_X, NEAR_X = X - 1.5, X + 1.5

local function InCombat(guid)
	return (GetUnitFlags(guid) & IN_COMBAT) ~= 0
end

GM.Godmode(true)
GM.Worldport(SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z, 0)
Assert(WaitUntil(function() return GetPosX(Me()) > 0 end, 30000, "at the spawn point"),
	"player should start from the default spawn")

GM.Worldport(1, X, Y, Z, 0)
Assert(WaitUntil(function() return GetPosX(Me()) < 100 end, 30000, "on the ledge"),
	"the worldport onto the ledge should land")
Sleep(2000)
GM.Godmode(true)
local me = Me()

local door = GM.CreateObject(DOOR, 0)
Assert(GetObjectState(door) == 0, "door should spawn closed (state 0)")

-- Spawn the novice on the far side, then pull it with a clear view
GM.Worldport(1, FAR_X, Y, Z, 0)
Sleep(500)
local novice = GM.CreateMonster(RESTLESS_NOVICE)
Assert(novice ~= nil, "the novice should spawn")
Assert(WaitUntil(function() return GetHealth(novice) > 0 end, 10000, "novice health"),
	"the novice's health should replicate")
TargetUnit(novice)
GM.DamageTarget(1)
Assert(WaitUntil(function() return InCombat(novice) end, 10000, "novice in combat"),
	"the novice should be in combat after being hit")

-- 1. Step through to the near side. The novice stays within melee range, but the door is in between.
GM.Worldport(1, NEAR_X, Y, Z, 0)
Sleep(1000)
GM.Godmode(true)
Assert(not GM.CheckLoS(novice), "the closed door should block line of sight to the novice")
Log("near side; novice " .. string.format("%.1f", GetDistance(me, novice)) .. " away")

Assert(WaitUntil(function() return not InCombat(novice) end, 30000, "novice evaded"),
	"a creature in melee range but behind a closed door should evade")
Assert(WaitUntil(function() return GetHealth(novice) >= GetMaxHealth(novice) end, 15000, "novice healed"),
	"the evading novice should heal to full")
Assert(WaitUntil(function() return not InCombat(me) end, 15000, "player out of combat"),
	"the player should leave combat once the novice evaded")

-- 2. Auto-attack it through the door.
Assert(GetDistance(me, novice) < 4.5, "the novice should be within melee reach (distance "
	.. string.format("%.1f", GetDistance(me, novice)) .. ")")
local swingsBefore = MeleeSwingCount(novice)
TargetUnit(novice)
FaceUnit(novice)
StartAttack(novice)

Assert(WaitUntil(function() return LastSwingError() == "line_of_sight" end, 10000, "line of sight swing error"),
	"a swing through a closed door should report line_of_sight, was " .. LastSwingError())
Sleep(3000)
Assert(MeleeSwingCount(novice) == swingsBefore, "no swing should land through the closed door")
Assert(not InCombat(novice), "attacking through the door must not pull the novice")
Assert(GetHealth(novice) >= GetMaxHealth(novice), "the novice should be untouched")

StopAttack()
GM.DestroyMonster(novice)
GM.DestroyObject(door)
Assert(WaitUntil(function() return FindObjectByEntry(DOOR) == nil end, 10000, "door despawned"),
	"door should despawn after GM.DestroyObject")

GM.Worldport(SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z, 0)
Assert(WaitUntil(function() return GetPosX(Me()) > 0 end, 30000, "back at the spawn point"),
	"player should be back at the default spawn")
GM.Godmode(false)
Assert(IsAlive(Me()), "the character should survive the scenario")
Log("Melee through a closed door neither lands nor keeps a fight alive")
