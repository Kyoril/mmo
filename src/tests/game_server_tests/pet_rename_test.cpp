// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Renaming the summoned persistent pet (GamePlayerS::RenamePersistentPet), the state the PetSpells
// packet carries (GamePlayerS::GetPetSpellsData), and stored pet names fitting the realm's
// character_pets.name column.

#include "catch.hpp"
#include "persistent_pet_fixture.h"

#include "game/pet_packets.h"

using namespace mmo;
using namespace mmo::pet_tests;

namespace
{
	/// Summons a stored beast pet (an entry that allows player naming unless told otherwise).
	GameCreatureS* SummonBeast(PersistentPetFixture& fixture, GamePlayerS& player, const bool allowNaming)
	{
		fixture.beastPet->set_allow_player_naming(allowNaming);
		PersistentPetData beast = fixture.MakeStoredPet(BeastPetId, 2);
		beast.unitEntry = BoarUnitId;
		beast.name = "Fido";
		player.SetStoredPets({ beast });
		return fixture.Summon(player, BeastPetId);
	}
}

TEST_CASE_METHOD(PersistentPetFixture, "a valid pet name is accepted and normalized", "[pets][rename]")
{
	const auto player = SpawnPlayer(4);
	GameCreatureS* pet = SummonBeast(*this, *player, true);
	REQUIRE(pet);

	const uint32 spellsChangedBefore = watcher.spellsChanged;
	const uint32 savesBefore = storedPetsChangedCount;

	CHECK(player->RenamePersistentPet("sHADE") == pet_result::Ok);

	CHECK(player->GetStoredPets().front().name == "Shade");
	CHECK(pet->GetName() == "Shade");
	CHECK(watcher.spellsChanged == spellsChangedBefore + 1);
	CHECK(storedPetsChangedCount == savesBefore + 1);
	CHECK(player->GetPetSpellsData().name == "Shade");
}

TEST_CASE_METHOD(PersistentPetFixture, "an invalid pet name is refused and nothing changes", "[pets][rename]")
{
	const auto player = SpawnPlayer(4);
	GameCreatureS* pet = SummonBeast(*this, *player, true);
	REQUIRE(pet);

	const uint32 spellsChangedBefore = watcher.spellsChanged;
	const uint32 savesBefore = storedPetsChangedCount;
	const size_t resultsBefore = watcher.results.size();

	const char* name = GENERATE("", "ThirteenChars", "Sh4de", "Sha de");
	CAPTURE(name);

	CHECK(player->RenamePersistentPet(name) == pet_result::InvalidName);

	CHECK(player->GetStoredPets().front().name == "Fido");
	CHECK(pet->GetName() == "Fido");
	CHECK(watcher.spellsChanged == spellsChangedBefore);
	CHECK(storedPetsChangedCount == savesBefore);
	REQUIRE(watcher.results.size() == resultsBefore + 1);
	CHECK(watcher.results.back() == pet_result::InvalidName);
}

TEST_CASE_METHOD(PersistentPetFixture, "a pet whose entry forbids naming cannot be renamed", "[pets][rename]")
{
	const auto player = SpawnPlayer(4);
	GameCreatureS* pet = SummonBeast(*this, *player, false);
	REQUIRE(pet);

	const uint32 savesBefore = storedPetsChangedCount;

	CHECK(player->RenamePersistentPet("Shade") == pet_result::NamingNotAllowed);

	CHECK(player->GetStoredPets().front().name == "Fido");
	CHECK(pet->GetName() == "Fido");
	CHECK(storedPetsChangedCount == savesBefore);
	REQUIRE_FALSE(watcher.results.empty());
	CHECK(watcher.results.back() == pet_result::NamingNotAllowed);
	CHECK_FALSE(player->GetPetSpellsData().canRename);
}

TEST_CASE_METHOD(PersistentPetFixture, "renaming without a summoned persistent pet fails", "[pets][rename]")
{
	const auto player = SpawnPlayer(4);
	beastPet->set_allow_player_naming(true);
	PersistentPetData beast = MakeStoredPet(BeastPetId, 2);
	beast.name = "Fido";
	player->SetStoredPets({ beast });

	CHECK(player->RenamePersistentPet("Shade") == pet_result::NoPet);
	CHECK(player->GetStoredPets().front().name == "Fido");
	REQUIRE_FALSE(watcher.results.empty());
	CHECK(watcher.results.back() == pet_result::NoPet);
}

TEST_CASE_METHOD(PersistentPetFixture, "renaming with the PETS subsystem off fails", "[pets][rename]")
{
	const auto player = SpawnPlayer(4);
	REQUIRE(SummonBeast(*this, *player, true));

	watcher.petsAvailable = false;
	CHECK(player->RenamePersistentPet("Shade") == pet_result::Disabled);
	CHECK(player->GetStoredPets().front().name == "Fido");
}

TEST_CASE_METHOD(PersistentPetFixture, "PetSpells data describes the summoned persistent pet", "[pets][rename]")
{
	const auto player = SpawnPlayer(4);

	SECTION("no persistent pet")
	{
		const game::PetSpellsData data = player->GetPetSpellsData();
		CHECK(data.petGuid == 0);
		CHECK(data.petEntry == 0);
		CHECK(data.name.empty());
		CHECK(data.spells.empty());
	}

	SECTION("a summoned own-level pet")
	{
		GameCreatureS* pet = SummonBeast(*this, *player, true);
		REQUIRE(pet);

		// Teaching it a spell makes it part of the list.
		REQUIRE(player->LearnPetSpell(ClawSpellId) == pet_result::Ok);

		const game::PetSpellsData data = player->GetPetSpellsData();
		CHECK(data.petGuid == pet->GetGuid());
		CHECK(data.petEntry == BeastPetId);
		CHECK(data.name == "Fido");
		CHECK(data.levelMode == pet_level_mode::OwnLevel);
		CHECK(data.strategy == pet_strategy::Assist);
		CHECK(data.canRename);
		CHECK(std::find(data.spells.begin(), data.spells.end(), ClawSpellId) != data.spells.end());

		// The strategy is the pet AI's.
		player->GetPets().Command(pet_command::SetStrategy, 0, pet_strategy::Passive);
		CHECK(player->GetPetSpellsData().strategy == pet_strategy::Passive);
	}

	SECTION("a dismissed pet is no persistent pet")
	{
		REQUIRE(SummonBeast(*this, *player, true));
		player->DismissPersistentPet(false);
		CHECK(player->GetPetSpellsData().petGuid == 0);
	}
}

TEST_CASE_METHOD(PersistentPetFixture, "a tamed creature's long name is clamped to the stored name size", "[pets][rename]")
{
	// "Forest Boar" padded with multi-byte characters past MaxStoredPetNameBytes.
	String longName = "Ancient Forest Boar of the ";
	for (int i = 0; i < 10; ++i)
	{
		longName += "\xC3\xA9";	// U+00E9, two bytes
	}
	REQUIRE(longName.size() > MaxStoredPetNameBytes);
	boarUnit->set_name(longName);

	const auto player = SpawnPlayer(4);
	const auto boar = SpawnCreature(*boarUnit);
	REQUIRE(player->TameCreature(*boar) == pet_result::Ok);

	REQUIRE(player->GetStoredPets().size() == 1);
	const String& stored = player->GetStoredPets().front().name;
	CHECK(stored.size() <= MaxStoredPetNameBytes);
	CHECK(stored == ClampStoredPetName(longName));

	// Cut at a character boundary: no dangling lead byte.
	REQUIRE_FALSE(stored.empty());
	CHECK((static_cast<uint8>(stored.back()) & 0xC0) != 0xC0);
}

TEST_CASE_METHOD(PersistentPetFixture, "renaming a pet to its current name changes nothing", "[pets][rename]")
{
	const auto player = SpawnPlayer(4);
	GameCreatureS* pet = SummonBeast(*this, *player, true);
	REQUIRE(pet);

	const uint32 spellsChangedBefore = watcher.spellsChanged;
	const uint32 savesBefore = storedPetsChangedCount;

	bool changed = true;
	CHECK(player->RenamePersistentPet("fido", &changed) == pet_result::Ok);
	CHECK_FALSE(changed);
	CHECK(watcher.spellsChanged == spellsChangedBefore);
	CHECK(storedPetsChangedCount == savesBefore);

	CHECK(player->RenamePersistentPet("Shade", &changed) == pet_result::Ok);
	CHECK(changed);
}
