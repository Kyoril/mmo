-- Door mechanics: a closed door blocks server-side line of sight, opening it via
-- the OpenLock spell path clears the block, and the door's auto close timer
-- restores it.
--
-- Uses: "E2E Test Door" (object entry 12, type Door, display 7 = a 6x4 m wall
-- mesh with a baked collision tree, data = [Door lock type 3, no post-unlock lock,
-- 5000ms auto close]) and the instant "Open Door" spell (235, OpenLock vs lock
-- type 3, level-1 class spell for every class — doors open without a cast bar,
-- unlike the generic 5s "Open" spell 39 used for chests).
-- The Training Dummy (40) on the far side is the LoS probe target — LoS is always
-- checked against the dummy, never the door itself, since the door's own geometry
-- occludes its center.

local DOOR = 12
local OPEN_DOOR = 235
local TRAINING_DUMMY = 40

-- Hostile-free flat spot near the default character spawn (the same area the combat
-- smoke scenarios use undisturbed). Do NOT anchor near quest content: the escort-quest
-- area at (-436, -1, 244) has ambush/hostile spawns that wander on warm instances and
-- killed the level-1 caster mid-cast in earlier revisions of this scenario. Scenarios
-- also share one server run AND the character's last position, so this scenario must
-- destroy its wall-sized door before ending (see cleanup at the bottom) and leave the
-- character on valid ground.
local X, Y, Z = 312, 5.33, 552

local me = Me()

-- Open Door is a level-1 class spell, so a fresh character should already know it —
-- assert that instead of GM-learning it, so the class-grant data stays regression-tested.
Assert(WaitUntil(function() return HasSpell(OPEN_DOOR) end, 10000, "Open Door known"),
	"every class should know the Open Door spell from level 1")

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
Assert(CastSpellOnObject(OPEN_DOOR, door), "Open Door cast request should be accepted")
Assert(WaitUntil(function() return GetObjectState(door) == 1 end, 10000, "door opened"),
	"door state should replicate as open after the instant Open Door cast, cast result: "
	.. LastCastResult() .. ", hp: " .. GetHealth(me))

Assert(GM.CheckLoS(dummy), "open door should not block line of sight to the dummy")

-- The door closes itself after 5s and the LoS block returns.
Assert(WaitUntil(function() return GetObjectState(door) == 0 end, 15000, "auto close"),
	"door should close itself after the 5s auto close time")
Assert(not GM.CheckLoS(dummy), "auto-closed door should block line of sight again")

-- Environmental interference (a wandering hostile killing the caster) invalidates the
-- whole run and, worse, leaves later scenarios a dead character — fail loudly instead.
Assert(IsAlive(me), "character should survive the door cycle (hp " .. GetHealth(me) .. ")")

-- Clean up both temporaries — scenarios share one server run, and a leftover
-- wall-sized door would silently distort later scenarios' line-of-sight checks.
GM.DestroyMonster(dummy)
GM.DestroyObject(door)
Assert(WaitUntil(function() return FindObjectByEntry(DOOR) == nil end, 10000, "door despawned"),
	"door should despawn after GM.DestroyObject")

-- Return the character to the default spawn point: later scenarios move RELATIVE to
-- wherever the character stands, and leaving it out here pushes them off the nav mesh
-- (move_to) or behind static geometry (spell LoS failures).
GM.Worldport(0, 292.267, 5.33, 552.571, 0)

Log("Door LoS + auto close cycle verified")
