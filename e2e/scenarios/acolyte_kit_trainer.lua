-- e2e-class: 3
-- e2e-own-character: kt
-- e2e-timeout: 240
--
-- The acolyte's overhauled trainer kit, cast by a level-12 acolyte at a Training Dummy (creature 40):
-- Curse of Frailty (1330), Curse of the Mire (1333, replaces Frailty: one curse per target),
-- Drain Essence (channel 1331, tick 1332: heals the caster for the damage dealt), Wasting Plague
-- (1334, its own track beside Corrupting Touch 24), Rot Field (1336, ground zone ticking 1337) and
-- Call Gloomlings (1335: two Gloomlings, creature 123, that attack the caster's target).
--
-- The spells are learned with GM.LearnSpell. Own character: it is levelled to 12 and must not
-- share that with the other acolyte scenarios. The talents are covered by acolyte_kit_summoner.lua
-- and acolyte_kit_blight_dread.lua.

local CORRUPTING_TOUCH = 24
local CURSE_OF_FRAILTY = 1330
local DRAIN_ESSENCE, DRAIN_TICK = 1331, 1332
local CURSE_OF_THE_MIRE = 1333
local WASTING_PLAGUE = 1334
local CALL_GLOOMLINGS, GLOOMLING = 1335, 123
local ROT_FIELD, ROT_FIELD_TICK = 1336, 1337
local TRAINING_DUMMY = 40

local me = Me()

GM.LevelUp(11)
Assert(WaitUntil(function() return GetLevel(me) >= 10 end, 15000, "levelled up"),
	"the acolyte should reach level 10+ for Rot Field, is " .. GetLevel(me))

for _, spellId in ipairs({ CORRUPTING_TOUCH, CURSE_OF_FRAILTY, DRAIN_ESSENCE, CURSE_OF_THE_MIRE, WASTING_PLAGUE,
	CALL_GLOOMLINGS, ROT_FIELD }) do
	GM.LearnSpell(spellId)
	Assert(WaitUntil(function() return HasSpell(spellId) end, 10000, "spell " .. spellId .. " learned"),
		"the acolyte should know spell " .. spellId)
end

-- The dummy spawns on top of the character; spells need it in front at some distance.
local dummy = GM.CreateMonster(TRAINING_DUMMY)
GM.Worldport(0, GetPosX(me) + 10, GetPosY(me), GetPosZ(me), 0)
Assert(WaitUntil(function() return GetDistance(me, dummy) > 8 end, 10000, "stepped away from the dummy"),
	"the character should stand ten yards from the dummy")

--- Casts spellId at target (or without a target) with fresh cooldowns and full mana, and waits
--- for the cast to succeed.
local function Cast(spellId, target)
	-- With an enemy selected the GM resets would apply to it, not to the character.
	TargetUnit("0x0")
	GM.ResetCooldowns()
	GM.RestorePower()
	if target then
		TargetUnit(target)
	end
	local accepted = target and CastSpell(spellId, target) or CastSpell(spellId)
	Assert(accepted, "cast request for " .. spellId .. " should be accepted")
	Assert(WaitUntil(function()
		local result = LastCastResult()
		return result == "ok" or result:find("failed") ~= nil
	end, 10000, "cast " .. spellId .. " resolves"), "the cast of " .. spellId .. " should resolve")
	Assert(LastCastResult() == "ok", "cast " .. spellId .. " should succeed, got: " .. LastCastResult())
end

--- Casts a negative aura spell until it sticks (a spell can miss), and returns once the aura is up.
local function Afflict(spellId, target, auraId)
	auraId = auraId or spellId
	for attempt = 1, 5 do
		Cast(spellId, target)
		if WaitUntil(function() return HasAura(target, auraId) end, 3000, "aura " .. auraId .. " (try " .. attempt .. ")") then
			return
		end
	end
	Fail("spell " .. spellId .. " should leave aura " .. auraId .. " on the target")
end

--- Heals the dummy to full, so a measurement never runs into its refill at low health.
local function HealDummy()
	TargetUnit(dummy)
	GM.Heal()
	Assert(WaitUntil(function() return GetHealth(dummy) == GetMaxHealth(dummy) end, 5000, "dummy healed"),
		"the dummy should be back at full health")
	TargetUnit("0x0")
end

-- ---------------------------------------------------------------------------------------------
-- Drain Essence: three ticks, each heals the caster for the damage it deals
-- ---------------------------------------------------------------------------------------------

TargetUnit(me)
GM.DamageTarget(math.floor(GetMaxHealth(me) * 0.6))
TargetUnit("0x0")
Assert(WaitUntil(function() return GetHealth(me) < GetMaxHealth(me) * 0.5 end, 5000, "character hurt"),
	"the character should be hurt before draining")
HealDummy()

local ticksBefore = SpellGoCount(me, DRAIN_TICK)
local healthBefore = GetHealth(me)
local dummyBefore = GetHealth(dummy)
Cast(DRAIN_ESSENCE, dummy)
Assert(WaitUntil(function() return IsChanneling(me) end, 3000, "drain channels"), "Drain Essence should channel")
Assert(WaitUntil(function() return not IsChanneling(me) end, 6000, "drain ends"), "the drain channel should end")
Sleep(300)
local ticks = SpellGoCount(me, DRAIN_TICK) - ticksBefore
local dealt = dummyBefore - GetHealth(dummy)
local healed = GetHealth(me) - healthBefore
Log("Drain Essence: " .. ticks .. " ticks, dealt " .. dealt .. ", caster gained " .. healed)
Assert(ticks == 3, "a 3 s Drain Essence should tick three times, ticked " .. ticks)
Assert(dealt > 0, "Drain Essence should damage the dummy")
-- Regeneration only adds to the caster's gain, so the leech is at least the damage dealt.
Assert(healed >= dealt, "the caster should be healed for the damage dealt (dealt " .. dealt .. ", healed " .. healed .. ")")

-- ---------------------------------------------------------------------------------------------
-- Curses: one curse per caster and target
-- ---------------------------------------------------------------------------------------------

Afflict(CURSE_OF_FRAILTY, dummy)
Afflict(CURSE_OF_THE_MIRE, dummy)
Assert(WaitUntil(function() return not HasAura(dummy, CURSE_OF_FRAILTY) end, 3000, "frailty replaced"),
	"Curse of the Mire should replace the acolyte's Curse of Frailty on the dummy")
Log("Curse of the Mire replaced Curse of Frailty")

-- ---------------------------------------------------------------------------------------------
-- Wasting Plague runs beside Corrupting Touch
-- ---------------------------------------------------------------------------------------------

Afflict(CORRUPTING_TOUCH, dummy)
Afflict(WASTING_PLAGUE, dummy)
Assert(HasAura(dummy, CORRUPTING_TOUCH), "Corrupting Touch should stay on the dummy beside Wasting Plague")
Assert(HasAura(dummy, CURSE_OF_THE_MIRE), "the curse should stay on the dummy beside both DoTs")
Log("Corrupting Touch and Wasting Plague tick side by side")

-- ---------------------------------------------------------------------------------------------
-- Rot Field: a zone under the target that ticks every 2 s for 8 s
-- ---------------------------------------------------------------------------------------------

-- Let the DoTs run out first, so only the field damages the dummy.
Assert(WaitUntil(function()
	return not HasAura(dummy, CORRUPTING_TOUCH) and not HasAura(dummy, WASTING_PLAGUE)
end, 25000, "dots expired"), "the DoTs should expire")
HealDummy()

-- Ticks are counted as the dummy's health drops, not by SpellGo: the field ticks once per enemy
-- inside it, and the shared map may hold other creatures nearby.
local rotBefore = SpellGoCount(me, ROT_FIELD_TICK)
dummyBefore = GetHealth(dummy)
Cast(ROT_FIELD, dummy)
local rotTicks = 0
local last = GetHealth(dummy)
for i = 1, 110 do
	Sleep(100)
	local health = GetHealth(dummy)
	if health < last then
		rotTicks = rotTicks + 1
	end
	last = health
end
Assert(SpellGoCount(me, ROT_FIELD_TICK) > rotBefore, "the field's ticks should be the acolyte's Rot Field tick spell")
Log("Rot Field ticked " .. rotTicks .. " times, dummy lost " .. (dummyBefore - GetHealth(dummy)))
Assert(rotTicks >= 3 and rotTicks <= 4, "an 8 s Rot Field should tick 3-4 times, ticked " .. rotTicks)
Assert(GetHealth(dummy) < dummyBefore, "the Rot Field ticks should damage the dummy")

-- ---------------------------------------------------------------------------------------------
-- Call Gloomlings: two Gloomlings attack the caster's target, for 15 s
-- ---------------------------------------------------------------------------------------------

Cast(CALL_GLOOMLINGS, dummy)
Assert(WaitUntil(function() return CountUnitsOwnedBy(me, GLOOMLING) == 2 end, 5000, "gloomlings called"),
	"Call Gloomlings should call two Gloomlings, has " .. CountUnitsOwnedBy(me, GLOOMLING))
local gloomlings = GetOwnedUnits(me, GLOOMLING)
for _, gloomling in ipairs(gloomlings) do
	Assert(GetOwner(gloomling) == me, "each Gloomling should be owned by the caster")
	-- Summoned into the world, not walked into sight: clients play the Gloomling's spawn effect.
	Assert(SpawnReason(gloomling) == "spawned", "a summoned Gloomling should arrive spawned, got " .. SpawnReason(gloomling))
end
Assert(SpawnReason(dummy) == "spawned", "a GM-created dummy is added to the world too")
Assert(WaitUntil(function()
	for _, gloomling in ipairs(gloomlings) do
		if GetTarget(gloomling) ~= dummy then return false end
	end
	return true
end, 8000, "gloomlings attack"), "both Gloomlings should attack the caster's target")
Assert(WaitUntil(function() return CountUnitsOwnedBy(me, GLOOMLING) == 0 end, 20000, "gloomlings leave"),
	"the Gloomlings should leave after 15 s")
-- Expiring is a removal from the world, so clients play the Gloomling's despawn effect.
for _, gloomling in ipairs(gloomlings) do
	Assert(DespawnReason(gloomling) == "destroyed",
		"an expired Gloomling should be destroyed, not leave sight, got " .. DespawnReason(gloomling))
end
Log("Two Gloomlings attacked the dummy and left")

TargetUnit("0x0")
GM.DestroyMonster(dummy)
