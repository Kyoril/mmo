-- Regression: creature combat scripts — the Training Dummy's combat script
-- (TrainingDummyCombatScript, CanDie() == false) must prevent death while the
-- dummy is in combat: a kill attempt restores its health instead.
--
-- Note: the script only exists while the dummy is IN COMBAT (combat scripts are
-- created on combat entry). An idle dummy can be GM-killed - that path is covered
-- by kill_target.lua using a script-less creature instead.

local TRAINING_DUMMY = 40

local dummy = GM.CreateMonster(TRAINING_DUMMY)

-- Step aside (spawn is on top of us) but stay in melee reach, then put the
-- dummy into combat by actually damaging it.
local me = Me()
GM.Worldport(0, GetPosX(me) + 3, GetPosY(me), GetPosZ(me), 0)
FaceUnit(dummy)

local maxHealth = GetMaxHealth(dummy)
TargetUnit(dummy)
StartAttack(dummy)

Assert(WaitUntil(function() return GetHealth(dummy) < maxHealth end, 15000, "dummy enters combat"),
	"dummy should take auto-attack damage so its combat script activates")

-- Keep attacking (combat resets after ~5s without damage) and try to kill it.
GM.KillTarget()

-- The combat script restores the dummy to full health instead of letting it die.
Assert(WaitUntil(function() return GetHealth(dummy) >= maxHealth end, 10000, "health restored"),
	"dummy health should be restored by its combat script (health " .. tostring(GetHealth(dummy)) .. "/" .. maxHealth .. ")")
Assert(IsAlive(dummy), "training dummy must not die while in combat")

StopAttack()
Log("Immortality verified: dummy survived GM kill at " .. GetHealth(dummy) .. "/" .. maxHealth .. " health")

GM.DestroyMonster(dummy)
