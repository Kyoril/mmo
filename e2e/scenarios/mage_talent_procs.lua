-- e2e-timeout: 150
-- e2e-own-character: tlp
-- The new mage talents' procs fire end to end: Shatter marks enemies hit by Frost Nova,
-- Heating Up empowers the caster when Fire Blast hits, and Ignite sets the target ablaze on a
-- Fire critical strike. Ignite also proves that direct damage spells roll crits at all (they
-- did not before the talent redesign).
--
-- The talent passives are learned with GM.LearnSpell, which applies the same aura the talent
-- would. The character is the scenario's own: the passives must not leak into scenarios that
-- assert on plain damage.
--
-- Uses: Frost Nova (68, 10 yd around the caster), Fire Blast (7, instant), Shatter (1014 ->
-- debuff 1015, +50% crit chance taken), Ignite (1021 -> burn 1022), Heating Up (1023 -> buff
-- 1024) and the Training Dummy (creature 40).

local FROST_NOVA = 68
local FIRE_BLAST = 7
local SHATTER, SHATTERED = 1014, 1015
local IGNITE, IGNITED = 1021, 1022
local HEATING_UP, HEATED_UP = 1023, 1024
local TRAINING_DUMMY = 40

local me = Me()

-- Frost Nova needs level 4; GM.LevelUp is a no-op past the cap on re-runs.
GM.LevelUp(9)
Assert(WaitUntil(function() return GetLevel(me) >= 4 end, 10000, "levelled up"),
	"the character should reach level 4 for Frost Nova")

for _, spellId in ipairs({ FROST_NOVA, FIRE_BLAST, SHATTER, IGNITE, HEATING_UP }) do
	GM.LearnSpell(spellId)
	Assert(WaitUntil(function() return HasSpell(spellId) end, 10000, "spell " .. spellId .. " learned"),
		"the character should know spell " .. spellId)
end

local dummy = GM.CreateMonster(TRAINING_DUMMY)

-- Fire Blast needs the target in front at some distance; Frost Nova reaches 10 yards.
GM.Worldport(0, GetPosX(me) + 6, GetPosY(me), GetPosZ(me), 0)
Assert(WaitUntil(function() return GetDistance(me, dummy) > 4 end, 10000, "stepped away from the dummy"),
	"the character should stand a few yards from the dummy")
TargetUnit(dummy)

-- Frost Nova centers on the caster and takes no unit target.
local function Cast(spellId, target)
	-- With an enemy selected the GM resets would apply to it, not to the character.
	TargetUnit("0x0")
	GM.ResetCooldowns()
	GM.RestorePower()
	TargetUnit(dummy)
	local accepted = target and CastSpell(spellId, target) or CastSpell(spellId)
	Assert(accepted, "cast request for " .. spellId .. " should be accepted")
	Assert(WaitUntil(function()
		local result = LastCastResult()
		return result == "ok" or result:find("failed") ~= nil
	end, 10000, "cast " .. spellId .. " finished"), "cast should finish")
	Assert(LastCastResult() == "ok", "cast " .. spellId .. " should succeed, got: " .. LastCastResult())
end

-- Shatter: Frost Nova hits leave the dummy Shattered.
Cast(FROST_NOVA)
Assert(WaitUntil(function() return HasAura(dummy, SHATTERED) end, 5000, "dummy shattered"),
	"Frost Nova should leave the dummy Shattered")

-- Heating Up: every Fire Blast hit empowers the caster.
Cast(FIRE_BLAST, dummy)
Assert(WaitUntil(function() return HasAura(me, HEATED_UP) end, 5000, "caster heated up"),
	"a Fire Blast hit should apply Heating Up to the caster")

-- Ignite: with Shattered (+50%) on top of the 5% base, Fire Blast crits more often than not.
-- Re-freeze whenever Shattered ran out; 15 tries leave a miss chance far below 1e-4.
local ignited = HasAura(dummy, IGNITED)
for attempt = 1, 15 do
	if ignited then
		break
	end
	if not HasAura(dummy, SHATTERED) then
		Cast(FROST_NOVA)
	end
	Cast(FIRE_BLAST, dummy)
	ignited = WaitUntil(function() return HasAura(dummy, IGNITED) end, 1500, "ignite attempt " .. attempt)
end
Assert(ignited, "a Fire Blast critical strike should Ignite the dummy")

TargetUnit("0x0")
GM.DestroyMonster(dummy)
