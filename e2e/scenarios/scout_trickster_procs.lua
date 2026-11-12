-- e2e-class: 4
-- e2e-own-character: tk
-- e2e-timeout: 180
--
-- The Trickster talents react to things that raised no proc events before the Scout rework:
-- casting a spell that deals no damage (Knockout, Evasive Step) and dodging a melee swing.
--
-- * Cheap Shot (1421): Knockout leaves the target Cheap Shot-ed (debuff 1433).
-- * Slippery (1420): Evasive Step grants Energy (energize 1432, observed as a SpellGo).
-- * Riposte (1428), Opportunist (1430) and Smoke and Mirrors (1426): dodging a Skeleton Warrior's
--   swings arms Riposte (buff 1429), grants Energy (1431) and strikes back (1435). Quick Cut then
--   consumes Riposte.
--
-- The talent passives are learned with GM.LearnSpell, which applies the same aura the talent would.
-- Own character, so the passives do not leak into other Scout scenarios. Godmode turns the
-- skeleton's hits into immune swings; dodges are rolled before immunity, so they still count.

local KNOCKOUT = 161
local EVASIVE_STEP = 166
local QUICK_CUT = 163
local CHEAP_SHOT, CHEAP_SHOT_DEBUFF = 1421, 1433
local SLIPPERY, SLIPPERY_ENERGY = 1420, 1432
local RIPOSTE, RIPOSTE_BUFF = 1428, 1429
local OPPORTUNIST, OPPORTUNIST_ENERGY = 1430, 1431
local SMOKE_AND_MIRRORS, COUNTERSTRIKE = 1426, 1435
local TRAINING_DUMMY = 40
local SKELETON_WARRIOR = 18

local me = Me()

GM.Godmode(true)
GM.LevelUp(9)
GM.ClassLevelUp(9)
Assert(WaitUntil(function() return GetLevel(me) >= 10 end, 15000, "reaches the level cap"),
	"the character should be level 10 to match the skeleton")

for _, spellId in ipairs({ KNOCKOUT, EVASIVE_STEP, QUICK_CUT, CHEAP_SHOT, SLIPPERY, RIPOSTE, OPPORTUNIST, SMOKE_AND_MIRRORS }) do
	GM.LearnSpell(spellId)
	Assert(WaitUntil(function() return HasSpell(spellId) end, 10000, "spell " .. spellId .. " learned"),
		"the character should know spell " .. spellId)
end

-- Clears every cooldown (the global one included) and fills energy. With an enemy selected the GM
-- resets would apply to it, so the target is dropped around them.
local function Prepare(target)
	TargetUnit("0x0")
	GM.ResetCooldowns()
	GM.RestorePower()
	if target then
		TargetUnit(target)
	end
end

local function Cast(spellId, target, what)
	local accepted = target and CastSpell(spellId, target) or CastSpell(spellId)
	Assert(accepted, what .. ": cast request should be accepted")
	Assert(WaitUntil(function()
		local result = LastCastResult()
		return result == "ok" or result:find("failed") ~= nil
	end, 10000, what .. " finished"), what .. ": cast should finish")
	Assert(LastCastResult() == "ok", what .. " should succeed, got: " .. LastCastResult())
end

-- === Casting spells without damage ===
local dummy = GM.CreateMonster(TRAINING_DUMMY)
GM.Worldport(0, GetPosX(me) + 3, GetPosY(me), GetPosZ(me), 0)
FaceUnit(dummy)

Prepare(dummy)
Cast(KNOCKOUT, dummy, "Knockout")
Assert(WaitUntil(function() return HasAura(dummy, CHEAP_SHOT_DEBUFF) end, 5000, "dummy cheap-shotted"),
	"Knockout with Cheap Shot should make the target take more damage")

Prepare(nil)
local slipperyBefore = SpellGoCount(me, SLIPPERY_ENERGY)
Cast(EVASIVE_STEP, nil, "Evasive Step")
Assert(WaitUntil(function() return SpellGoCount(me, SLIPPERY_ENERGY) > slipperyBefore end, 5000, "slippery energy"),
	"Evasive Step with Slippery should grant Energy")

TargetUnit("0x0")
GM.DestroyMonster(dummy)

-- === Dodging ===
local skeleton = GM.CreateMonster(SKELETON_WARRIOR)
Assert(skeleton ~= nil, "the skeleton should spawn")
GM.Worldport(0, GetPosX(me) + 2, GetPosY(me), GetPosZ(me), 0)
FaceUnit(skeleton)
TargetUnit(skeleton)
GM.DamageTarget(1)

-- 55% dodge chance during Smoke and Mirrors; a skeleton swings every ~2 sec. Recast after it ends
-- so the window below covers well over twenty swings.
local opportunistBefore = SpellGoCount(me, OPPORTUNIST_ENERGY)
local counterBefore = SpellGoCount(me, COUNTERSTRIKE)
local function AllDodgeProcsSeen()
	return HasAura(me, RIPOSTE_BUFF)
		and SpellGoCount(me, OPPORTUNIST_ENERGY) > opportunistBefore
		and SpellGoCount(me, COUNTERSTRIKE) > counterBefore
end

local seen = false
for attempt = 1, 4 do
	Prepare(skeleton)
	Cast(SMOKE_AND_MIRRORS, nil, "Smoke and Mirrors " .. attempt)
	seen = WaitUntil(AllDodgeProcsSeen, 12000, "dodge procs, attempt " .. attempt)
	if seen then
		break
	end
end
Assert(HasAura(me, RIPOSTE_BUFF), "a dodge should arm Riposte")
Assert(SpellGoCount(me, OPPORTUNIST_ENERGY) > opportunistBefore, "a dodge should grant Opportunist Energy")
Assert(SpellGoCount(me, COUNTERSTRIKE) > counterBefore, "a dodge during Smoke and Mirrors should strike back")

-- Quick Cut consumes Riposte. A new dodge can re-arm it right away, so the cast is checked rather
-- than the aura's absence: the buff must drop at least briefly.
Prepare(skeleton)
Cast(QUICK_CUT, skeleton, "Quick Cut")
Assert(WaitUntil(function() return not HasAura(me, RIPOSTE_BUFF) end, 3000, "riposte consumed"),
	"Quick Cut should consume Riposte")

TargetUnit("0x0")
GM.DestroyMonster(skeleton)
GM.Godmode(false)
