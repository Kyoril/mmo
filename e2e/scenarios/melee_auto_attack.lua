-- Regression: melee auto-attack pipeline — swing timer, melee validation
-- (range/facing) and attacker state updates.
--
-- Uses: Training Dummy (entry 40).

local TRAINING_DUMMY = 40

local dummy = GM.CreateMonster(TRAINING_DUMMY)

-- Step a short distance aside (monster spawns on top of us; melee also needs
-- the in-front check to pass) but stay well within melee reach.
local me = Me()
GM.Worldport(0, GetPosX(me) + 3, GetPosY(me), GetPosZ(me), 0)
FaceUnit(dummy)

local startHealth = GetHealth(dummy)
Assert(startHealth > 0, "dummy should be alive before the attack")

TargetUnit(dummy)
StartAttack(dummy)

-- Auto-attack swings are on a weapon timer; give a few swings worth of time.
Assert(WaitUntil(function() return GetHealth(dummy) < startHealth end, 15000, "melee swing lands"),
	"dummy health should drop from auto-attack (start " .. startHealth .. ", now " .. tostring(GetHealth(dummy)) .. ")")

StopAttack()
Log("Auto-attack dealt " .. (startHealth - GetHealth(dummy)) .. " damage")

GM.DestroyMonster(dummy)
