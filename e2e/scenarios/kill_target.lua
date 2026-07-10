-- Regression: death pipeline — GM kill, death state replication to clients.
--
-- Uses: Guard (entry 4) — deliberately a creature WITHOUT a combat script,
-- so nothing can interfere with the death (training dummies restore their
-- health when killed while in combat; see training_dummy_immortality.lua).

local GUARD = 4

local guard = GM.CreateMonster(GUARD)
Assert(IsAlive(guard), "guard should spawn alive")

TargetUnit(guard)
GM.KillTarget()

Assert(WaitUntil(function() return not IsAlive(guard) end, 10000, "guard dies"),
	"guard should be dead after GM.KillTarget (health " .. tostring(GetHealth(guard)) .. ")")

Log("Kill verified: guard health is " .. GetHealth(guard))

GM.DestroyMonster(guard)
