-- Regression: client movement pipeline — nav-mesh pathing, movement packets
-- passing server-side anti-cheat validation, position replication.

local me = Me()

local startX = GetPosX(me)
local startY = GetPosY(me)
local startZ = GetPosZ(me)

-- Walk a short distance; the spawn area is open terrain.
local targetX = startX + 10

Assert(MoveTo(targetX, startY, startZ, 30000), "player should reach a walkable point 10 units away")

-- The client is authoritative for its own position; the server validated every
-- movement packet on the way (a rejection would have failed the scenario with a
-- disconnect). Verify we actually ended up near the target.
Assert(math.abs(GetPosX(me) - targetX) < 3,
	"player should stand near the target x (wanted " .. targetX .. ", got " .. tostring(GetPosX(me)) .. ")")

Log(string.format("Moved from (%.1f, %.1f, %.1f) to (%.1f, %.1f, %.1f)",
	startX, startY, startZ, GetPosX(me), GetPosY(me), GetPosZ(me)))
