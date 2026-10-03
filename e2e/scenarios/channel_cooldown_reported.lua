-- e2e-own-character: fbc
-- A channeled spell reports its own cooldown to the caster's client, and keeps it after the
-- channel has ended.
--
-- Fire Barrage refused a recast with "not ready" while the action bar showed no cooldown. The
-- server read the started cast's result from the SingleCastState the channel had already handed
-- over and freed, and answered the cast request with a SpellFailure on top of the SpellGo. The
-- game client clears a spell's cooldown on a SpellFailure for the cast it is running.
--
-- Uses: Fire Barrage (spell 150, mage channel, 1.5s, 10s cooldown, spell level 4), Training Dummy
-- (creature 40). Own character: the scenario levels up and must start from a known level.

local FIRE_BARRAGE = 150
local TRAINING_DUMMY = 40

local me = Me()

GM.LevelUp(3)
Assert(WaitUntil(function() return GetLevel(me) >= 4 end, 10000, "reached level 4"),
	"character should be at least level 4, is " .. tostring(GetLevel(me)))
GM.LearnSpell(FIRE_BARRAGE)
Assert(WaitUntil(function() return HasSpell(FIRE_BARRAGE) end, 10000, "spell learned"),
	"player should know Fire Barrage")
GM.ResetCooldowns()

local dummy = GM.CreateMonster(TRAINING_DUMMY)
GM.Worldport(0, GetPosX(me) + 8, GetPosY(me), GetPosZ(me), 0)
Assert(WaitUntil(function() return GetDistance(me, dummy) > 5 end, 10000, "teleported away from dummy"),
	"player should be a few units away from the dummy after worldport")
TargetUnit(dummy)

Assert(CastSpell(FIRE_BARRAGE, dummy), "Fire Barrage request should be accepted")
Assert(WaitUntil(function() return IsChanneling(me) end, 5000, "channel announced"),
	"Fire Barrage should start a channel, last cast result: " .. LastCastResult())
Assert(IsSpellOnCooldown(FIRE_BARRAGE), "Fire Barrage should be on cooldown while channeling")

-- The SpellFailure followed the channel's packets in the same tick, so give it time to arrive.
Assert(not WaitUntil(function() return string.sub(LastCastResult(), 1, 7) == "failed:" end, 500,
	"cast reported failed"), "a started channel must not be reported as failed, got " .. LastCastResult())

Assert(WaitUntil(function() return not IsChanneling(me) end, 4000, "channel ended"),
	"the channel should end after its duration")
Assert(IsSpellOnCooldown(FIRE_BARRAGE), "Fire Barrage should still be on cooldown after the channel")

GM.DestroyMonster(dummy)
