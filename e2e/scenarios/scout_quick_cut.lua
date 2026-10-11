-- e2e-class: 4
-- e2e-own-character: qct
-- e2e-timeout: 150
--
-- Quick Cut (spell 163) is the Scout's off-global-cooldown filler: it does not trigger or respect
-- the global cooldown, so it can be woven right after Wounding Strike, and against a target
-- wounded by the caster it always restores energy (Quick Cut Energy Restoration, spell 164).
-- Before the redesign it sat on the global cooldown like every other strike and restored energy on
-- a 30% roll regardless of the wound, which left it strictly worse than Wounding Strike.
--
-- Uses: Wounding Strike (76, leaves Wounded, spell 165, on the target), Quick Cut (163), its
-- energy restoration (164, observed as a SpellGo by the caster) and the Training Dummy (creature
-- 40), which never fights back. Own character, so the learned spells do not leak into other Scout
-- scenarios.

local WOUNDING_STRIKE = 76
local QUICK_CUT = 163
local QUICK_CUT_ENERGY = 164
local WOUNDED = 165
local TRAINING_DUMMY = 40

local me = Me()

for _, spellId in ipairs({ WOUNDING_STRIKE, QUICK_CUT }) do
	GM.LearnSpell(spellId)
	Assert(WaitUntil(function() return HasSpell(spellId) end, 10000, "spell " .. spellId .. " learned"),
		"the character should know spell " .. spellId)
end

local dummy = GM.CreateMonster(TRAINING_DUMMY)

-- The dummy spawns on top of us; melee strikes need it in front, so step aside within reach.
GM.Worldport(0, GetPosX(me) + 3, GetPosY(me), GetPosZ(me), 0)
FaceUnit(dummy)
TargetUnit(dummy)

-- Clears every cooldown (the global one included) and fills energy. With an enemy selected the GM
-- resets would apply to it, so the target is dropped around them.
local function Prepare()
	TargetUnit("0x0")
	GM.ResetCooldowns()
	GM.RestorePower()
	TargetUnit(dummy)
end

local function Cast(spellId, what)
	Assert(CastSpell(spellId, dummy), what .. ": cast request should be accepted")
	Assert(WaitUntil(function()
		local result = LastCastResult()
		return result == "ok" or result:find("failed") ~= nil
	end, 10000, what .. " finished"), what .. ": cast should finish")
	Assert(LastCastResult() == "ok", what .. " should succeed, got: " .. LastCastResult())
end

-- Not wounded: Quick Cut deals its damage but restores nothing. Three casts, so the former 30%
-- roll without a wound check would show up most of the time.
Assert(not HasAura(dummy, WOUNDED), "the fresh dummy must not be wounded")
for attempt = 1, 3 do
	Prepare()
	local restoresBefore = SpellGoCount(me, QUICK_CUT_ENERGY)
	Cast(QUICK_CUT, "Quick Cut on an unwounded target " .. attempt)
	Sleep(750)
	Assert(SpellGoCount(me, QUICK_CUT_ENERGY) == restoresBefore,
		"Quick Cut must not restore energy against a target that is not wounded")
end

-- Wounded: Quick Cut goes off straight after Wounding Strike, inside its global cooldown, and
-- always restores energy.
for attempt = 1, 3 do
	Prepare()
	Cast(WOUNDING_STRIKE, "Wounding Strike " .. attempt)
	Assert(WaitUntil(function() return HasAura(dummy, WOUNDED) end, 1000, "dummy wounded"),
		"Wounding Strike should leave the dummy Wounded")

	local restoresBefore = SpellGoCount(me, QUICK_CUT_ENERGY)
	Cast(QUICK_CUT, "Quick Cut inside Wounding Strike's global cooldown " .. attempt)
	Assert(WaitUntil(function() return SpellGoCount(me, QUICK_CUT_ENERGY) > restoresBefore end, 3000,
		"energy restored " .. attempt), "Quick Cut against a wounded target should always restore energy")
end

TargetUnit("0x0")
GM.DestroyMonster(dummy)
