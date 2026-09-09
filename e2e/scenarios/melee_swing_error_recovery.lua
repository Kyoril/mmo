-- Regression: an auto-attack swing error must be *cleared* once swings land again.
--
-- Swing outcomes go on the wire as transitions and the client repeats the last one it was told
-- about until it hears a different one. The server used to swallow the successful swing, so a
-- single out-of-range swing left the client repeating "out of range" -- error message and voice
-- line -- forever, straight through the attacks that were connecting.
--
-- Uses: Training Dummy (entry 40).

local TRAINING_DUMMY = 40

local dummy = GM.CreateMonster(TRAINING_DUMMY)
local me = Me()
local dummyX, dummyY, dummyZ = GetPosX(dummy), GetPosY(dummy), GetPosZ(dummy)

-- Attack it from well outside melee reach. The swing resolves, fails and retries several times a
-- second, but only the first failure is reported. Ported rather than walked: the character starts
-- wherever the previous scenario left it, so there is no promise of a walkable path here.
GM.Worldport(0, dummyX + 25, dummyY, dummyZ, 0)
Assert(GetDistance(me, dummy) > 10, "should be out of melee reach (distance "
	.. tostring(GetDistance(me, dummy)) .. ")")

TargetUnit(dummy)
StartAttack(dummy)

Assert(WaitUntil(function() return SwingErrorCount() > 0 end, 15000, "swing error arrives"),
	"attacking from out of range should report a swing error")
Assert(LastSwingError() == "out_of_range",
	"swing error should be out_of_range, was " .. LastSwingError())
Assert(SwingRecoveryCount() == 0, "nothing has landed yet, so there is nothing to recover from")

-- Back into reach without stopping the attack. The swings start connecting on their own.
GM.Worldport(0, dummyX + 3, dummyY, dummyZ, 0)
FaceUnit(dummy)

Assert(WaitUntil(function() return MeleeSwingCount(dummy) > 0 end, 15000, "melee swing lands"),
	"auto-attack should resolve a swing once back in range")

-- The point of the test: the server has to say so, or the client never stops complaining.
Assert(WaitUntil(function() return SwingRecoveryCount() > 0 end, 5000, "swing error cleared"),
	"a landing swing must clear the out-of-range error (swings resolved: "
		.. MeleeSwingCount(dummy) .. ", recoveries: " .. SwingRecoveryCount() .. ")")

-- And it has to say it once, not once per swing: the repeat is the client's job. Measured as a
-- steady state rather than an exact count -- the port back into reach can land a swing before the
-- facing correction arrives, which legitimately costs one extra error/recovery pair.
local errorsAtSteady = SwingErrorCount()
local recoveriesAtSteady = SwingRecoveryCount()
local swingsAtSteady = MeleeSwingCount(dummy)
Sleep(3000)

Assert(MeleeSwingCount(dummy) > swingsAtSteady,
	"the fight should still be landing swings, or the checks below prove nothing")
Assert(SwingErrorCount() == errorsAtSteady,
	"a fight that keeps landing must not report further swing errors")
Assert(SwingRecoveryCount() == recoveriesAtSteady,
	"the recovery is a transition, not one report per landing swing")

StopAttack()
GM.DestroyMonster(dummy)
