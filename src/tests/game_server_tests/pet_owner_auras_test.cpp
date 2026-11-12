// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Owner auras that empower the persistent pet: ModPetStatsPct scales the pet's damage or health,
// PetPassive puts a passive spell on the pet while the owner aura exists, and the effect target
// Pet resolves to the caster's summoned persistent pet.

#include "catch.hpp"
#include "persistent_pet_fixture.h"

#include "game/aura.h"

using namespace mmo;
using namespace mmo::pet_tests;

namespace
{
	constexpr uint32 PetDamageTalentId = 400;
	constexpr uint32 PetHealthTalentId = 401;
	constexpr uint32 PetPassiveTalentId = 402;
	constexpr uint32 PetHardenSpellId = 403;
	constexpr uint32 EmpowerPetSpellId = 404;

	struct PetOwnerAuraFixture : PersistentPetFixture
	{
		PetOwnerAuraFixture()
		{
			impUnit->set_minmeleedmg(10.0f);
			impUnit->set_maxmeleedmg(20.0f);

			AddAuraSpell(PetDamageTalentId, true, aura_type::ModPetStatsPct, 30, spell_effect_targets::Caster, 0);
			AddAuraSpell(PetHealthTalentId, true, aura_type::ModPetStatsPct, 20, spell_effect_targets::Caster, 1);

			auto* petPassive = AddAuraSpell(PetPassiveTalentId, true, aura_type::PetPassive, 0, spell_effect_targets::Caster, 0);
			petPassive->mutable_effects(0)->set_triggerspell(PetHardenSpellId);

			AddAuraSpell(PetHardenSpellId, true, aura_type::ModDamageTakenPct, -20, spell_effect_targets::Caster, 0);
			AddAuraSpell(EmpowerPetSpellId, false, aura_type::ModDamageTakenPct, -10, spell_effect_targets::Pet, 0);
		}

		proto::SpellEntry* AddAuraSpell(const uint32 id, const bool passive, const aura_type::Type aura, const int32 basePoints,
			const spell_effect_targets::Type target, const int32 miscValueA)
		{
			auto* spell = project.spells.add(id);
			spell->set_name("Owner Aura");
			spell->add_attributes(passive ? spell_attributes::Passive : 0);
			spell->add_attributes(0);
			auto* effect = spell->add_effects();
			effect->set_index(0);
			effect->set_type(spell_effects::ApplyAura);
			effect->set_aura(static_cast<uint32>(aura));
			effect->set_basepoints(basePoints);
			effect->set_miscvaluea(miscValueA);
			effect->set_targeta(target);
			return spell;
		}

		/// Dismisses the persistent pet and waits until it left the world.
		void Dismiss(GamePlayerS& player)
		{
			const std::weak_ptr<GameObjectS> weakPet = PersistentPetOf(player)->shared_from_this();
			player.DismissPersistentPet(false);
			REQUIRE(RunUntil([&] { return weakPet.expired(); }));
		}
	};
}

TEST_CASE_METHOD(PetOwnerAuraFixture, "ModPetStatsPct damage scales the summoned pet's melee damage", "[pet_owner_auras]")
{
	const auto player = SpawnPlayer(4);
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });

	GameCreatureS* pet = Summon(*player, ImpPetId);
	const float baseMin = pet->Get<float>(object_fields::MinDamage);
	const float baseMax = pet->Get<float>(object_fields::MaxDamage);
	REQUIRE(baseMin == Approx(10.0f));
	REQUIRE(baseMax == Approx(20.0f));
	Dismiss(*player);

	player->AddSpell(PetDamageTalentId);
	CHECK(player->GetPetStatMultiplier(pet_stat_kind::Damage) == Approx(1.3f));
	CHECK(player->GetPetStatMultiplier(pet_stat_kind::Health) == Approx(1.0f));

	pet = Summon(*player, ImpPetId);
	CHECK(pet->Get<float>(object_fields::MinDamage) == Approx(baseMin * 1.3f));
	CHECK(pet->Get<float>(object_fields::MaxDamage) == Approx(baseMax * 1.3f));
	CHECK(pet->GetMaxHealth() == 200);

	// Losing the talent while the pet is out takes the bonus away again.
	player->RemoveSpell(PetDamageTalentId);
	CHECK(pet->Get<float>(object_fields::MinDamage) == Approx(baseMin));
	CHECK(pet->Get<float>(object_fields::MaxDamage) == Approx(baseMax));
}

TEST_CASE_METHOD(PetOwnerAuraFixture, "ModPetStatsPct health scales the pet's max health and keeps its health ratio", "[pet_owner_auras]")
{
	const auto player = SpawnPlayer(4);
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });

	player->AddSpell(PetHealthTalentId);
	GameCreatureS* pet = Summon(*player, ImpPetId);

	// 100 from the creature x 2.0 from the pet entry x 1.2 from the owner.
	CHECK(pet->GetMaxHealth() == 240);
	CHECK(pet->GetHealth() == 240);

	// Removed while the pet is out: back to base, current health clamped to the new max.
	player->RemoveSpell(PetHealthTalentId);
	CHECK(pet->GetMaxHealth() == 200);
	CHECK(pet->GetHealth() == 200);

	// Applied while the pet is out and hurt: the health ratio is kept.
	pet->Set<uint32>(object_fields::Health, 100);
	player->AddSpell(PetHealthTalentId);
	CHECK(pet->GetMaxHealth() == 240);
	CHECK(pet->GetHealth() == 120);

	player->RemoveSpell(PetHealthTalentId);
	CHECK(pet->GetMaxHealth() == 200);
	CHECK(pet->GetHealth() == 100);
}

TEST_CASE_METHOD(PetOwnerAuraFixture, "ModPetStatsPct survives the pet's own stat refresh", "[pet_owner_auras]")
{
	const auto player = SpawnPlayer(4);
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });
	player->AddSpell(PetHealthTalentId);
	GameCreatureS* pet = Summon(*player, ImpPetId);

	// Anything that refreshes the pet's stats (a level up, an aura on the pet) keeps the bonus.
	pet->SetLevel(5);
	CHECK(pet->GetMaxHealth() == 240);
}

TEST_CASE_METHOD(PetOwnerAuraFixture, "PetPassive puts its spell on the persistent pet while the owner aura exists", "[pet_owner_auras]")
{
	const auto player = SpawnPlayer(4);
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });

	SECTION("learned before the summon")
	{
		player->AddSpell(PetPassiveTalentId);
		GameCreatureS* pet = Summon(*player, ImpPetId);
		CHECK(pet->HasAuraSpell(PetHardenSpellId));
		CHECK(pet->GetIncomingDamageTakenMultiplier(nullptr, spell_dmg_class::Melee) == Approx(0.8f));
	}

	SECTION("learned while the pet is out")
	{
		GameCreatureS* pet = Summon(*player, ImpPetId);
		REQUIRE_FALSE(pet->HasAuraSpell(PetHardenSpellId));
		player->AddSpell(PetPassiveTalentId);
		CHECK(pet->HasAuraSpell(PetHardenSpellId));
	}

	// Dismiss and summon again: the new pet gets it, once.
	Dismiss(*player);
	GameCreatureS* pet = Summon(*player, ImpPetId);
	CHECK(pet->HasAuraSpell(PetHardenSpellId));
	uint32 hardenAuras = 0;
	pet->ForEachAura([&hardenAuras](const AuraContainer& aura)
		{
			if (aura.GetSpellId() == PetHardenSpellId)
			{
				++hardenAuras;
			}
		});
	CHECK(hardenAuras == 1);

	// A talent reset removes the owner aura, and with it the pet's passive.
	player->RemoveSpell(PetPassiveTalentId);
	CHECK_FALSE(pet->HasAuraSpell(PetHardenSpellId));

	// And it does not come back with the next summon.
	Dismiss(*player);
	pet = Summon(*player, ImpPetId);
	CHECK_FALSE(pet->HasAuraSpell(PetHardenSpellId));
}

TEST_CASE_METHOD(PetOwnerAuraFixture, "pet empowerment comes back after the owner leaves and re-enters the world", "[pet_owner_auras]")
{
	const auto player = SpawnPlayer(4);
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });
	player->AddSpell(PetPassiveTalentId);
	player->AddSpell(PetHealthTalentId);
	GameCreatureS* pet = Summon(*player, ImpPetId);
	const std::weak_ptr<GameObjectS> weakPet = pet->shared_from_this();

	// Logout: the pet is stored as summoned, the owner's auras are cleared.
	world->RemoveGameObject(*player);
	REQUIRE(RunUntil([&] { return weakPet.expired(); }));
	CHECK(player->GetPetStatMultiplier(pet_stat_kind::Health) == Approx(1.0f));

	// Login: passive talents are cast again and the pet comes back empowered.
	world->AddGameObject(*player);
	REQUIRE(RunUntil([&] { return PersistentPetOf(*player) != nullptr; }));
	pet = PersistentPetOf(*player);
	CHECK(pet->HasAuraSpell(PetHardenSpellId));
	CHECK(pet->GetMaxHealth() == 240);
}

TEST_CASE_METHOD(PetOwnerAuraFixture, "a spell targeting the pet fails without one and reaches the pet otherwise", "[pet_owner_auras]")
{
	const auto player = SpawnPlayer(4);
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });

	CHECK(CastAt(*player, EmpowerPetSpellId, nullptr) == spell_cast_result::FailedNoPet);

	GameCreatureS* pet = Summon(*player, ImpPetId);
	REQUIRE(CastAt(*player, EmpowerPetSpellId, nullptr) == spell_cast_result::CastOkay);
	CHECK(RunUntil([&] { return pet->HasAuraSpell(EmpowerPetSpellId); }));
	CHECK_FALSE(player->HasAuraSpell(EmpowerPetSpellId));
}
