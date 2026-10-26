-- Regression: after a teleport - within the map and across maps - the server goes on accepting
-- the client's movement.
--
-- The server puts every teleported character into the falling state and only lets a landing
-- packet take it out again. A client that walks off without landing first has every movement
-- packet refused ("Client tried to remove FALLING flag in non-jump packet"): it walks on, while
-- for the server it never left the teleport spot. Nothing disconnects, so the only way to see it
-- is to ask the server where the character is - a creature the GM spawns appears at the
-- position the server holds for them.

local me = Me()

local function assertServerAgrees(label)
	local dummy = GM.CreateMonster(40)
	Assert(dummy ~= nil, label .. ": the training dummy should spawn")

	local distance = GetDistance(me, dummy)
	Log(string.format("%s: client at (%.1f, %.1f, %.1f), dummy spawned %.1f away",
		label, GetPosX(me), GetPosY(me), GetPosZ(me), distance))
	Assert(distance >= 0 and distance < 3,
		label .. ": the server should hold the position the client walked to, but its spawn is "
		.. tostring(distance) .. " away")

	GM.DestroyMonster(dummy)
end

-- Within the map: a teleport ack, then a walk.
local startX = GetPosX(me)
local startY = GetPosY(me)
local startZ = GetPosZ(me)

GM.Worldport(0, startX + 5, startY, startZ, 0)
Assert(MoveTo(startX + 15, startY, startZ, 30000), "should walk away from the same-map teleport spot")
assertServerAgrees("same map")

-- Across maps: into the Hollow Choir, a transfer to another world instance, then a walk.
GM.Worldport(1, 5, 1, 0, 0)
Assert(MoveTo(-10, 1.06, 2.1, 30000), "should walk away from the dungeon entrance")
assertServerAgrees("other map")
