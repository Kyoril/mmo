-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
--
-- Pet (PetInfo). No pet by default; scenarios/pet.lua summons one by filling UiTest.data.pet and
-- firing PET_CHANGED. Spells are tables with the SpellEntry fields the UI reads, indices 0-based
-- like the client. Commands print what the client would send, so clicks show up in ui_test.log.

UiTest.data.pet = nil

local function Pet()
	return UiTest.data.pet
end

function HasPetUI()
	return Pet() ~= nil
end

function HasPersistentPet()
	return Pet() ~= nil and Pet().persistent == true
end

function HasTemporaryPets()
	return Pet() ~= nil and Pet().persistent ~= true
end

function GetNumPetSpells()
	return Pet() ~= nil and #Pet().spells or 0
end

function GetPetSpell(index)
	return Pet() ~= nil and Pet().spells[index + 1] or nil
end

function GetPetSpellCooldown(index)
	local spell = GetPetSpell(index)
	return spell ~= nil and (spell.cooldownRemaining or 0) or 0
end

function GetPetSpellCooldownDuration(index)
	local spell = GetPetSpell(index)
	return spell ~= nil and (spell.cooldownDuration or 0) or 0
end

function GetPetStrategy()
	return Pet() ~= nil and Pet().strategy or 0
end

function GetPetLevelMode()
	return Pet() ~= nil and Pet().levelMode or 0
end

function GetPetXp()
	if Pet() == nil then
		return 0, 0
	end
	return Pet().xp, Pet().nextLevelXp
end

function CanRenamePet()
	return Pet() ~= nil and Pet().persistent == true
end

function PetCastSpell(index) print("PetCastSpell", index) end
function PetAttack() print("PetAttack") end
function PetFollow() print("PetFollow") end
function PetStay() print("PetStay") end
function PetSetStrategy(strategy) print("PetSetStrategy", strategy) end
function PetDismiss() print("PetDismiss") end
function PetRename(name) print("PetRename", name) end
