// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Persistent pet spells are checked before the cast starts: a summon, grant, tame or revive that
// is bound to fail is refused with a spell cast result instead of spending the cast.

#include "catch.hpp"
#include "persistent_pet_fixture.h"

using namespace mmo;
using namespace mmo::pet_tests;

namespace
{
	constexpr uint32 GrantImpSpellId = 302;
	constexpr uint32 RevivePetSpellId = 303;

	void AddCasterSpell(proto::Project& project, const uint32 id, const uint32 effectType, const int32 miscValue)
	{
		auto* spell = project.spells.add(id);
		spell->set_name("Pet Spell");
		spell->add_attributes(0);
		spell->add_attributes(0);
		auto* effect = spell->add_effects();
		effect->set_index(0);
		effect->set_type(effectType);
		effect->set_targeta(spell_effect_targets::Caster);
		effect->set_miscvaluea(miscValue);
	}

	struct PetCastFixture : PersistentPetFixture
	{
		PetCastFixture()
		{
			AddCasterSpell(project, GrantImpSpellId, spell_effects::GrantPersistentPet, ImpPetId);
			AddCasterSpell(project, RevivePetSpellId, spell_effects::RevivePet, 0);
		}

		/// Fills every stored pet slot with pets of entries nobody casts for.
		std::vector<PersistentPetData> FullStable()
		{
			std::vector<PersistentPetData> pets;
			for (uint32 i = 0; i < MaxStoredPets; ++i)
			{
				pets.push_back(MakeStoredPet(ScholarPetId));
			}
			return pets;
		}
	};
}

TEST_CASE_METHOD(PetCastFixture, "summoning without a stored pet is refused before the cast", "[pets][cast_checks]")
{
	const auto player = SpawnPlayer(4);
	CHECK(player->CanSummonStoredPet(0) == pet_result::NoPet);
	CHECK(CastAt(*player, CallPetSpellId, nullptr) == spell_cast_result::FailedNoPet);
	CHECK(player->GetStoredPets().empty());
}

TEST_CASE_METHOD(PetCastFixture, "summoning a dead tamed pet is refused before the cast", "[pets][cast_checks]")
{
	const auto player = SpawnPlayer(4);
	PersistentPetData dead = MakeStoredPet(BeastPetId, 2);
	dead.unitEntry = BoarUnitId;
	dead.isDead = true;
	player->SetStoredPets({ dead });

	CHECK(player->CanSummonStoredPet(0) == pet_result::PetDead);
	CHECK(CastAt(*player, CallPetSpellId, nullptr) == spell_cast_result::FailedPetDead);
}

TEST_CASE_METHOD(PetCastFixture, "summoning another class's pet is refused before the cast", "[pets][cast_checks]")
{
	const auto player = SpawnPlayer(4);
	player->SetStoredPets({ MakeStoredPet(ImpPetId, 1, OtherClassId) });

	CHECK(player->CanSummonStoredPet(0) == pet_result::WrongClass);
	CHECK(CastAt(*player, CallPetSpellId, nullptr) == spell_cast_result::FailedPetWrongClass);
}

TEST_CASE_METHOD(PetCastFixture, "a summonable pet passes the check", "[pets][cast_checks]")
{
	const auto player = SpawnPlayer(4);
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });

	CHECK(player->CanSummonStoredPet(0) == pet_result::Ok);
	CHECK(CastAt(*player, CallPetSpellId, nullptr) == spell_cast_result::CastOkay);
	CHECK(RunUntil([&] { return PersistentPetOf(*player) != nullptr; }));
}

TEST_CASE_METHOD(PetCastFixture, "granting a pet that is stored already or without a free slot is refused before the cast", "[pets][cast_checks]")
{
	const auto player = SpawnPlayer(4);

	SECTION("already stored")
	{
		player->SetStoredPets({ MakeStoredPet(ImpPetId) });
		CHECK(player->CanGrantPersistentPet(ImpPetId) == pet_result::AlreadyHavePet);
		CHECK(CastAt(*player, GrantImpSpellId, nullptr) == spell_cast_result::FailedAlreadyHavePet);
	}

	SECTION("no free slot")
	{
		player->SetStoredPets(FullStable());
		CHECK(player->CanGrantPersistentPet(ImpPetId) == pet_result::NoFreeSlot);
		CHECK(CastAt(*player, GrantImpSpellId, nullptr) == spell_cast_result::FailedNoFreePetSlot);
	}

	CHECK(requests.empty());
}

TEST_CASE_METHOD(PetCastFixture, "taming what cannot be tamed is refused before the cast", "[pets][cast_checks]")
{
	SECTION("a creature without a pet entry")
	{
		const auto player = SpawnPlayer(4);
		const auto rock = SpawnCreature(*rockUnit);
		CHECK(player->CanTameCreature(*rock) == pet_result::NotTamable);
		CHECK(CastAt(*player, TameSpellId, rock.get()) == spell_cast_result::FailedNotTamable);
		CHECK(rock->GetWorldInstance() != nullptr);
	}

	SECTION("a creature above the caster's level")
	{
		const auto player = SpawnPlayer(1);
		const auto boar = SpawnCreature(*boarUnit);
		CHECK(player->CanTameCreature(*boar) == pet_result::TooHighLevel);
		CHECK(CastAt(*player, TameSpellId, boar.get()) == spell_cast_result::FailedTargetTooHighLevel);
	}

	SECTION("without a free slot")
	{
		const auto player = SpawnPlayer(4);
		player->SetStoredPets(FullStable());
		const auto boar = SpawnCreature(*boarUnit);
		CHECK(CastAt(*player, TameSpellId, boar.get()) == spell_cast_result::FailedNoFreePetSlot);
	}

	SECTION("a player")
	{
		const auto player = SpawnPlayer(4);
		const auto other = SpawnPlayer(4);
		CHECK(CastAt(*player, TameSpellId, other.get()) == spell_cast_result::FailedNotTamable);
	}

	CHECK(requests.empty());
}

TEST_CASE_METHOD(PetCastFixture, "a tamable creature passes the check", "[pets][cast_checks]")
{
	const auto player = SpawnPlayer(4);
	const auto boar = SpawnCreature(*boarUnit);
	CHECK(player->CanTameCreature(*boar) == pet_result::Ok);
	CHECK(CastAt(*player, TameSpellId, boar.get()) == spell_cast_result::CastOkay);
	CHECK(requests.size() == 1);
}

TEST_CASE_METHOD(PetCastFixture, "reviving without a dead pet is refused before the cast", "[pets][cast_checks]")
{
	const auto player = SpawnPlayer(4);
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });

	CHECK(player->CanRevivePet(0) == pet_result::NoPet);
	CHECK(CastAt(*player, RevivePetSpellId, nullptr) == spell_cast_result::FailedNoPet);

	PersistentPetData dead = MakeStoredPet(BeastPetId, 2);
	dead.unitEntry = BoarUnitId;
	dead.isDead = true;
	player->SetStoredPets({ dead });
	CHECK(player->CanRevivePet(0) == pet_result::Ok);
	CHECK(CastAt(*player, RevivePetSpellId, nullptr) == spell_cast_result::CastOkay);
}
