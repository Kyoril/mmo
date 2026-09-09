-- e2e-own-character: stp
-- e2e-timeout: 180
--
-- Regression: the client asking to stop auto-attacking must actually stop it on the server.
--
-- Player::OnAttackStop read the client timestamp out of the packet and returned. Nothing else in
-- the world server calls GameUnitS::StopAttack in response to a player request -- the only callers
-- are creature AI, triggers, and internal paths (victim killed, target cleared) -- so a player who
-- toggled auto-attack off kept swinging until the target died or left sight.
--
-- Two things are asserted, because the packet does two jobs:
--
--   1. Swings stop. MeleeSwingCount counts swings the server resolved, hit or miss. Health cannot
--      stand in for it: a swing can be dodged, and TrainingDummyCombatScript heals the dummy to
--      full after 5000ms without taking damage, so "health stopped dropping" is also what a
--      working swing timer looks like.
--   2. The AttackStop broadcast arrives. It is the only thing that clears the client's own state --
--      the swing-error UI keeps replaying its last error until this packet lands, and weapons stay
--      drawn. IsAutoAttacking() is driven purely by the broadcast packets, never by the request we
--      sent, so it answers "did the server acknowledge?" rather than "did we ask?".
--
-- Uses the Training Dummy (entry 40): it never fights back and cannot die while in combat, so the
-- fight lasts as long as the scenario needs without the victim-died path stopping the attack for
-- reasons that have nothing to do with the packet under test.

local TRAINING_DUMMY = 40

-- An unarmed level-10 character swings about every 2900ms. Watch for three intervals: with the
-- handler doing nothing, three more swings land inside this window.
local SETTLE_MS = 9000

-- A swing already due the instant the request arrives may still resolve, so take the baseline a
-- moment after asking rather than at the request itself. Anything after this is the bug.
local ACK_GRACE_MS = 1000

GM.LevelUp(9)

local dummy = GM.CreateMonster(TRAINING_DUMMY)

-- The monster spawns on top of us and melee needs the in-front check to pass, so step aside while
-- staying well inside melee reach.
local me = Me()
GM.Worldport(0, GetPosX(me) + 3, GetPosY(me), GetPosZ(me), 0)
FaceUnit(dummy)
TargetUnit(dummy)

Assert(GetHealth(dummy) > 0, "dummy should be alive before the attack")

StartAttack(dummy)

-- Baseline: melee has to connect at all, or "no swings after the stop" would pass for reasons that
-- have nothing to do with the stop request.
Assert(WaitUntil(function() return MeleeSwingCount(dummy) > 0 end, 15000, "opening swing lands"),
	"auto-attack should connect before asking the server to stop")
Assert(IsAutoAttacking(), "the server should have acknowledged the attack start")
Log("Opening swing resolved, dummy at " .. GetHealth(dummy) .. " of " .. GetMaxHealth(dummy))

StopAttack()

Assert(WaitUntil(function() return not IsAutoAttacking() end, ACK_GRACE_MS + 4000,
		"server broadcasts AttackStop"),
	"the server must answer the stop request with the AttackStop packet -- without it the client "
		.. "never clears its swing-error state and never lowers its weapons")

Sleep(ACK_GRACE_MS)
local swingsAtStop = MeleeSwingCount(dummy)

Sleep(SETTLE_MS)

local swingsAfterStop = MeleeSwingCount(dummy) - swingsAtStop
Assert(swingsAfterStop == 0,
	"auto-attack must stop when the client asks: " .. swingsAfterStop .. " more swings resolved in "
		.. SETTLE_MS .. "ms after StopAttack (expected none)")

Log("No swings resolved in " .. SETTLE_MS .. "ms after the stop request, dummy at "
	.. GetHealth(dummy) .. " health")

GM.DestroyMonster(dummy)

-- Second half: the same request in the one state where the server has no victim but still counts
-- as attacking. GameUnitS::TriggerNextAutoAttack answers a swing whose target is already dead
-- with AttackSwingEvent::TargetDead and a bare m_victim.reset() -- no StopAttack -- so the
-- Attacking unit flag stays set and the weapons stay drawn. Killing your own victim does not
-- reach it (GameUnitS::VictimKilled runs StopAttack); starting an attack on something already
-- dead does, because StartAttack checks visibility and friendliness but never aliveness.
--
-- This is the branch that costs the handler its second condition, so it is the branch that has
-- to be pinned: a guard narrowed to "is there a victim" passes everything above and fails here.
--
-- A second dummy rather than the one above, and killed before it ever fights: TrainingDummy-
-- CombatScript is instantiated on combat entry and protects the dummy from dying, so only an
-- idle one can be killed. The Guard (entry 4) cannot stand in for it -- it is friendly to the
-- test character, and StartAttack answers a friendly target by stopping instead of starting.
local corpse = GM.CreateMonster(TRAINING_DUMMY)
TargetUnit(corpse)
GM.KillTarget()
Assert(WaitUntil(function() return not IsAlive(corpse) end, 10000, "idle dummy dies"),
	"an idle training dummy should die to GM.KillTarget -- its combat script only protects it once"
		.. " the encounter has started")

StartAttack(corpse)
Assert(WaitUntil(function() return IsAutoAttacking() end, 5000, "corpse attack starts"),
	"the server accepts an attack on a corpse -- StartAttack never checks aliveness -- so it should"
		.. " acknowledge this one")

-- Let the first swing resolve into TargetDead and clear the victim server-side. The opening swing
-- arms immediately (measured at ~15ms), so this wait is already generous; without it the victim
-- might still be set and the assertion below would pass on the other branch of the guard.
Sleep(3000)

StopAttack()

Assert(WaitUntil(function() return not IsAutoAttacking() end, 4000,
		"server broadcasts AttackStop for the corpse"),
	"a player left attacking a corpse still has the Attacking flag set and no victim, and the stop "
		.. "request must still be answered -- otherwise the weapons never come down")

Log("Stop request acknowledged with no victim set, only the Attacking flag")

GM.DestroyMonster(corpse)
