// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// GM pet commands: taming that ignores the level check, removing a stored pet, and making any
// creature the GM's temporary pet.

#include "catch.hpp"
#include "persistent_pet_fixture.h"

using namespace mmo;
using namespace mmo::pet_tests;

TEST_CASE_METHOD(PersistentPetFixture, "making a hostile creature a pet makes it friendly and following", "[pets][gm]")
{
	const auto gm = SpawnPlayer(1);
	const auto boar = SpawnCreature(*boarUnit);
	REQUIRE(boar->UnitIsEnemy(*gm));

	CHECK(MakeGmPet(*gm, *boar) == pet_result::Ok);

	CHECK(boar->IsPet());
	CHECK(boar->GetOwnerGuid() == gm->GetGuid());
	CHECK(boar->GetSummonOwner() == gm);
	CHECK(boar->UnitIsFriendly(*gm));
	CHECK_FALSE(boar->UnitIsEnemy(*gm));
	CHECK(boar->GetPetGroup() == GmPetGroup);
	CHECK(gm->GetPets().GetGroupCount(GmPetGroup) == 1);
	CHECK(gm->GetPets().HasPet(*boar));
	CHECK_FALSE(boar->GrantsKillRewards());

	// It follows its owner: the pet state is active.
	REQUIRE(boar->GetAI() != nullptr);
	CHECK(boar->GetAI()->IsPetMode());
}

TEST_CASE_METHOD(PersistentPetFixture, "making pets has no cap", "[pets][gm]")
{
	const auto gm = SpawnPlayer(1);
	std::vector<std::shared_ptr<GameCreatureS>> boars;
	for (int i = 0; i < 8; ++i)
	{
		boars.push_back(SpawnCreature(*boarUnit));
		CHECK(MakeGmPet(*gm, *boars.back()) == pet_result::Ok);
	}

	CHECK(gm->GetPets().GetGroupCount(GmPetGroup) == 8);
}

TEST_CASE_METHOD(PersistentPetFixture, "making a pet refuses dead creatures and other pets", "[pets][gm]")
{
	const auto gm = SpawnPlayer(1);
	const auto other = SpawnPlayer(1);

	const auto dead = SpawnCreature(*boarUnit);
	dead->Kill(nullptr);
	CHECK(MakeGmPet(*gm, *dead) == pet_result::NotTamable);

	const auto owned = SpawnCreature(*boarUnit);
	owned->SetOwnerGuid(other->GetGuid());
	CHECK(MakeGmPet(*gm, *owned) == pet_result::NotTamable);
}

TEST_CASE_METHOD(PersistentPetFixture, "gm taming ignores the level check but not the rest", "[pets][gm]")
{
	const auto player = SpawnPlayer(1);
	const auto boar = SpawnCreature(*boarUnit);
	boar->SetLevel(5);

	SECTION("normal taming refuses a too high level creature")
	{
		CHECK(player->TameCreature(*boar) == pet_result::TooHighLevel);
	}

	SECTION("gm taming tames it")
	{
		CHECK(player->TameCreature(*boar, true) == pet_result::Ok);
		REQUIRE(requests.size() == 1);
		CHECK(requests.front().petEntry == BeastPetId);
	}

	SECTION("gm taming still needs a pet entry")
	{
		const auto rock = SpawnCreature(*rockUnit);
		CHECK(player->TameCreature(*rock, true) == pet_result::NotTamable);
	}

	SECTION("gm taming still needs a free slot")
	{
		std::vector<PersistentPetData> pets;
		for (uint32 i = 0; i < MaxStoredPets; ++i)
		{
			pets.push_back(MakeStoredPet(ImpPetId));
		}
		player->SetStoredPets(pets);
		CHECK(player->TameCreature(*boar, true) == pet_result::NoFreeSlot);
	}
}

TEST_CASE_METHOD(PersistentPetFixture, "removing a stored pet deletes it", "[pets][gm]")
{
	const auto player = SpawnPlayer(4);
	player->SetStoredPets({ MakeStoredPet(ImpPetId), MakeStoredPet(BeastPetId, 1) });
	const uint32 changedBefore = storedPetsChangedCount;

	CHECK(player->RemoveStoredPet(ImpPetId) == pet_result::Ok);
	REQUIRE(player->GetStoredPets().size() == 1);
	CHECK(player->GetStoredPets().front().petEntry == BeastPetId);
	CHECK(storedPetsChangedCount > changedBefore);

	CHECK(player->RemoveStoredPet(ImpPetId) == pet_result::NoPet);
}

TEST_CASE_METHOD(PersistentPetFixture, "removing the summoned pet dismisses it first", "[pets][gm]")
{
	const auto player = SpawnPlayer(4);
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });
	GameCreatureS* pet = Summon(*player, ImpPetId);
	REQUIRE(pet != nullptr);

	CHECK(player->RemoveStoredPet(ImpPetId) == pet_result::Ok);
	CHECK(player->GetStoredPets().empty());
	CHECK(RunUntil([&] { return PersistentPetOf(*player) == nullptr && CountCreaturesInWorld(ImpUnitId) == 0; }));
}
