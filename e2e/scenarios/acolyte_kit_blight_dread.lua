-- e2e-class: 3
-- e2e-own-character: kb
-- e2e-timeout: 420
--
-- The acolyte's Blight and Dread talents, observed on a live server. The talent rank spells are
-- learned with GM.LearnSpell, which applies the same passive aura the talent would.
--
-- Blight:
-- - Miasma (1355): Rot Field (1336, tick 1337) afflicts the enemies it damages with Corrupting
--   Touch (24).
-- - Creeping Rot (1356): Rot Field lasts 4 s longer (two more ticks) and slows enemies inside (1357).
-- - Venomous Backlash (1358): melee attackers catch Wasting Plague (1334), 20% per hit.
-- - Plaguebearer (63): when an enemy with the acolyte's Corrupting Touch dies, it spreads to up to
--   two nearby enemies.
-- Dread:
-- - Lingering Dread (1364): Curse of Frailty (1330) also cuts healing taken (1365).
-- - Grasping Dread (1361): Curse of the Mire (1333) also roots (1362).
-- - Tangled Tongue (1360, active curse): replaces the acolyte's other curse, and the target's casts
--   take 30% longer (a Bandit Mage's 1.5 s cast, creature 39 / spell 275).
-- - Wail of the Lost (1363, active): fears up to three enemies within 8 yards.
-- - Embrace of Dread (1328, active): +20% damage on the acolyte's damage spells (a Wasting Plague
--   tick).
--
-- Own character: it is levelled to 12 and keeps the talent passives.

local CORRUPTING_TOUCH = 24
local CURSE_OF_FRAILTY = 1330
local CURSE_OF_THE_MIRE = 1333
local WASTING_PLAGUE = 1334
local ROT_FIELD = 1336
local PLAGUEBEARER = 63
local EMBRACE_OF_DREAD = 1328
local MIASMA = 1355
local CREEPING_ROT, CREEPING_ROT_SLOW = 1356, 1357
local VENOMOUS_BACKLASH = 1358
local TANGLED_TONGUE = 1360
local GRASPING_DREAD, GRASPING_DREAD_ROOT = 1361, 1362
local WAIL_OF_THE_LOST = 1363
local LINGERING_DREAD, LINGERING_DREAD_AURA = 1364, 1365
local TRAINING_DUMMY = 40
local FOREST_BANDIT = 37
local BANDIT_MAGE, BANDIT_MAGE_SPELL_CAST_TIME = 39, 1500

local me = Me()
local homeX, homeY, homeZ = GetPosX(me), GetPosY(me), GetPosZ(me)

GM.LevelUp(11)
Assert(WaitUntil(function() return GetLevel(me) >= 10 end, 15000, "levelled up"),
	"the acolyte should reach level 10+, is " .. GetLevel(me))

local function Learn(spellId)
	GM.LearnSpell(spellId)
	Assert(WaitUntil(function() return HasSpell(spellId) end, 10000, "spell " .. spellId .. " learned"),
		"the acolyte should know spell " .. spellId)
end

for _, spellId in ipairs({ CORRUPTING_TOUCH, CURSE_OF_FRAILTY, CURSE_OF_THE_MIRE, WASTING_PLAGUE, ROT_FIELD }) do
	Learn(spellId)
end

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
local function Afflict(spellId, target)
	for attempt = 1, 5 do
		Cast(spellId, target)
		if WaitUntil(function() return HasAura(target, spellId) end, 3000, "aura " .. spellId .. " (try " .. attempt .. ")") then
			return
		end
	end
	Fail("spell " .. spellId .. " should leave its aura on the target")
end

--- Heals the character to full (the GM heal applies to the selection, so clear it first).
local function HealSelf()
	TargetUnit("0x0")
	GM.Heal()
end

local function HealUnit(unit)
	TargetUnit(unit)
	GM.Heal()
	Assert(WaitUntil(function() return GetHealth(unit) == GetMaxHealth(unit) end, 5000, "unit healed"),
		"the unit should be back at full health")
	TargetUnit("0x0")
end

--- Spawns a creature at the character's position plus (dx, dy), then returns to where it stood.
local function SpawnAt(entry, dx, dy)
	local x, y, z = GetPosX(me), GetPosY(me), GetPosZ(me)
	GM.Worldport(0, x + dx, y + dy, z, 0)
	local unit = GM.CreateMonster(entry)
	Assert(unit ~= nil, "creature " .. entry .. " should spawn")
	GM.Worldport(0, x, y, z, 0)
	return unit
end

--- Removes creatures one by one (killing living hostiles first, so nothing fights on).
local function Remove(units)
	TargetUnit("0x0")
	for _, unit in ipairs(units) do
		if UnitExists(unit) then
			GM.DestroyMonster(unit)
			Assert(WaitUntil(function() return not UnitExists(unit) end, 5000, "creature removed"),
				"the creature should be removed")
		end
	end
end

--- Waits for the next tick of spellId's DoT on target and returns the damage the server logged for
--- it (PeriodicAuraLog). Not read off the health bar: the creature's regeneration rides along with
--- the same health update and shrinks the drop by a varying amount.
local function NextTick(target, spellId)
	local ticks = PeriodicTickCount(target, spellId)
	Assert(WaitUntil(function() return PeriodicTickCount(target, spellId) > ticks end, 6000, "dot ticks"),
		"the DoT " .. spellId .. " should tick on the target")
	return LastPeriodicAmount(target, spellId)
end

-- =============================================================================================
-- Blight: Rot Field with Miasma and Creeping Rot
-- =============================================================================================

-- The character stands 12 yards from the dummy (spells need it in front at some distance).
local dummy = GM.CreateMonster(TRAINING_DUMMY)
GM.Worldport(0, homeX + 12, homeY, homeZ, 0)
Assert(WaitUntil(function() return GetDistance(me, dummy) > 10 end, 10000, "stepped away from the dummy"),
	"the character should stand twelve yards from the dummy")

--- Casts Rot Field at the dummy and counts the field's ticks on it (the dummy's health drops) over
--- watchMs, noting whether the Creeping Rot slow showed up. Counted on the dummy, not by SpellGo: the
--- field ticks once per enemy inside it, and the shared map may hold other creatures nearby.
local function RotFieldTicks(watchMs)
	HealUnit(dummy)
	Cast(ROT_FIELD, dummy)
	local drops = 0
	local slowed = false
	local last = GetHealth(dummy)
	for i = 1, watchMs / 100 do
		Sleep(100)
		slowed = slowed or HasAura(dummy, CREEPING_ROT_SLOW)
		local health = GetHealth(dummy)
		if health < last then
			drops = drops + 1
		end
		last = health
	end
	return drops, slowed
end

-- Base: four ticks (2/4/6/8 s), no slow, no Corrupting Touch.
local baseTicks, baseSlowed = RotFieldTicks(11000)
Assert(baseTicks >= 3 and baseTicks <= 4, "an 8 s Rot Field should tick 3-4 times on the dummy, ticked " .. baseTicks)
Assert(not baseSlowed, "without Creeping Rot the field should not slow")
Assert(not HasAura(dummy, CORRUPTING_TOUCH), "without Miasma the field should not apply Corrupting Touch")

-- Creeping Rot: two more ticks, and the field slows.
Learn(CREEPING_ROT)
local creepingTicks, slowed = RotFieldTicks(15000)
Log("Rot Field ticked " .. baseTicks .. " times without and " .. creepingTicks .. " times with Creeping Rot")
Assert(slowed, "with Creeping Rot the field should slow the dummy")
Assert(creepingTicks >= baseTicks + 2, "Creeping Rot should keep the field 4 s (two ticks) longer: " ..
	baseTicks .. " -> " .. creepingTicks .. " ticks")
Assert(not HasAura(dummy, CORRUPTING_TOUCH), "without Miasma the field should not apply Corrupting Touch")

-- Miasma: the field afflicts the enemies it damages with the acolyte's Corrupting Touch.
Learn(MIASMA)
local touchesBefore = SpellGoCount(me, CORRUPTING_TOUCH)
HealUnit(dummy)
Cast(ROT_FIELD, dummy)
Assert(WaitUntil(function() return HasAura(dummy, CORRUPTING_TOUCH) end, 5000, "miasma"),
	"with Miasma the field should afflict the dummy with Corrupting Touch")
Assert(SpellGoCount(me, CORRUPTING_TOUCH) > touchesBefore, "Miasma's Corrupting Touch should come from the acolyte")
Log("Miasma put Corrupting Touch on the dummy")

-- =============================================================================================
-- Dread: curses with Lingering Dread, Grasping Dread and Tangled Tongue
-- =============================================================================================

Learn(LINGERING_DREAD)
Learn(GRASPING_DREAD)
Learn(TANGLED_TONGUE)

Afflict(CURSE_OF_FRAILTY, dummy)
Assert(WaitUntil(function() return HasAura(dummy, LINGERING_DREAD_AURA) end, 3000, "lingering dread"),
	"with Lingering Dread Curse of Frailty should also cut the dummy's healing taken")

Afflict(CURSE_OF_THE_MIRE, dummy)
Assert(WaitUntil(function() return HasAura(dummy, GRASPING_DREAD_ROOT) end, 3000, "grasping dread"),
	"with Grasping Dread Curse of the Mire should also root the dummy")
Assert(not HasAura(dummy, CURSE_OF_FRAILTY), "Curse of the Mire should replace Curse of Frailty")

Afflict(TANGLED_TONGUE, dummy)
Assert(WaitUntil(function() return not HasAura(dummy, CURSE_OF_THE_MIRE) end, 3000, "tongue replaces mire"),
	"Tangled Tongue is a curse too and should replace Curse of the Mire")
Log("Lingering Dread, Grasping Dread and the Tangled Tongue curse rule verified")

-- =============================================================================================
-- Embrace of Dread: +20% on a Wasting Plague tick
-- =============================================================================================

-- Two fresh dummies (the first one carries Miasma's Corrupting Touch), well outside the last Rot
-- Field (8 yards around the first dummy), which may still be ticking.
local plain = SpawnAt(TRAINING_DUMMY, -12, 14)
local embraced = SpawnAt(TRAINING_DUMMY, -12, -14)

Afflict(WASTING_PLAGUE, plain)
local plainTick = NextTick(plain, WASTING_PLAGUE)

Learn(EMBRACE_OF_DREAD)
Cast(EMBRACE_OF_DREAD)
Assert(WaitUntil(function() return HasAura(me, EMBRACE_OF_DREAD) end, 3000, "embraced"),
	"Embrace of Dread should buff the acolyte")
Afflict(WASTING_PLAGUE, embraced)
local embracedTick = NextTick(embraced, WASTING_PLAGUE)
Log("Wasting Plague tick: " .. plainTick .. " without, " .. embracedTick .. " with Embrace of Dread")
-- Ticks are whole numbers, so allow for rounding around the 20%.
Assert(embracedTick >= plainTick * 1.15 and embracedTick <= plainTick * 1.3 + 1,
	"Embrace of Dread should raise the Wasting Plague tick by 20% (" .. plainTick .. " -> " .. embracedTick .. ")")

Remove({ dummy, plain, embraced })

-- =============================================================================================
-- Venomous Backlash: melee attackers catch Wasting Plague
-- =============================================================================================

Learn(VENOMOUS_BACKLASH)
HealSelf()
local plaguesBefore = SpellGoCount(me, WASTING_PLAGUE)
local bandits = {}
for i = 1, 3 do
	bandits[i] = SpawnAt(FOREST_BANDIT, 6, (i - 2) * 3)
end
-- Low-level bandits do not aggro a level-12 character on their own (they go for the town's NPCs):
-- a curse pulls each of them onto the acolyte.
for _, bandit in ipairs(bandits) do
	Afflict(CURSE_OF_FRAILTY, bandit)
end
Assert(WaitUntil(function()
	for _, bandit in ipairs(bandits) do
		if GetTarget(bandit) ~= me then return false end
	end
	return true
end, 10000, "bandits attack the acolyte"), "the cursed bandits should attack the acolyte")
local infected = WaitUntil(function()
	if GetHealth(me) < GetMaxHealth(me) * 0.5 then
		HealSelf()
	end
	for _, bandit in ipairs(bandits) do
		if HasAura(bandit, WASTING_PLAGUE) then
			return true
		end
	end
	return false
end, 75000, "an attacker catches the plague")
Assert(infected, "a bandit hitting the acolyte in melee should catch Wasting Plague")
Assert(SpellGoCount(me, WASTING_PLAGUE) > plaguesBefore, "the plague should come from the acolyte")
Log("Venomous Backlash infected a melee attacker")
Remove(bandits)

-- =============================================================================================
-- Wail of the Lost: up to three enemies within 8 yards flee
-- =============================================================================================

Learn(WAIL_OF_THE_LOST)
HealSelf()
bandits = {}
for i = 1, 4 do
	bandits[i] = SpawnAt(FOREST_BANDIT, 3, (i - 2.5) * 2)
end
for _, bandit in ipairs(bandits) do
	Assert(GetDistance(me, bandit) < 7, "the bandits should stand within 8 yards (" .. GetDistance(me, bandit) .. ")")
end
Cast(WAIL_OF_THE_LOST)
Sleep(1000)
local feared = 0
for _, bandit in ipairs(bandits) do
	if HasAura(bandit, WAIL_OF_THE_LOST) then
		feared = feared + 1
	end
end
Log("Wail of the Lost feared " .. feared .. " of 4 bandits")
Assert(feared == 3, "Wail of the Lost should fear exactly three of four nearby enemies, feared " .. feared)
Remove(bandits)
HealSelf()

-- =============================================================================================
-- Plaguebearer: Corrupting Touch spreads to up to two enemies when its victim dies
-- =============================================================================================

Learn(PLAGUEBEARER)
-- Three dummies stand around the spot where the victim will die; the acolyte stands a few yards off.
local receivers = {
	SpawnAt(TRAINING_DUMMY, 3, -3),
	SpawnAt(TRAINING_DUMMY, 3, 0),
	SpawnAt(TRAINING_DUMMY, 3, 3),
}
local victim = SpawnAt(FOREST_BANDIT, 3, 1)
Afflict(CORRUPTING_TOUCH, victim)
TargetUnit(victim)
GM.KillTarget()
Assert(WaitUntil(function() return not IsAlive(victim) end, 5000, "victim dies"), "the bandit should die")
TargetUnit("0x0")
local function Spread()
	local count = 0
	for _, receiver in ipairs(receivers) do
		if HasAura(receiver, CORRUPTING_TOUCH) then
			count = count + 1
		end
	end
	return count
end
Assert(WaitUntil(function() return Spread() >= 2 end, 5000, "touch spreads"),
	"Plaguebearer should spread Corrupting Touch to two nearby enemies, spread to " .. Spread())
Sleep(1500)
Assert(Spread() == 2, "Plaguebearer should spread to at most two enemies, spread to " .. Spread())
Log("Plaguebearer spread Corrupting Touch to two dummies")
Remove(receivers)
Remove({ victim })

-- =============================================================================================
-- Tangled Tongue: a creature's casts take 30% longer
-- =============================================================================================

HealSelf()
local mage = SpawnAt(BANDIT_MAGE, 0, 0)
-- Stand off, so the mage casts at range rather than meleeing.
GM.Worldport(0, GetPosX(me) + 15, GetPosY(me), GetPosZ(me), 0)
Assert(WaitUntil(function()
	if GetHealth(me) < GetMaxHealth(me) * 0.5 then
		HealSelf()
	end
	return CastStartCount(mage) >= 1
end, 20000, "mage casts"), "the Bandit Mage should cast at the acolyte")
Assert(LastCastTime(mage) == BANDIT_MAGE_SPELL_CAST_TIME,
	"the Bandit Mage's cast should take 1.5 s before the curse, took " .. LastCastTime(mage) .. " ms")
Afflict(TANGLED_TONGUE, mage)
local casts = CastStartCount(mage)
Assert(WaitUntil(function()
	if GetHealth(me) < GetMaxHealth(me) * 0.5 then
		HealSelf()
	end
	return CastStartCount(mage) > casts
end, 20000, "mage casts cursed"), "the cursed Bandit Mage should cast again")
Log("Bandit Mage cast time: " .. BANDIT_MAGE_SPELL_CAST_TIME .. " ms before, " .. LastCastTime(mage) .. " ms with Tangled Tongue")
Assert(LastCastTime(mage) >= BANDIT_MAGE_SPELL_CAST_TIME * 1.25,
	"Tangled Tongue should make the mage's cast 30% longer, took " .. LastCastTime(mage) .. " ms")
Remove({ mage })
HealSelf()
