-- e2e-own-character: chd
-- A channel whose target leaves the world ends at once, and clients are told so.
--
-- The target subscriptions used to stay bound to the SingleCastState that started the channel.
-- Losing the target mid-channel then stopped that stale state - a SpellFailure, no
-- ChannelUpdate(0) - and swapped the live ChannelingCastState out without ending it. Clients
-- kept the channel until its countdown ran out on its own.
--
-- Despawning stands in for a kill: the Training Dummy cannot die while in combat, and both
-- signals are wired the same way.
--
-- Uses: Fire Barrage (spell 150, mage channel, 1.5s, spell level 4), Training Dummy
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

local dummy = GM.CreateMonster(TRAINING_DUMMY)
GM.Worldport(0, GetPosX(me) + 8, GetPosY(me), GetPosZ(me), 0)
Assert(WaitUntil(function() return GetDistance(me, dummy) > 5 end, 10000, "teleported away from dummy"),
	"player should be a few units away from the dummy after worldport")
TargetUnit(dummy)

Assert(CastSpell(FIRE_BARRAGE, dummy), "Fire Barrage request should be accepted")
Assert(WaitUntil(function() return IsChanneling(me) end, 5000, "channel announced"),
	"Fire Barrage should start a channel, last cast result: " .. LastCastResult())

GM.DestroyMonster(dummy)

-- Well inside the 1.5s channel: only the target-loss path can end it this early.
Assert(WaitUntil(function() return not IsChanneling(me) end, 700, "channel ended with its target"),
	"the channel should end as soon as its target is gone")
Assert(ChannelStartCount(me) == 1, "expected exactly one ChannelStart, got " .. ChannelStartCount(me))
