-- e2e-own-character: hcb
-- e2e-timeout: 240
--
-- Hollow Choir G2 barrier (bug repro): while Sister Mereth lives, the magic barriers at x -85.5
-- must keep creatures on the crypt side. Dragging a hostile mob to the barrier used to let it path
-- through (the nav mesh did not know about doors), where Cantor Veyr assisted it and could be
-- pulled without Mereth ever dying.

local VEYR, HOLLOW_CHORISTER = 88, 91
local BARRIER_X = -85.5
local CRYPT_SIDE_X, BARRIER_Y, BARRIER_Z = -98.0, 5.7, 0.0

GM.Godmode(true)
GM.Worldport(1, CRYPT_SIDE_X, BARRIER_Y, BARRIER_Z, 0)
Assert(WaitUntil(function() return GetPosX(Me()) < -90 end, 30000, "at the G2 barrier"),
	"the worldport next to the G2 barrier should land")
Sleep(3000)
GM.Godmode(true)

local veyr = nil
Assert(WaitUntil(function() veyr = FindUnitByEntry(VEYR); return veyr ~= nil end, 30000, "Veyr visible"),
	"Cantor Veyr should be spawned behind the barrier")
local veyrMax = GetMaxHealth(veyr)
Assert(WaitUntil(function() return GetHealth(veyr) >= veyrMax end, 30000, "Veyr at full health"),
	"Veyr should start untouched")

-- A hostile Veyr ally, engaged on the crypt side.
local mob = GM.CreateMonster(HOLLOW_CHORISTER)
TargetUnit(mob)
StartAttack(mob)
Assert(WaitUntil(function() return GetHealth(mob) < GetMaxHealth(mob) end, 15000, "mob engaged"),
	"auto attacks should put the chorister in combat with us")

-- Stand right at the barrier, so the mob's chase slots fall on the far side, and hold there.
MoveTo(BARRIER_X - 1.0, BARRIER_Y, BARRIER_Z, 15000)
local crossed = false
local deadline = 20
for _ = 1, deadline do
	Sleep(1000)
	if GetPosX(mob) > BARRIER_X + 0.3 then
		crossed = true
		break
	end
end
StopAttack()

Assert(not crossed, "the chorister must not path through the closed G2 barrier (x " .. GetPosX(mob) .. ")")
Assert(GetHealth(veyr) >= veyrMax, "Veyr must not be pulled through the closed barrier")
Log("G2 barrier held for " .. deadline .. " s")

GM.DestroyMonster(mob)
