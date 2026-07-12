-- e2e-class: 1
--
-- Regression: Charge spell movement handshake — the server announces the charge
-- (MoveCharge), the client acks (MoveChargeAck) and only then does the server move
-- the character to the target. If the handshake breaks, the character never moves
-- and the follow-up melee swing can't land from 15 units away.
--
-- Note: the headless bot doesn't simulate the movement path (CreatureMove is
-- ignored), so self-position queries stay at the pre-charge spot. The melee swing
-- landing is the observable proof that the server-side character arrived in reach.
--
-- Uses: Charge (spell 48, warrior only — hence the e2e-class directive above)
-- against a Training Dummy (entry 40, attackable; the Guard is friendly and a
-- TargetEnemy effect refuses friendly targets).

local CHARGE = 48
local TRAINING_DUMMY = 40

-- Charge has a level requirement; make sure the fresh level-1 character meets it.
local me = Me()
if GetLevel(me) < 10 then
	GM.LevelUp(10 - GetLevel(me))
	Assert(WaitUntil(function() return GetLevel(me) >= 10 end, 10000, "leveled up for Charge"),
		"player should reach level 10 for the Charge level requirement")
end

GM.LearnSpell(CHARGE)
Assert(WaitUntil(function() return HasSpell(CHARGE) end, 10000, "Charge learned"),
	"player should know Charge after GM.LearnSpell")

local dummy = GM.CreateMonster(TRAINING_DUMMY)
Log("Training dummy spawned: " .. dummy)

local startHealth = GetHealth(dummy)
Assert(startHealth > 0, "training dummy should spawn alive (health " .. tostring(startHealth) .. ")")

-- The monster spawns exactly at the player's position; step well outside melee
-- reach so only a successful charge can bring us back into range.
GM.Worldport(0, GetPosX(me) + 15, GetPosY(me), GetPosZ(me), 0)
Assert(WaitUntil(function() return GetDistance(me, dummy) > 12 end, 10000, "teleported away from dummy"),
	"player should be well out of melee reach after worldport")

TargetUnit(dummy)
Assert(CastSpell(CHARGE, dummy), "Charge cast request should be accepted")

Assert(WaitUntil(function()
	local result = LastCastResult()
	return result == "ok" or result:find("failed") ~= nil
end, 10000, "Charge cast finished"), "cast should finish within 10s")

Assert(LastCastResult() == "ok", "Charge cast should succeed, got: " .. LastCastResult())

-- The charge covers ~15 units at 35 units/second (< 1s), but only after the
-- MoveCharge/MoveChargeAck round trip. Auto-attack only lands once the server-side
-- character is in melee reach of the dummy.
StartAttack(dummy)
Assert(WaitUntil(function() return GetHealth(dummy) < startHealth end, 15000, "melee swing lands after charge"),
	"dummy health should drop from auto-attack after charging into melee reach (start "
	.. startHealth .. ", now " .. tostring(GetHealth(dummy)) .. ")")

StopAttack()
Log("Charge brought us into melee reach; auto-attack dealt " .. (startHealth - GetHealth(dummy)) .. " damage")

GM.DestroyMonster(dummy)
