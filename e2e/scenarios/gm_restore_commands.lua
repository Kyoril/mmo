-- e2e-own-character: rst
-- The GM restore commands: GM.Heal, GM.RestorePower and GM.ResetCooldowns act on the current
-- target, or on the GM's own character when nothing is targeted.
--
-- Uses: Frost Armor (spell 6, instant self-buff, triggers the global cooldown), Fire Barrage
-- (spell 150, mage channel, 45 mana, 10s cooldown, spell level 4), Training Dummy (creature 40, immortal).
-- Own character: the scenario levels up and must start from a known level.

local FROST_ARMOR = 6
local FIRE_BARRAGE = 150
local TRAINING_DUMMY = 40
local FAILED_NOT_READY = "failed:10"

local me = Me()

GM.LevelUp(3)
Assert(WaitUntil(function() return GetLevel(me) >= 4 end, 10000, "reached level 4"),
	"character should be at least level 4, is " .. tostring(GetLevel(me)))
GM.LearnSpell(FROST_ARMOR)
GM.LearnSpell(FIRE_BARRAGE)
Assert(WaitUntil(function() return HasSpell(FROST_ARMOR) and HasSpell(FIRE_BARRAGE) end, 10000, "spell learned"),
	"player should know Frost Armor and Fire Barrage")

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

-- GM.ResetCooldowns acts on the current target, so drop the target for our own reset and select
-- the dummy again afterwards.
local function resetOwnCooldowns(retarget)
	TargetUnit("0x0")
	GM.ResetCooldowns()
	TargetUnit(retarget)
end

-- Waits for the server's answer to a cast request and returns it ("started", "ok", "failed:<n>").
local function castResult(spellId, target, what)
	Assert(CastSpell(spellId, target), what .. " request should be accepted")
	Assert(WaitUntil(function() return LastCastResult() ~= "pending" end, 5000, what .. " answered"),
		what .. " should be answered by the server")
	return LastCastResult()
end

-- Global cooldown. Negative control first: an instant spell's global cooldown refuses Fire
-- Barrage, which has never been cast and so has no cooldown of its own.
Assert(castResult(FROST_ARMOR, me, "Frost Armor") == "ok", "Frost Armor should go off, got " .. LastCastResult())
local refused = castResult(FIRE_BARRAGE, dummy, "Fire Barrage inside the global cooldown")
Assert(refused == FAILED_NOT_READY, "the global cooldown should refuse Fire Barrage, got " .. refused)

-- Resetting the targeted dummy leaves our own global cooldown alone...
GM.ResetCooldowns()
refused = castResult(FIRE_BARRAGE, dummy, "Fire Barrage after resetting the dummy")
Assert(refused == FAILED_NOT_READY, "resetting the target must not touch our cooldowns, got " .. refused)

-- ...while our own reset, still inside the same 1.5s global cooldown, lifts it.
resetOwnCooldowns(dummy)
local startMana = GetPower(me)
castResult(FIRE_BARRAGE, dummy, "Fire Barrage after reset")
Assert(WaitUntil(function() return ChannelStartCount(me) == 1 end, 5000, "first channel"),
	"GM.ResetCooldowns should lift the global cooldown, last cast result: " .. LastCastResult())
Assert(WaitUntil(function() return not IsChanneling(me) end, 5000, "first channel ended"),
	"the first channel should end")
Assert(WaitUntil(function() return GetPower(me) < startMana end, 5000, "mana spent"),
	"Fire Barrage should have cost mana")
Assert(IsSpellOnCooldown(FIRE_BARRAGE), "the client should show Fire Barrage's cooldown after the cast")

-- Mana: the dummy is targeted, so clear the target to act on ourselves.
TargetUnit("0x0")
GM.RestorePower()
Assert(WaitUntil(function() return GetPower(me) == GetMaxPower(me) end, 5000, "mana restored"),
	"GM.RestorePower should fill mana, have " .. GetPower(me) .. "/" .. GetMaxPower(me))
TargetUnit(dummy)

-- The spell's own cooldown: Fire Barrage is now on its 10s cooldown and refused...
refused = castResult(FIRE_BARRAGE, dummy, "Fire Barrage on cooldown")
Assert(refused == FAILED_NOT_READY, "Fire Barrage should be on cooldown, got " .. refused)

-- ...until a reset, well inside those 10s.
resetOwnCooldowns(dummy)
Assert(WaitUntil(function() return not IsSpellOnCooldown(FIRE_BARRAGE) end, 5000, "client cooldown cleared"),
	"the reset should clear Fire Barrage's cooldown on the client too")
castResult(FIRE_BARRAGE, dummy, "Fire Barrage after second reset")
Assert(WaitUntil(function() return ChannelStartCount(me) == 2 end, 5000, "second channel"),
	"Fire Barrage should be castable again right after GM.ResetCooldowns, last cast result: " .. LastCastResult())
Assert(WaitUntil(function() return not IsChanneling(me) end, 5000, "second channel ended"),
	"the second channel should end")

GM.DestroyMonster(dummy)
