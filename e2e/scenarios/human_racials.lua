-- e2e-class: 1
-- e2e-timeout: 120
-- Human racial abilities: a fresh human character knows its three racials, Call of the
-- Watch applies its buff and starts its cooldown, and the racials survive a class switch.
--
-- Uses: Versatility (248), Used to Hard Work (249), Call of the Watch (250),
-- the Mage class-change spell (230, 10 s cast time) and Frost Armor (6, a Mage level-1
-- class spell). The passives carry HiddenAura, so their auras are never sent to the
-- client; only the spells themselves are observable here.

local VERSATILITY = 248
local HARD_WORK = 249
local CALL_OF_THE_WATCH = 250
local BECOME_MAGE = 230
local FROST_ARMOR = 6

local me = Me()

for _, spellId in ipairs({ VERSATILITY, HARD_WORK, CALL_OF_THE_WATCH }) do
	Assert(WaitUntil(function() return HasSpell(spellId) end, 10000, "racial " .. spellId .. " known"),
		"a fresh human character should know racial spell " .. spellId)
end

Assert(CastSpell(CALL_OF_THE_WATCH), "Call of the Watch request should be accepted")
Assert(WaitUntil(function() return LastCastResult() == "ok" end, 10000, "cast succeeds"),
	"Call of the Watch should succeed, got: " .. LastCastResult())
Assert(WaitUntil(function() return HasAura(me, CALL_OF_THE_WATCH) end, 10000, "buff applied"),
	"Call of the Watch should buff the caster")
Assert(IsSpellOnCooldown(CALL_OF_THE_WATCH), "Call of the Watch should be on cooldown")

-- Switch class: racials have classmask 0 and must stay known and castable.
GM.LearnSpell(BECOME_MAGE)
Assert(WaitUntil(function() return HasSpell(BECOME_MAGE) end, 10000, "class change learned"),
	"the Mage class-change spell should be learned")
-- Casting right after Call of the Watch hits the global cooldown (NotReady), so clear it.
-- The spell is CanOnlyTargetPlayers, so it needs an explicit (self) target.
GM.ResetCooldowns()
Assert(CastSpell(BECOME_MAGE, me), "class change request should be accepted")
Assert(WaitUntil(function() return HasSpell(FROST_ARMOR) end, 25000, "now a mage"),
	"switching to Mage should grant Frost Armor")

for _, spellId in ipairs({ VERSATILITY, HARD_WORK, CALL_OF_THE_WATCH }) do
	Assert(HasSpell(spellId), "racial " .. spellId .. " should survive the class switch")
end

-- Call of the Watch has a 2 min cooldown: reset, then recast and require the cooldown to
-- start again as proof the second cast really ran.
GM.ResetCooldowns()
Assert(WaitUntil(function() return not IsSpellOnCooldown(CALL_OF_THE_WATCH) end, 10000, "cooldown reset"),
	"GM.ResetCooldowns should clear the Call of the Watch cooldown")
Assert(CastSpell(CALL_OF_THE_WATCH), "Call of the Watch should still be castable as a mage")
Assert(WaitUntil(function() return IsSpellOnCooldown(CALL_OF_THE_WATCH) end, 10000, "cast as mage runs"),
	"Call of the Watch as a mage should succeed and start its cooldown, got: " .. LastCastResult())

Log("racials known, Call of the Watch buffs and survives a class switch")
