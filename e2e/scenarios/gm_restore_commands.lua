-- e2e-own-character: rst
-- The GM restore commands: GM.Heal, GM.RestorePower and GM.ResetCooldowns act on the current
-- target, or on the GM's own character when nothing is targeted.
--
-- Uses: Fire Barrage (spell 150, mage channel, 45 mana, 10s cooldown, spell level 4),
-- Training Dummy (creature 40, immortal).
-- Own character: the scenario levels up and must start from a known level.

local FIRE_BARRAGE = 150
local TRAINING_DUMMY = 40

local me = Me()

GM.LevelUp(3)
Assert(WaitUntil(function() return GetLevel(me) >= 4 end, 10000, "reached level 4"),
	"character should be at least level 4, is " .. tostring(GetLevel(me)))
GM.LearnSpell(FIRE_BARRAGE)
Assert(WaitUntil(function() return HasSpell(FIRE_BARRAGE) end, 10000, "spell learned"),
	"player should know Fire Barrage")

-- Self, nothing targeted: heal.
GM.DamageTarget(20)
Assert(WaitUntil(function() return GetHealth(me) < GetMaxHealth(me) end, 5000, "took damage"),
	"GM.DamageTarget without a target should hurt the GM")
GM.Heal()
Assert(WaitUntil(function() return GetHealth(me) == GetMaxHealth(me) end, 5000, "healed"),
	"GM.Heal should restore full health, have " .. GetHealth(me) .. "/" .. GetMaxHealth(me))

-- A target: heal the dummy, not ourselves.
local dummy = GM.CreateMonster(TRAINING_DUMMY)
GM.Worldport(0, GetPosX(me) + 8, GetPosY(me), GetPosZ(me), 0)
Assert(WaitUntil(function() return GetDistance(me, dummy) > 5 end, 10000, "teleported away from dummy"),
	"player should be a few units away from the dummy after worldport")
TargetUnit(dummy)
GM.DamageTarget(20)
Assert(WaitUntil(function() return GetHealth(dummy) < GetMaxHealth(dummy) end, 5000, "dummy took damage"),
	"the targeted dummy should take the damage")
GM.Heal()
Assert(WaitUntil(function() return GetHealth(dummy) == GetMaxHealth(dummy) end, 5000, "dummy healed"),
	"GM.Heal should heal the target, dummy has " .. GetHealth(dummy) .. "/" .. GetMaxHealth(dummy))

-- Spend mana and put Fire Barrage on its 10s cooldown.
local startMana = GetPower(me)
Assert(CastSpell(FIRE_BARRAGE, dummy), "first Fire Barrage request should be accepted")
Assert(WaitUntil(function() return ChannelStartCount(me) == 1 end, 5000, "first channel"),
	"the first Fire Barrage should channel, last cast result: " .. LastCastResult())
Assert(WaitUntil(function() return not IsChanneling(me) end, 5000, "first channel ended"),
	"the first channel should end")
Assert(GetPower(me) < startMana, "Fire Barrage should have cost mana")

-- Mana: the dummy is targeted, so clear the target to act on ourselves.
TargetUnit("0x0")
GM.RestorePower()
Assert(WaitUntil(function() return GetPower(me) == GetMaxPower(me) end, 5000, "mana restored"),
	"GM.RestorePower should fill mana, have " .. GetPower(me) .. "/" .. GetMaxPower(me))

-- Cooldowns: well inside the 10s cooldown, a reset lets the channel go off again.
GM.ResetCooldowns()
TargetUnit(dummy)
Assert(CastSpell(FIRE_BARRAGE, dummy), "second Fire Barrage request should be accepted")
Assert(WaitUntil(function() return ChannelStartCount(me) == 2 end, 5000, "second channel"),
	"Fire Barrage should be castable again right after GM.ResetCooldowns, last cast result: " .. LastCastResult())
Assert(WaitUntil(function() return not IsChanneling(me) end, 5000, "second channel ended"),
	"the second channel should end")

GM.DestroyMonster(dummy)
