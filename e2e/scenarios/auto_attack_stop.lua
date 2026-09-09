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

-- Second half: a swing that finds its target already dead must stop the attack, not just drop
-- the victim. GameUnitS::ExecuteAutoAttackSwing used to answer that case with
-- AttackSwingEvent::TargetDead and a bare SetVictim(nullptr), which left unit_flags::Attacking
-- set behind it -- the flag the client reads as IsWeaponDrawn() -- and sent no AttackStop, the
-- packet that is the client's only acknowledgement. Nothing cleared it afterwards either: since
-- SetVictim(nullptr) disconnects the victim signals, not even the corpse despawning would.
--
-- No StopAttack() is sent here on purpose. A player could paper over the state by toggling attack
-- off, which is what this phase used to assert; a creature cannot, because there is no client
-- behind it to send the request. So the property under test is that the server stops on its own.
--
-- The state is reachable only by attacking something already dead: GameUnitS::OnKilled fires the
-- killed signal, so every attacker of a unit that dies under them runs StopAttack, but
-- StartAttack checks visibility and friendliness and never aliveness.
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

-- The accept and the stop land inside a tick or two of each other -- the opening swing arms
-- immediately, measured at ~15ms -- so IsAutoAttacking() has no true window wide enough to wait
-- on. The swing error is the durable witness instead: TargetDead can only come from a swing the
-- server accepted and resolved against this corpse, so it rules out the assertion below passing
-- merely because the attack was refused outright.
--
-- The count is checked alongside the name because LastSwingError() is sticky for the life of the
-- scenario. Nothing above can leave a target_dead behind today -- the dummy in the first phase
-- cannot die -- but that is luck, not construction, and a phase added in between would silently
-- turn this witness into a no-op.
local errorsBeforeCorpse = SwingErrorCount()
StartAttack(corpse)

Assert(WaitUntil(function()
			return SwingErrorCount() > errorsBeforeCorpse and LastSwingError() == "target_dead"
		end, 8000, "server reports the dead target"),
	"the server should accept an attack on a corpse -- StartAttack never checks aliveness -- and "
		.. "report TargetDead on the swing that follows")

Assert(WaitUntil(function() return not IsAutoAttacking() end, 4000,
		"server stops the attack on the dead target by itself"),
	"the swing that finds its target dead must stop the attack and broadcast AttackStop without "
		.. "being asked -- a creature has no client to ask on its behalf, so anything left set here "
		.. "stays set")

Log("Server stopped the attack on the corpse by itself")

GM.DestroyMonster(corpse)
