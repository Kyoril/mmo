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
local STAIR_X, STAIR_Y = -84.5, 5.2

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
TargetUnit(mob)
StartAttack(mob)
Assert(WaitUntil(function() return GetHealth(mob) < GetMaxHealth(mob) end, 15000, "mob engaged"),
	"auto attacks should put the chorister in combat with us")

-- Stand right at the barrier, so the mob's chase slots fall on the far side, and hold there.
MoveTo(STAIR_X, STAIR_Y, 0.0, 15000)
local crossed = false
local deadline = 40
local zs = { -2.0, 2.0 }
for i = 1, deadline do
	if i % 8 == 0 then
		MoveTo(STAIR_X, STAIR_Y, zs[(i / 8) % 2 + 1], 5000)
	end
	Sleep(1000)
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
