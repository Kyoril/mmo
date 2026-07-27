-- Door mechanics: a closed door blocks server-side line of sight, opening it via
-- the OpenLock spell path clears the block, and the door's auto close timer
-- restores it.
--
-- Uses: "E2E Test Door" (object entry 12, type Door, display 7 = a 6x4 m wall
-- mesh with a baked collision tree, data = [no lock, no post-unlock lock, 5000ms
-- auto close]) and the generic "Open" spell (39, OpenLock, lock 0, 5s cast).
-- The Training Dummy (40) on the far side is the LoS probe target — LoS is always
-- checked against the dummy, never the door itself, since the door's own geometry
-- occludes its center.

local DOOR = 12
local OPEN = 39
local TRAINING_DUMMY = 40

-- Flat, empty spot on map 0 (same area the escort scenario uses).
local X, Y, Z = -436, -1, 244

local me = Me()

GM.LearnSpell(OPEN)
Assert(WaitUntil(function() return HasSpell(OPEN) end, 10000, "Open learned"),
	"player should know the Open spell after GM.LearnSpell")

-- Spawn the door (closed) at the anchor point.
GM.Worldport(0, X, Y, Z, 0)
local door = GM.CreateObject(DOOR, 0)
Log("Door spawned: " .. door)
Assert(GetObjectState(door) == 0, "door should spawn closed (state 0)")

-- Spawn the LoS probe dummy on the far side of the door.
GM.Worldport(0, X - 6, Y, Z, 0)
local dummy = GM.CreateMonster(TRAINING_DUMMY)
Log("LoS probe dummy spawned: " .. dummy)

-- Step to the near side: the closed door now stands between player and dummy.
GM.Worldport(0, X + 6, Y, Z, 0)
Assert(not GM.CheckLoS(dummy), "closed door should block line of sight to the dummy")

-- Move into interaction range and open the door through the OpenLock spell path.
GM.Worldport(0, X + 3, Y, Z, 0)
Assert(CastSpellOnObject(OPEN, door), "Open cast request should be accepted")
Assert(WaitUntil(function() return GetObjectState(door) == 1 end, 15000, "door opened"),
	"door state should replicate as open after the Open cast (cast time 5s)")

Assert(GM.CheckLoS(dummy), "open door should not block line of sight to the dummy")

-- The door closes itself after 5s and the LoS block returns.
Assert(WaitUntil(function() return GetObjectState(door) == 0 end, 15000, "auto close"),
	"door should close itself after the 5s auto close time")
Assert(not GM.CheckLoS(dummy), "auto-closed door should block line of sight again")

GM.DestroyMonster(dummy)
Log("Door LoS + auto close cycle verified")
