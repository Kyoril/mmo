-- e2e-own-character: hcb
-- e2e-timeout: 240
--
-- Hollow Choir G2 barrier (bug repro): while Sister Mereth lives, the magic barriers at x -85.5
-- must keep creatures on the nave side (x > -85.5). The apse behind them (x < -85.5) is Cantor
-- Veyr's. Dragging a hostile mob up the stair to the barrier used to let it path through (the nav
-- mesh did not know about doors), where Veyr assisted it and could be pulled without Mereth ever
-- dying.

local VEYR, HOLLOW_CHORISTER = 88, 91
local BARRIER_X = -85.5
local NAVE_X, NAVE_Y, NAVE_Z = -75.0, 1.0, 0.0
local APSE_X, STAIR_Y = -91.0, 5.7

GM.Godmode(true)
GM.Worldport(1, NAVE_X, NAVE_Y, NAVE_Z, 0)
Assert(WaitUntil(function() local x = GetPosX(Me()); return x > BARRIER_X and x < -32 end, 30000, "in the nave"),
	"the worldport into the nave should land east of the G2 barrier")
Sleep(3000)
GM.Godmode(true)

-- Veyr may be out of view range from the nave; check him only if he is visible.
local veyr = nil
local veyrMax = nil
if WaitUntil(function() veyr = FindUnitByEntry(VEYR); return veyr ~= nil end, 5000, "Veyr visible") then
	veyrMax = GetMaxHealth(veyr)
else
	Log("Veyr not visible from the nave; asserting on the mob only")
end

-- A hostile Veyr ally, engaged on the nave side.
local mob = GM.CreateMonster(HOLLOW_CHORISTER)
-- The monster spawns on top of us and melee needs the in-front check to pass, so step aside
-- (still inside melee reach) and face it.
GM.Worldport(1, NAVE_X + 2, NAVE_Y, NAVE_Z, 0)
Sleep(1000)
FaceUnit(mob)
TargetUnit(mob)
StartAttack(mob)
Assert(WaitUntil(function() return GetHealth(mob) < GetMaxHealth(mob) end, 15000, "mob engaged"),
	"auto attacks should put the chorister in combat with us")

-- Drag it: once it is fighting, teleport behind the barrier, out of melee reach of the mob's
-- side. Without the door-aware nav filter the mob paths through the closed barrier (x -86.5
-- measured); with it, there is no path and the mob stays put.
GM.Worldport(1, APSE_X, STAIR_Y, 0.0, 0)
Sleep(2000)
GM.Godmode(true)
local crossed = false
local deadline = 40
for i = 1, deadline do
	Sleep(1000)
	if i % 4 == 0 then
		Log(string.format("t=%d me=(%.1f, %.1f, %.1f) mob=(%.1f, %.1f, %.1f)", i, GetPosX(Me()), GetPosY(Me()), GetPosZ(Me()), GetPosX(mob), GetPosY(mob), GetPosZ(mob)))
	end
	if GetPosX(mob) < BARRIER_X - 0.3 then
		crossed = true
		break
	end
end
StopAttack()

Assert(not crossed, "the chorister must not path through the closed G2 barrier (x " .. GetPosX(mob) .. ")")
if veyr then
	Assert(GetHealth(veyr) >= veyrMax, "Veyr must not be pulled through the closed barrier")
end
Log("G2 barrier held for " .. deadline .. " s")

GM.DestroyMonster(mob)
