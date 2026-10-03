-- e2e-own-character: chp
-- A channel can still be stopped after the spells it triggers have gone off.
--
-- Proc and triggered casts used to be installed as the caster's current cast state, replacing
-- whatever was there. Fire Barrage triggers its damage spell from a periodic aura on the caster,
-- so its first tick swapped the running channel out for the finished proc. The channel still
-- ended on its own countdown, but every StopCast - cancelling it, moving, kicks, control effects -
-- reached the proc instead: from the first tick on, the channel could no longer be stopped.
--
-- Cancelling stands in for the other interrupts: all of them end the channel through the same
-- SpellCast::StopCast, and it is the one the headless client can trigger deterministically.
--
-- Uses: Fire Barrage (spell 150, mage channel, 1.5s, ticks every 500ms, spell level 4), Training
-- Dummy (creature 40). Own character: the scenario levels up and must start from a known level.

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

-- Past the first tick (500ms), well before the channel's own end (1500ms).
Sleep(650)
Assert(IsChanneling(me), "the channel should still be running after its first tick")

CancelCast()

-- ~0.7s into a 1.5s channel: only the cancel can end it this early.
Assert(WaitUntil(function() return not IsChanneling(me) end, 400, "channel cancelled"),
	"cancelling should end the channel after it has ticked")
Assert(ChannelStartCount(me) == 1, "expected exactly one ChannelStart, got " .. ChannelStartCount(me))

GM.DestroyMonster(dummy)
