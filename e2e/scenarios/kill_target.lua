-- Regression: death pipeline — GM kill, death state replication to clients.
--
-- Uses: Training Dummy (entry 40).

local TRAINING_DUMMY = 40

local dummy = GM.CreateMonster(TRAINING_DUMMY)
Assert(IsAlive(dummy), "dummy should spawn alive")

TargetUnit(dummy)
GM.KillTarget()

Assert(WaitUntil(function() return not IsAlive(dummy) end, 10000, "dummy dies"),
	"dummy should be dead after GM.KillTarget (health " .. tostring(GetHealth(dummy)) .. ")")

Log("Kill verified: dummy health is " .. GetHealth(dummy))

GM.DestroyMonster(dummy)
