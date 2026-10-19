-- e2e-class: 2
--
-- Regression: a spell with a cast time must not survive the caster running.
--
-- Hymn of Dawn (spell 1203, 2.5s cast) carried no interruptflags, so the server's
-- GameUnitS::ApplyMovementInfo -> SingleCastState::StopCast(spell_interrupt_flags::Movement)
-- returned early for it and the cast completed while the character was moving.
-- Every other player spell with a cast time opts in with interruptflags = 5
-- (Movement | Interrupt); spell 1203 now does too.
--
-- Uses: Hymn of Dawn (spell 1203, cleric - hence the e2e-class directive above).

local HYMN_OF_DAWN = 1203
local FAILED_MOVING = 8 -- spell_cast_result::FailedMoving, see src/shared/game/spell.h

-- The character is shared between scenarios of its class: start from the flat, open
-- default spawn so the run below always finds a path.
local SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z = 0, 292.267, 5.33, 552.571
GM.Worldport(SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z, 0)
Assert(WaitUntil(function() return math.abs(GetPosX(Me()) - SPAWN_X) < 1 end, 10000, "at the spawn point"),
	"player should start from the default spawn")

local me = Me()

-- Hymn of Dawn costs 80 mana, which a fresh level 1 character cannot pay.
if GetLevel(me) < 10 then
	GM.LevelUp(10 - GetLevel(me))
	Assert(WaitUntil(function() return GetLevel(me) >= 10 end, 10000, "leveled up"),
		"player should reach level 10 to afford Hymn of Dawn")
end

GM.LearnSpell(HYMN_OF_DAWN)
Assert(WaitUntil(function() return HasSpell(HYMN_OF_DAWN) end, 10000, "Hymn of Dawn learned"),
	"player should know Hymn of Dawn after GM.LearnSpell")

GM.ResetCooldowns()
GM.RestorePower()
Assert(WaitUntil(function() return GetPower(me) >= 80 end, 10000, "mana restored"),
	"player should have enough mana for Hymn of Dawn (got " .. tostring(GetPower(me)) .. ")")

-- Start the cast standing still: that part is allowed.
Assert(CastSpell(HYMN_OF_DAWN), "self-cast request should be accepted")
Assert(WaitUntil(function() return LastCastResult() == "started" end, 5000, "cast started"),
	"the 2.5s cast should start, got: " .. LastCastResult())

-- Now run. The first movement packet must end the cast.
Assert(MoveTo(GetPosX(me) + 10, GetPosY(me), GetPosZ(me), 30000),
	"player should be able to run 10 units from the spawn")

Assert(WaitUntil(function() return LastCastResult() ~= "started" end, 10000, "cast resolved"),
	"the cast should not still be running after the character moved, got: " .. LastCastResult())

Assert(LastCastResult() == "failed:" .. FAILED_MOVING,
	"moving should interrupt Hymn of Dawn with FailedMoving, got: " .. LastCastResult())

Log("Hymn of Dawn was interrupted by movement as expected")
