-- e2e-own-character: gpc
-- e2e-timeout: 180
--
-- The GM pet commands petadd, petremove, pettame and petmake (CheatPet* opcodes, gated at GM
-- level 2; the test account has GM level 3, so they must work here).
--
-- Uses the taming test kit: pet entry 2 "Wolf" (creature 44), which Young Forest Wolf (creature
-- 44, tame_pet_entry 2) tames into, and Summon Companion (spell 310: SummonPersistentPet, miscvaluea 0 =
-- the stored pet summoned last), learned with learnspell. The Training Dummy (creature 40) is not
-- tamable. Own character: it ends up owning pets.

local SUMMON_COMPANION = 310
local WOLF_PET = 2
local YOUNG_FOREST_WOLF = 44
local TRAINING_DUMMY = 40

-- pet_result
local NO_PET = 2
local NOT_TAMABLE = 4
local ALREADY_HAVE_PET = 7
local FAILED_NO_PET = "failed:24" -- spell_cast_result::FailedNoPet

local me = Me()
local homeX, homeY, homeZ = GetPosX(me), GetPosY(me), GetPosZ(me)

-- The wolves are hostile; the test is about the commands, not about surviving them.
GM.Godmode(true)

GM.LearnSpell(SUMMON_COMPANION)
Assert(WaitUntil(function() return HasSpell(SUMMON_COMPANION) end, 10000, "Summon Companion learned"), "Summon Companion should be learned")

-- A rerun (the runner retries infrastructure failures) starts without the wolf of the last attempt.
TargetUnit("0x0")
GM.PetRemove(WOLF_PET)
Sleep(500)

-- Waits until a cast request has resolved and returns LastCastResult.
local function CastResult(spellId)
	GM.ResetCooldowns()
	CastSpell(spellId, me)
	Assert(WaitUntil(function()
		local result = LastCastResult()
		return result == "ok" or result:find("failed") ~= nil
	end, 10000, "cast " .. spellId .. " resolves"), "the cast of " .. spellId .. " should resolve")
	return LastCastResult()
end

local function WaitForPetResult(expected, what)
	Assert(WaitUntil(function() return LastPetResult() == expected end, 10000, what),
		what .. ": expected pet result " .. expected .. ", got " .. LastPetResult())
	ClearLastPetResult()
end

-- ---------------------------------------------------------------------------------------------
-- petadd: grants a stored pet, which Summon Companion summons
-- ---------------------------------------------------------------------------------------------

ClearLastPetResult()
GM.PetAdd(WOLF_PET)

-- The realm stores the new pet asynchronously; Summon Companion succeeds once it exists.
Assert(WaitUntil(function() return CastResult(SUMMON_COMPANION) == "ok" end, 20000, "added pet summonable"),
	"Summon Companion should summon the pet petadd granted")
Assert(WaitUntil(function() return GetPet() ~= nil and UnitExists(GetPet()) end, 10000, "added pet appears"),
	"the added pet should appear")
local added = GetPet()
Assert(GetPetEntry() == WOLF_PET, "the summoned pet should be of pet entry 2, is " .. GetPetEntry())
Assert(GetOwner(added) == me, "the added pet should be owned by the character")
Assert(GetPetName() == "Wolf", "pet entry 2 has the fixed name Wolf, got " .. GetPetName())
Log("petadd granted " .. GetPetName() .. " (" .. added .. ")")

-- One stored pet per pet entry.
GM.PetAdd(WOLF_PET)
WaitForPetResult(ALREADY_HAVE_PET, "second petadd")

-- ---------------------------------------------------------------------------------------------
-- petremove: deletes the stored pet, dismissing it first
-- ---------------------------------------------------------------------------------------------

GM.PetRemove(WOLF_PET)
Assert(WaitUntil(function() return GetPet() == nil end, 10000, "pet state cleared"),
	"petremove should dismiss the summoned pet")
Assert(WaitUntil(function() return not UnitExists(added) end, 10000, "removed pet leaves"),
	"the removed pet should leave the world")
local callRemoved = CastResult(SUMMON_COMPANION)
Assert(callRemoved == FAILED_NO_PET, "Summon Companion should fail with NoPet without a stored pet, got " .. callRemoved)

GM.PetRemove(WOLF_PET)
WaitForPetResult(NO_PET, "petremove of a pet the character does not have")

-- ---------------------------------------------------------------------------------------------
-- pettame: the selected creature becomes a stored pet and is summoned
-- ---------------------------------------------------------------------------------------------

local dummy = GM.CreateMonster(TRAINING_DUMMY)
TargetUnit(dummy)
GM.PetTame()
WaitForPetResult(NOT_TAMABLE, "pettame on the Training Dummy")
Assert(UnitExists(dummy) and GetOwner(dummy) == "0x0", "the dummy should stay an ordinary creature")
TargetUnit("0x0")
GM.DestroyMonster(dummy)

local wild = GM.CreateMonster(YOUNG_FOREST_WOLF)
Assert(GetOwner(wild) == "0x0", "the wild wolf has no owner")
TargetUnit(wild)
GM.PetTame()
Assert(WaitUntil(function() return not UnitExists(wild) end, 10000, "wild wolf leaves"),
	"the tamed creature should leave the world; its pet replaces it")
Assert(WaitUntil(function() return GetPet() ~= nil and UnitExists(GetPet()) end, 15000, "tamed pet appears"),
	"the tamed wolf should be stored and summoned")
local tamed = GetPet()
Assert(tamed ~= wild, "the pet is a new object, not the wild creature")
Assert(GetPetEntry() == WOLF_PET, "a Young Forest Wolf tames into pet entry 2, got " .. GetPetEntry())
Assert(GetOwner(tamed) == me, "the tamed pet should be owned by the character")
Log("pettame produced " .. GetPetName() .. " (" .. tamed .. ")")
TargetUnit("0x0")

-- ---------------------------------------------------------------------------------------------
-- petmake: the selected creature becomes a temporary pet without a duration
-- ---------------------------------------------------------------------------------------------

local made = GM.CreateMonster(YOUNG_FOREST_WOLF)
TargetUnit(made)
GM.PetMake()
Assert(WaitUntil(function() return GetOwner(made) == me end, 10000, "creature made a pet"),
	"petmake should make the selected creature the character's pet")
Assert(GetPet() == tamed, "a temporary pet does not replace the summoned persistent pet")
Assert(CountUnitsOwnedBy(me) == 2, "the character should own the tamed and the made pet, owns " .. CountUnitsOwnedBy(me))

-- A pet cannot be made a pet again.
GM.PetMake()
WaitForPetResult(NOT_TAMABLE, "petmake on a pet")
TargetUnit("0x0")

-- Both pets follow their owner.
Assert(MoveTo(homeX + 15, homeY, homeZ, 30000), "the character should walk 15 m")
Assert(WaitUntil(function() return GetDistance(me, made) < 6 and GetDistance(me, tamed) < 6 end, 20000, "pets follow"),
	"the made pet and the tamed pet should follow their owner")

-- Cleanup: the made wolf has no duration, the tamed one is stored.
GM.DestroyMonster(made)
GM.PetRemove(WOLF_PET)
Assert(WaitUntil(function() return GetPet() == nil end, 10000, "tamed pet removed"), "petremove should remove the tamed pet")
GM.Godmode(false)
Log("petadd, petremove, pettame and petmake verified")
