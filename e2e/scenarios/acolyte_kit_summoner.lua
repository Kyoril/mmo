-- e2e-class: 3
-- e2e-own-character: ks
-- e2e-timeout: 420
--
-- The acolyte's Summoner talent tree, observed on a live server. The talent rank spells are learned
-- with GM.LearnSpell, which applies the same passive aura the talent would.
--
-- - Dark Bond (1340 / 1341): the Duskling's max health rises by 10% / 20%.
-- - Empower Duskling (1345, active): the Duskling carries the empowerment aura.
-- - Soul Siphon (1351): Drain Essence (1331) ticks (1332) four times instead of three.
-- - Shared Night (1348): every Drain Essence tick heals the Duskling (1349).
-- - Swarm Lore (1342): Call Gloomlings (1335) calls three Gloomlings (creature 123) instead of two,
--   and they stay about 5 s longer.
-- - Dark Tutelage (1350): Call Gloomlings' cooldown is 60 s instead of 90 s.
-- - Brood Horror (1352, active): a Brood Horror (creature 124) attacks the caster's target.
-- - Gloom Tether (1346): Corrupting Touch (24) ticks sometimes (10%) call a Gloomling (1347).
--
-- Not observable here: Dark Bond's damage part, Empower Duskling's attack speed and Shadow
-- Sympathy (1343 -> 1344, a hidden passive aura on the pet: no AuraUpdate, no SpellGo, and
-- GM.DamageTarget bypasses damage-taken modifiers). Unit tests cover those.
--
-- Own character: it is levelled to 12, binds a Duskling and keeps the talent passives.

local CORRUPTING_TOUCH = 24
local DRAIN_ESSENCE, DRAIN_TICK = 1331, 1332
local CALL_GLOOMLINGS, GLOOMLING = 1335, 123
local SUMMON_DUSKLING, DISMISS_DUSKLING, DUSKLING_PET = 302, 303, 1
local DARK_BOND_1, DARK_BOND_2 = 1340, 1341
local SWARM_LORE = 1342
local EMPOWER_DUSKLING = 1345
local GLOOM_TETHER, GLOOM_TETHER_SUMMON = 1346, 1347
local SHARED_NIGHT, SHARED_NIGHT_HEAL = 1348, 1349
local DARK_TUTELAGE = 1350
local SOUL_SIPHON = 1351
local BROOD_HORROR, BROOD_HORROR_UNIT = 1352, 124
local TRAINING_DUMMY = 40

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

for _, spellId in ipairs({ CORRUPTING_TOUCH, DRAIN_ESSENCE, CALL_GLOOMLINGS, SUMMON_DUSKLING, DISMISS_DUSKLING }) do
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

--- Seconds from now until no creature of entry owned by the character is left.
local function SecondsUntilGone(entry, timeoutMs)
	local started = os.time()
	Assert(WaitUntil(function() return CountUnitsOwnedBy(me, entry) == 0 end, timeoutMs, "summons leave"),
		"the summoned creatures " .. entry .. " should leave")
	return os.time() - started
end

-- ---------------------------------------------------------------------------------------------
-- The Duskling
-- ---------------------------------------------------------------------------------------------

-- A retried attempt finds the duskling of the first one.
TargetUnit("0x0")
GM.PetRemove(DUSKLING_PET)
Sleep(500)
GM.PetAdd(DUSKLING_PET)
-- The realm creates the stored pet asynchronously; the summon succeeds once it exists.
for attempt = 1, 6 do
	TargetUnit("0x0")
	GM.ResetCooldowns()
	GM.RestorePower()
	CastSpell(SUMMON_DUSKLING, me)
	if WaitUntil(function() return GetPet() ~= nil and UnitExists(GetPet()) end, 6000, "duskling appears") then
		break
	end
end
local pet = GetPet()
Assert(pet ~= nil and UnitExists(pet), "Summon Duskling should call the bound duskling")
Assert(GetOwner(pet) == me, "the duskling should belong to the character")

-- ---------------------------------------------------------------------------------------------
-- Dark Bond: Duskling health +10% / +20%
-- ---------------------------------------------------------------------------------------------

local baseHealth = GetMaxHealth(pet)
Log("Duskling max health without Dark Bond: " .. baseHealth)
Learn(DARK_BOND_1)
Assert(WaitUntil(function() return GetMaxHealth(pet) >= math.floor(baseHealth * 1.10) end, 5000, "dark bond 1"),
	"Dark Bond rank 1 should raise the Duskling's max health by 10% (" .. baseHealth .. " -> " .. GetMaxHealth(pet) .. ")")
Assert(GetMaxHealth(pet) <= math.floor(baseHealth * 1.10) + 1,
	"Dark Bond rank 1 should raise the Duskling's max health by no more than 10% (" .. baseHealth .. " -> " .. GetMaxHealth(pet) .. ")")
Learn(DARK_BOND_2)
Assert(WaitUntil(function() return GetMaxHealth(pet) >= math.floor(baseHealth * 1.20) end, 5000, "dark bond 2"),
	"Dark Bond rank 2 should raise the Duskling's max health by 20% (" .. baseHealth .. " -> " .. GetMaxHealth(pet) .. ")")
Assert(GetMaxHealth(pet) <= math.floor(baseHealth * 1.20) + 1,
	"Dark Bond rank 2 should raise the Duskling's max health by no more than 20%, not stack with rank 1 (" .. baseHealth .. " -> " .. GetMaxHealth(pet) .. ")")
Log("Duskling max health with Dark Bond: " .. GetMaxHealth(pet))

-- ---------------------------------------------------------------------------------------------
-- Empower Duskling
-- ---------------------------------------------------------------------------------------------

Learn(EMPOWER_DUSKLING)
Cast(EMPOWER_DUSKLING)
Assert(WaitUntil(function() return HasAura(pet, EMPOWER_DUSKLING) end, 5000, "duskling empowered"),
	"Empower Duskling should put its aura on the Duskling")
Assert(not HasAura(me, EMPOWER_DUSKLING), "Empower Duskling should not buff the caster")
Log("Duskling empowered")

-- ---------------------------------------------------------------------------------------------
-- Drain Essence: Soul Siphon adds a tick, Shared Night heals the Duskling on every tick
-- ---------------------------------------------------------------------------------------------

local dummy = GM.CreateMonster(TRAINING_DUMMY)
GM.Worldport(0, homeX + 10, homeY, homeZ, 0)
Assert(WaitUntil(function() return GetDistance(me, dummy) > 8 end, 10000, "stepped away from the dummy"),
	"the character should stand ten yards from the dummy")

--- Channels Drain Essence at the dummy and returns the number of ticks and Shared Night heals.
local function Drain()
	-- A dummy that drops to its refill threshold counts as a lost target and ends the channel.
	TargetUnit(dummy)
	GM.Heal()
	Assert(WaitUntil(function() return GetHealth(dummy) == GetMaxHealth(dummy) end, 5000, "dummy healed"),
		"the dummy should be back at full health")
	local ticksBefore = SpellGoCount(me, DRAIN_TICK)
	local healsBefore = SpellGoCount(me, SHARED_NIGHT_HEAL)
	Cast(DRAIN_ESSENCE, dummy)
	Assert(WaitUntil(function() return IsChanneling(me) end, 3000, "drain channels"), "Drain Essence should channel")
	Assert(WaitUntil(function() return not IsChanneling(me) end, 7000, "drain ends"), "the drain channel should end")
	Sleep(300)
	return SpellGoCount(me, DRAIN_TICK) - ticksBefore, SpellGoCount(me, SHARED_NIGHT_HEAL) - healsBefore
end

local ticks, heals = Drain()
Assert(ticks == 3, "without Soul Siphon Drain Essence should tick three times, ticked " .. ticks)
Assert(heals == 0, "without Shared Night no tick should heal the Duskling")

Learn(SOUL_SIPHON)
Learn(SHARED_NIGHT)
TargetUnit(pet)
GM.DamageTarget(math.floor(GetMaxHealth(pet) * 0.5))
TargetUnit("0x0")
Assert(WaitUntil(function() return GetHealth(pet) < GetMaxHealth(pet) * 0.6 end, 5000, "duskling hurt"),
	"the Duskling should be hurt before the drain")
local petHealth = GetHealth(pet)
ticks, heals = Drain()
Log("Drain with Soul Siphon and Shared Night: " .. ticks .. " ticks, " .. heals .. " Duskling heals, Duskling " ..
	petHealth .. " -> " .. GetHealth(pet))
Assert(ticks == 4, "with Soul Siphon Drain Essence should tick four times, ticked " .. ticks)
Assert(heals == ticks, "with Shared Night every drain tick should heal the Duskling (" .. heals .. " heals)")
Assert(GetHealth(pet) > petHealth, "the Duskling's health should rise during the drain")

-- ---------------------------------------------------------------------------------------------
-- Call Gloomlings: Swarm Lore (+1 Gloomling, +5 s) and Dark Tutelage (-30 s cooldown)
-- ---------------------------------------------------------------------------------------------

Cast(CALL_GLOOMLINGS, dummy)
local cooldown = GetSpellCooldownRemaining(CALL_GLOOMLINGS)
Assert(cooldown > 80000, "Call Gloomlings should have a 90 s cooldown, has " .. cooldown .. " ms left")
Assert(WaitUntil(function() return CountUnitsOwnedBy(me, GLOOMLING) == 2 end, 5000, "two gloomlings"),
	"without Swarm Lore Call Gloomlings should call two Gloomlings, has " .. CountUnitsOwnedBy(me, GLOOMLING))
local baseLifetime = SecondsUntilGone(GLOOMLING, 25000)

Learn(SWARM_LORE)
Learn(DARK_TUTELAGE)
Cast(CALL_GLOOMLINGS, dummy)
cooldown = GetSpellCooldownRemaining(CALL_GLOOMLINGS)
Assert(cooldown > 50000 and cooldown <= 60000,
	"with Dark Tutelage Call Gloomlings should have a 60 s cooldown, has " .. cooldown .. " ms left")
Assert(WaitUntil(function() return CountUnitsOwnedBy(me, GLOOMLING) == 3 end, 5000, "three gloomlings"),
	"with Swarm Lore Call Gloomlings should call three Gloomlings, has " .. CountUnitsOwnedBy(me, GLOOMLING))
local loreLifetime = SecondsUntilGone(GLOOMLING, 30000)
Log("Gloomlings stayed " .. baseLifetime .. " s without and " .. loreLifetime .. " s with Swarm Lore")
Assert(loreLifetime >= baseLifetime + 3,
	"Swarm Lore should keep the Gloomlings about 5 s longer (" .. baseLifetime .. " s -> " .. loreLifetime .. " s)")

-- ---------------------------------------------------------------------------------------------
-- Brood Horror
-- ---------------------------------------------------------------------------------------------

Learn(BROOD_HORROR)
Cast(BROOD_HORROR, dummy)
Assert(WaitUntil(function() return CountUnitsOwnedBy(me, BROOD_HORROR_UNIT) == 1 end, 5000, "brood horror"),
	"Brood Horror should call one Brood Horror")
local horror = GetOwnedUnits(me, BROOD_HORROR_UNIT)[1]
Assert(WaitUntil(function() return GetTarget(horror) == dummy end, 8000, "horror attacks"),
	"the Brood Horror should attack the caster's target")
Log("The Brood Horror attacks the dummy")
GM.DestroyMonster(horror)
Assert(WaitUntil(function() return not UnitExists(horror) end, 5000, "horror removed"), "the Brood Horror should be removed")

-- ---------------------------------------------------------------------------------------------
-- Gloom Tether: Corrupting Touch ticks (10%) call a Gloomling
-- ---------------------------------------------------------------------------------------------

Learn(GLOOM_TETHER)

-- Five dummies tick five times as often; about 90 ticks leave a miss chance below 1e-4.
local dummies = { dummy }
for i = 1, 4 do
	GM.Worldport(0, homeX, homeY + 3 * i, homeZ, 0)
	dummies[#dummies + 1] = GM.CreateMonster(TRAINING_DUMMY)
end
GM.Worldport(0, homeX + 10, homeY + 6, homeZ, 0)
Assert(WaitUntil(function() return GetDistance(me, dummies[5]) > 8 end, 10000, "stepped away from the dummies"),
	"the character should stand away from the dummies")

local tethersBefore = SpellGoCount(me, GLOOM_TETHER_SUMMON)
local deadline = os.time() + 75
while SpellGoCount(me, GLOOM_TETHER_SUMMON) == tethersBefore and os.time() < deadline do
	for _, target in ipairs(dummies) do
		if SpellGoCount(me, GLOOM_TETHER_SUMMON) > tethersBefore then
			break
		end
		if not HasAura(target, CORRUPTING_TOUCH) then
			Afflict(CORRUPTING_TOUCH, target)
		end
	end
	Sleep(500)
end
Assert(SpellGoCount(me, GLOOM_TETHER_SUMMON) > tethersBefore, "a Corrupting Touch tick should call a Gloomling")
Assert(WaitUntil(function() return CountUnitsOwnedBy(me, GLOOMLING) >= 1 end, 5000, "tethered gloomling"),
	"Gloom Tether should call a Gloomling to the acolyte's side")
Log("Gloom Tether called a Gloomling")

-- ---------------------------------------------------------------------------------------------
-- Cleanup: no summons, dummies or pet left on the shared map
-- ---------------------------------------------------------------------------------------------

TargetUnit("0x0")
for _, target in ipairs(dummies) do
	GM.DestroyMonster(target)
	Assert(WaitUntil(function() return not UnitExists(target) end, 5000, "dummy removed"), "the dummy should be removed")
end
for _, gloomling in ipairs(GetOwnedUnits(me, GLOOMLING)) do
	GM.DestroyMonster(gloomling)
	Assert(WaitUntil(function() return not UnitExists(gloomling) end, 5000, "gloomling removed"),
		"the Gloomling should be removed")
end
PetDismiss()
Assert(WaitUntil(function() return GetPet() == nil end, 10000, "duskling dismissed"), "the Duskling should be dismissed")
GM.PetRemove(DUSKLING_PET)
