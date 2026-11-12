-- e2e-class: 3
-- e2e-own-character: sh
-- e2e-timeout: 300
--
-- Persistent pets: the acolyte's Duskling (pet entry 1, creature 122).
--
-- GM.PetAdd stores the duskling, the way Malrik Duskwright's Bind Duskling does at the end of the
-- acolyte's quest line (acolyte_shadow_binding.lua covers that path); Summon Duskling calls it. The scenario checks that it follows and assists its
-- owner, that dismissing and summoning it again brings back the same pet (guid, name, level), that
-- a rename sticks and survives a relog (the realm stores the pet, and a pet that was out when its
-- owner left comes back with them), and the class lock: a class switch dismisses the duskling, it
-- cannot be summoned by another class, and it is still there when the acolyte comes back.
--
-- Own character: it binds a duskling and changes class. Acolyte = class 3.

local SUMMON_SHADOW = 302
local DISMISS_SHADOW = 303
local GLOOM_DART = 304
local SHADE_PET = 1
local UMBRAL_SHADE = 122
local SUMMON_COMPANION = 310
local BECOME_MAGE = 230
local BECOME_ACOLYTE = 233
local FROST_ARMOR = 6
local TRAINING_DUMMY = 40
local IN_COMBAT = 0x1
local PET_RESULT_INVALID_NAME = 9
local NEW_NAME = "Duskmoth"
-- spell_cast_result, as LastCastResult reports it
local FAILED_PET_WRONG_CLASS = "failed:26"

local me = Me()
local homeX, homeY, homeZ = GetPosX(me), GetPosY(me), GetPosZ(me)

-- Waits until a cast request has resolved and returns LastCastResult.
local function CastResult(spellId, target)
	GM.ResetCooldowns()
	GM.RestorePower()
	CastSpell(spellId, target)
	Assert(WaitUntil(function()
		local result = LastCastResult()
		return result == "ok" or result:find("failed") ~= nil
	end, 15000, "cast " .. spellId .. " resolves"), "the cast of " .. spellId .. " should resolve")
	return LastCastResult()
end

local function OutOfCombat(what)
	Assert(WaitUntil(function() return (GetUnitFlags(me) & IN_COMBAT) == 0 end, 20000, "out of combat"),
		"the character should leave combat " .. what)
end

-- Summons the duskling and returns its guid once it is in the world.
local function Summon(what)
	local result = CastResult(SUMMON_SHADOW, me)
	Assert(result == "ok", what .. ": Summon Duskling should be cast, got " .. result)
	Assert(WaitUntil(function() return GetPet() ~= nil and UnitExists(GetPet()) end, 10000, what .. ": duskling appears"),
		what .. ": the duskling should appear")
	local pet = GetPet()
	Assert(GetOwner(pet) == me, what .. ": the duskling should be owned by the character")
	Assert(GetPetEntry() == SHADE_PET, what .. ": PetSpells should name pet entry 1, got " .. GetPetEntry())
	return pet
end

local function Dismissed(pet, what)
	Assert(WaitUntil(function() return GetPet() == nil end, 10000, what .. ": pet state cleared"),
		what .. ": the client should be told there is no pet any more")
	Assert(WaitUntil(function() return not UnitExists(pet) end, 10000, what .. ": duskling leaves"),
		what .. ": the duskling should leave the world")
end

-- ---------------------------------------------------------------------------------------------
-- Bind and summon
-- ---------------------------------------------------------------------------------------------

-- A rerun of this scenario (the runner retries infrastructure failures) finds the duskling of the
-- first attempt: remove it, so the scenario starts from a character without one.
TargetUnit("0x0")
GM.PetRemove(SHADE_PET)
Sleep(500)
Assert(GetPet() == nil, "the acolyte has no pet out")
GM.PetAdd(SHADE_PET)
GM.LearnSpell(SUMMON_SHADOW)
GM.LearnSpell(DISMISS_SHADOW)
Assert(WaitUntil(function() return HasSpell(SUMMON_SHADOW) and HasSpell(DISMISS_SHADOW) end, 10000, "summon spells learned"),
	"Summon Duskling and Dismiss Duskling should be learned")

-- The realm creates the stored pet asynchronously; the summon succeeds once it exists.
local pet = nil
Assert(WaitUntil(function()
	local result = CastResult(SUMMON_SHADOW, me)
	return result == "ok"
end, 20000, "stored duskling summonable"), "Summon Duskling should succeed once the bound duskling is stored")
Assert(WaitUntil(function() return GetPet() ~= nil and UnitExists(GetPet()) end, 10000, "duskling appears"),
	"the duskling should appear")
pet = GetPet()
Assert(GetOwner(pet) == me, "the duskling should be owned by the character")
Assert(GetPetEntry() == SHADE_PET, "PetSpells should name pet entry 1")
Assert(PetKnowsSpell(GLOOM_DART), "the duskling should know its default spell Gloom Dart")

local petName = GetPetName()
local petLevel = GetLevel(pet)
Assert(petName ~= "", "the duskling should have a name")
Assert(petLevel >= 1, "the duskling should have a level")
Log("Summoned " .. petName .. " (" .. pet .. "), level " .. petLevel)

-- ---------------------------------------------------------------------------------------------
-- Follow and assist
-- ---------------------------------------------------------------------------------------------

Assert(MoveTo(homeX + 15, homeY, homeZ, 30000), "the character should walk 15 m")
Assert(WaitUntil(function() return GetDistance(me, pet) < 6 end, 20000, "duskling follows"),
	"the duskling should follow its owner (distance " .. GetDistance(me, pet) .. ")")

-- Assist: the duskling joins the fight its owner starts.
local dummy = GM.CreateMonster(TRAINING_DUMMY)
GM.Worldport(0, GetPosX(me) + 2, GetPosY(me), GetPosZ(me), 0)
TargetUnit(dummy)
FaceUnit(dummy)
StartAttack(dummy)
Assert(WaitUntil(function() return GetTarget(pet) == dummy end, 15000, "duskling assists"),
	"the duskling should attack its owner's target")
Assert(WaitUntil(function() return SpellGoCount(pet, GLOOM_DART) >= 1 end, 20000, "duskling casts"),
	"the assisting duskling should cast Gloom Dart at the target")
StopAttack()
TargetUnit("0x0")
GM.DestroyMonster(dummy)

-- The Attack command sends it on its own; Follow calls it back.
dummy = GM.CreateMonster(TRAINING_DUMMY)
PetAttack(dummy)
Assert(WaitUntil(function() return GetTarget(pet) == dummy end, 10000, "pet attack command"),
	"the duskling should attack the target of an Attack command")
PetFollow()
GM.DestroyMonster(dummy)
OutOfCombat("after the fight")
Assert(WaitUntil(function() return GetDistance(me, pet) < 6 end, 15000, "duskling returns"),
	"the duskling should return to its owner")

-- ---------------------------------------------------------------------------------------------
-- Dismiss and summon again: the same pet comes back
-- ---------------------------------------------------------------------------------------------

local dismiss = CastResult(DISMISS_SHADOW, me)
Assert(dismiss == "ok", "Dismiss Duskling should be cast, got " .. dismiss)
Dismissed(pet, "dismiss")

local again = Summon("resummon")
Assert(again == pet, "the resummoned duskling should be the same pet (guid " .. pet .. ", got " .. again .. ")")
Assert(GetPetName() == petName, "the resummoned duskling should keep its name " .. petName .. ", got " .. GetPetName())
Assert(GetLevel(again) == petLevel, "the resummoned duskling should keep its level " .. petLevel .. ", got " .. GetLevel(again))

-- ---------------------------------------------------------------------------------------------
-- Rename
-- ---------------------------------------------------------------------------------------------

ClearLastPetResult()
PetRename("Bad1")
Assert(WaitUntil(function() return LastPetResult() == PET_RESULT_INVALID_NAME end, 10000, "invalid name refused"),
	"a pet name with a digit should be refused with InvalidName, got " .. LastPetResult())
Assert(GetPetName() == petName, "a refused rename should not change the name")

PetRename(string.lower(NEW_NAME))
Assert(WaitUntil(function() return GetPetName() == NEW_NAME end, 10000, "renamed"),
	"the duskling should be renamed (normalized to " .. NEW_NAME .. "), is " .. GetPetName())

-- ---------------------------------------------------------------------------------------------
-- Relog: the summoned duskling comes back with its owner, still renamed
-- ---------------------------------------------------------------------------------------------

local petBeforeRelog = pet
ExpectDisconnect()
Assert(LoginElsewhere(15000), "second login should be accepted")
Assert(WaitUntil(function() return IsDisconnected() end, 15000, "session displaced"), "first session should be kicked")
Assert(Reconnect(30000), "should log back into the world")
me = Me()

Assert(WaitUntil(function() return GetPet() ~= nil and UnitExists(GetPet()) end, 15000, "duskling back after relog"),
	"the duskling that was out at logout should come back with its owner")
pet = GetPet()
Assert(pet == petBeforeRelog, "the returning duskling should be the same pet (guid " .. petBeforeRelog .. ", got " .. pet .. ")")
Assert(GetOwner(pet) == me, "the returning duskling should be owned by the character")
Assert(GetPetName() == NEW_NAME, "the duskling should keep its new name over a relog, is " .. GetPetName())
Assert(GetLevel(pet) == petLevel, "the duskling should keep its level over a relog")

-- ---------------------------------------------------------------------------------------------
-- Class lock
-- ---------------------------------------------------------------------------------------------

OutOfCombat("before the class switch")
for _, spellId in ipairs({ BECOME_MAGE, BECOME_ACOLYTE }) do
	GM.LearnSpell(spellId)
	Assert(WaitUntil(function() return HasSpell(spellId) end, 10000, "class change " .. spellId .. " learned"),
		"class-change spell " .. spellId .. " should be learned")
end
local toMage = CastResult(BECOME_MAGE, me)
Assert(toMage == "ok", "switching to Mage should be cast, got " .. toMage)
Assert(WaitUntil(function() return HasSpell(FROST_ARMOR) end, 15000, "now a mage"), "the character should be a mage")
Dismissed(pet, "class switch")

-- A mage cannot summon the acolyte's duskling. Summon Duskling is an acolyte spell the mage does not
-- know, so try the class-less test spell Summon Companion (310: SummonPersistentPet, miscvaluea 0 = the
-- stored pet summoned last, which is the duskling): the class lock must refuse it.
Assert(not HasSpell(SUMMON_SHADOW), "a mage should not know the acolyte's Summon Duskling")
GM.LearnSpell(SUMMON_COMPANION)
Assert(WaitUntil(function() return HasSpell(SUMMON_COMPANION) end, 10000, "Summon Companion learned"), "Summon Companion should be learned")
local wrongClass = CastResult(SUMMON_COMPANION, me)
Assert(wrongClass == FAILED_PET_WRONG_CLASS, "a mage's Summon Companion should not summon the acolyte's duskling, got " .. wrongClass)
Sleep(1500)
Assert(GetPet() == nil and CountUnitsOwnedBy(me, UMBRAL_SHADE) == 0, "no duskling should appear for the mage")

-- Back as an acolyte the duskling is still bound, and Summon Duskling is known again.
local toAcolyte = CastResult(BECOME_ACOLYTE, me)
Assert(toAcolyte == "ok", "switching back to Acolyte should be cast, got " .. toAcolyte)
Assert(WaitUntil(function() return not HasSpell(FROST_ARMOR) end, 15000, "acolyte again"), "the character should be an acolyte again")
Assert(WaitUntil(function() return HasSpell(SUMMON_SHADOW) and HasSpell(DISMISS_SHADOW) end, 10000, "summon spells back"),
	"back as an acolyte the character should know Summon Duskling and Dismiss Duskling again")
local back = Summon("back as acolyte")
Assert(GetPetName() == NEW_NAME, "the duskling should still be " .. NEW_NAME .. " after the class round trip")

-- Leave the character as it started for a rerun: no pet out.
local cleanup = CastResult(DISMISS_SHADOW, me)
Assert(cleanup == "ok", "Dismiss Duskling should be cast at the end, got " .. cleanup)
Dismissed(back, "cleanup")
Log("Persistent duskling: follow, assist, resummon, rename, relog and class lock verified")
