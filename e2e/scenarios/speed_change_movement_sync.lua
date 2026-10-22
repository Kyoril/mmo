-- Regression: a client the server slows down walks at the new speed, and the server goes on
-- accepting its movement.
--
-- A forced speed change (a slow, a sprint, GM.SetSpeed) must be acked and then applied: a client
-- that acks and runs on at its old speed covers more ground than the server allows, every
-- position packet fails the speed check, and the server kicks it once enough have.

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

local startX = GetPosX(me)
local startY = GetPosY(me)
local startZ = GetPosZ(me)

-- Half speed: the walk takes twice as long, and the speed check must not mind.
GM.SetSpeed(0.5)
Sleep(500)
Assert(MoveTo(startX + 15, startY, startZ, 60000), "should walk 15 yards at half speed")
Assert(not IsDisconnected(), "the server should still accept the slowed client")
assertServerAgrees("half speed")

GM.SetSpeed(1.0)
Sleep(500)
Assert(MoveTo(startX, startY, startZ, 30000), "should walk back at full speed")
Assert(not IsDisconnected(), "the server should still accept the client back at full speed")
assertServerAgrees("full speed")
