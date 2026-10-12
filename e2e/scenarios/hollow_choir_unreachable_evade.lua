-- e2e-own-character: evd
-- e2e-timeout: 180
--
-- A creature whose victim stands where its paths do not lead evades instead of waiting at the edge
-- of its area forever (UnreachableTargetTracker).
--
-- A Restless Novice is spawned on a ledge outside the Wake of the Dead's east wall: an island of
-- the navigation mesh, unconnected to the rest of the dungeon. It is pulled and wounded there, then
-- the player moves into the Wake's east aisle, well out of melee reach and with no path leading
-- there from the ledge. The novice's chase paths end on its ledge; after a few seconds it evades,
-- walks home and is healed to full.

local RESTLESS_NOVICE = 95

local SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z = 0, 292.267, 5.33, 552.571
local LEDGE_X, LEDGE_Y, LEDGE_Z = 10.2, 1.7, -35.0
-- The Wake's east aisle: 12 units from the ledge, the wall in between
local AISLE_X, AISLE_Y, AISLE_Z = 3.0, 1.7, -25.0

GM.Godmode(true)
GM.Worldport(SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z, 0)
Assert(WaitUntil(function() return GetPosX(Me()) > 0 end, 30000, "at the spawn point"),
	"player should start from the default spawn")

GM.Worldport(1, LEDGE_X, LEDGE_Y, LEDGE_Z, 0)
Assert(WaitUntil(function() return GetPosX(Me()) < 100 end, 30000, "on the ledge"),
	"the worldport onto the ledge should land")
Sleep(2000)
GM.Godmode(true)

local novice = GM.CreateMonster(RESTLESS_NOVICE)
Assert(novice ~= nil, "the novice should spawn on the ledge")
Assert(WaitUntil(function() return GetHealth(novice) > 0 end, 10000, "novice health"), "the novice's health should replicate")
local maxHealth = GetMaxHealth(novice)

TargetUnit(novice)
GM.DamageTarget(math.floor(maxHealth * 0.3))
Assert(WaitUntil(function() return GetHealth(novice) < maxHealth * 0.8 end, 10000, "novice wounded"),
	"the damage should land and pull the novice")

-- Out of its reach, where no path from the ledge leads
GM.Worldport(1, AISLE_X, AISLE_Y, AISLE_Z, 0)
Sleep(1000)
GM.Godmode(true)
Log("in the aisle; novice at " .. GetHealth(novice) .. " / " .. maxHealth .. ", " .. string.format("%.1f", GetDistance(Me(), novice)) .. " away")

Assert(WaitUntil(function() return GetHealth(novice) >= maxHealth end, 30000, "novice evaded"),
	"the novice should evade and heal once it cannot reach the player (health " .. GetHealth(novice) .. " / " .. maxHealth .. ")")
Log("the novice evaded and is back at full health, " .. string.format("%.1f", GetDistance(Me(), novice)) .. " away")
Assert(GetDistance(Me(), novice) > 6.0, "the novice should still be on its ledge")

GM.DestroyMonster(novice)
GM.Worldport(SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z, 0)
Assert(WaitUntil(function() return GetPosX(Me()) > 0 end, 30000, "back at the spawn point"),
	"player should be back at the default spawn")
GM.Godmode(false)
Assert(IsAlive(Me()), "the character should survive the scenario")
