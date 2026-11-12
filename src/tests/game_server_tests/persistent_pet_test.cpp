// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Persistent pets: a character's stored pets are summoned into the world as pet creatures with a
// stable guid, are dismissed (stored with their health and power) on dismiss, death of the owner,
// logout and class switch, come back with their owner, level with their owner or on their own
// experience, and are created by granting or taming. New stored pets are created by the realm; the
// tests stand in for it by answering petCreationRequested with OnPetCreated.

#include "catch.hpp"
#include "persistent_pet_fixture.h"

using namespace mmo;
using namespace mmo::pet_tests;

TEST_CASE("PersistentPetData survives a serialization round trip", "[pets][persistent]")
{
	PersistentPetData data;
	data.dbId = 0x123456789ull;
	data.classId = 7;
	data.petEntry = 3;
	data.unitEntry = 44;
	data.name = "Fluffy";
	data.level = 12;
	data.xp = 345;
	data.health = 678;
	data.power = 90;
	data.isDead = true;
	data.wasSummoned = true;
	data.spells = { 10, 20, 30 };

	std::vector<char> buffer;
	io::VectorSink sink(buffer);
	io::Writer writer(sink);
	writer << data;
	sink.Flush();

	// name: uint8 length + chars; spells: uint16 count + uint32 each.
	const size_t expectedSize = 8 + 4 + 4 + 4 + (1 + 6) + 4 + 4 + 4 + 4 + 1 + 1 + (2 + 3 * 4);
	CHECK(buffer.size() == expectedSize);

	io::MemorySource source(buffer);
	io::Reader reader(source);
	PersistentPetData read;
	REQUIRE(reader >> read);

	CHECK(read.dbId == data.dbId);
	CHECK(read.classId == data.classId);
	CHECK(read.petEntry == data.petEntry);
	CHECK(read.unitEntry == data.unitEntry);
	CHECK(read.name == data.name);
	CHECK(read.level == data.level);
	CHECK(read.xp == data.xp);
	CHECK(read.health == data.health);
	CHECK(read.power == data.power);
	CHECK(read.isDead == data.isDead);
	CHECK(read.wasSummoned == data.wasSummoned);
	CHECK(read.spells == data.spells);
}

TEST_CASE("MakePetGuid is a pet guid that differs per stored pet", "[pets][persistent]")
{
	const uint64 a = MakePetGuid(1, ImpPetId);
	const uint64 b = MakePetGuid(2, ImpPetId);
	CHECK(a != b);
	CHECK(IsPetGUID(a));
	CHECK(IsUnitGUID(a));
	CHECK(GuidEntryPart(a) == ImpPetId);
	CHECK(a == CreateEntryGUID(1, ImpPetId, GuidType::Pet));
}

TEST_CASE_METHOD(PersistentPetFixture, "a summoned stored pet enters the world as its owner's pet", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });
	const uint64 dbId = player->GetStoredPets().front().dbId;

	GameCreatureS* pet = Summon(*player, ImpPetId);
	CHECK(pet->GetGuid() == MakePetGuid(dbId, ImpPetId));
	CHECK(pet->GetOwnerGuid() == player->GetGuid());
	CHECK(pet->IsPet());
	CHECK(pet->GetPetGroup() == 0);
	CHECK(pet->GetName() == "Fido");
	CHECK(pet->GetEntry().id() == ImpUnitId);
	CHECK(pet->GetAI()->IsPetMode());
	CHECK(player->GetPets().HasPet(*pet));

	// MasterLevel with CharacterLevel: the owner's level. Health from the creature times the
	// pet entry's multiplier, and the pet is at full health.
	CHECK(pet->GetLevel() == 4);
	CHECK(pet->GetMaxHealth() == 200);
	CHECK(pet->GetHealth() == 200);

	// Known spells go to the pet AI.
	CHECK(HasSpell(pet->GetAI()->GetPetSpells(), BiteSpellId));

	CHECK(player->GetStoredPets().front().wasSummoned);
	CHECK(watcher.spellsChanged > 0);

	// Summoning the same pet again does not create a second one.
	CHECK(player->SummonStoredPet(ImpPetId) == pet_result::Ok);
	RunFor(std::chrono::milliseconds(50));
	CHECK(CountCreaturesInWorld(ImpUnitId) == 1);
}

TEST_CASE_METHOD(PersistentPetFixture, "summon fails for a pet that is not stored", "[pets][persistent]")
{
	const auto player = SpawnPlayer();
	CHECK(player->SummonStoredPet(ImpPetId) == pet_result::NoPet);
	CHECK(player->SummonStoredPet(0) == pet_result::NoPet);
	REQUIRE_FALSE(watcher.results.empty());
	CHECK(watcher.results.back() == pet_result::NoPet);
}

TEST_CASE_METHOD(PersistentPetFixture, "summon spell with entry 0 calls the most recently summoned pet", "[pets][persistent]")
{
	const auto player = SpawnPlayer();
	player->SetStoredPets({ MakeStoredPet(ImpPetId), MakeStoredPet(ScholarPetId) });

	Summon(*player, ScholarPetId);
	player->DismissPersistentPet(false);
	REQUIRE(RunUntil([&] { return CountCreaturesInWorld(ImpUnitId) == 0; }));

	REQUIRE(CastAt(*player, CallPetSpellId, nullptr) == spell_cast_result::CastOkay);
	REQUIRE(RunUntil([&] { return PersistentPetOf(*player) != nullptr; }));
	CHECK(GuidEntryPart(PersistentPetOf(*player)->GetGuid()) == ScholarPetId);
}

TEST_CASE_METHOD(PersistentPetFixture, "dismissing stores the pet's health and power", "[pets][persistent]")
{
	const auto player = SpawnPlayer();
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });
	GameCreatureS* pet = Summon(*player, ImpPetId);
	const std::weak_ptr<GameObjectS> weakPet = pet->shared_from_this();

	pet->Set<uint32>(object_fields::Health, 77);
	pet->Set<int32>(object_fields::Mana, 21);
	const uint32 changesBefore = storedPetsChangedCount;

	player->DismissPersistentPet(false);

	CHECK(PersistentPetOf(*player) == nullptr);
	const PersistentPetData& stored = player->GetStoredPets().front();
	CHECK(stored.health == 77);
	CHECK(stored.power == 21);
	CHECK_FALSE(stored.wasSummoned);
	CHECK_FALSE(stored.isDead);
	CHECK(storedPetsChangedCount > changesBefore);
	CHECK(RunUntil([&] { return weakPet.expired(); }));

	// It comes back the way it left.
	pet = Summon(*player, ImpPetId);
	CHECK(pet->GetHealth() == 77);
	CHECK(pet->Get<int32>(object_fields::Mana) == 21);
}

TEST_CASE_METHOD(PersistentPetFixture, "owner death dismisses the pet alive and resurrection brings it back", "[pets][persistent]")
{
	const auto player = SpawnPlayer();
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });
	GameCreatureS* pet = Summon(*player, ImpPetId);
	const std::weak_ptr<GameObjectS> weakPet = pet->shared_from_this();

	player->Kill(nullptr);

	CHECK(PersistentPetOf(*player) == nullptr);
	CHECK(RunUntil([&] { return weakPet.expired(); }));
	CHECK(player->GetStoredPets().front().wasSummoned);
	CHECK_FALSE(player->GetStoredPets().front().isDead);

	player->Set<uint32>(object_fields::Health, player->GetMaxHealth());
	player->OnResurrected();
	CHECK(RunUntil([&] { return PersistentPetOf(*player) != nullptr; }));
}

TEST_CASE_METHOD(PersistentPetFixture, "switching the class dismisses the pet and locks it to its class", "[pets][persistent]")
{
	const auto player = SpawnPlayer();
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });
	GameCreatureS* pet = Summon(*player, ImpPetId);
	const std::weak_ptr<GameObjectS> weakPet = pet->shared_from_this();

	REQUIRE(player->ChangeClass(OtherClassId));

	CHECK(PersistentPetOf(*player) == nullptr);
	CHECK(RunUntil([&] { return weakPet.expired(); }));
	CHECK_FALSE(player->GetStoredPets().front().wasSummoned);
	CHECK_FALSE(player->GetStoredPets().front().isDead);

	CHECK(player->SummonStoredPet(ImpPetId) == pet_result::WrongClass);
	CHECK(watcher.results.back() == pet_result::WrongClass);

	REQUIRE(player->ChangeClass(MainClassId));
	Summon(*player, ImpPetId);
}

TEST_CASE_METHOD(PersistentPetFixture, "an own-level pet levels from experience up to its master's level", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);
	PersistentPetData stored = MakeStoredPet(BeastPetId, 1);
	stored.unitEntry = BoarUnitId;
	player->SetStoredPets({ stored });
	GameCreatureS* pet = Summon(*player, BeastPetId);
	REQUIRE(pet->GetLevel() == 1);
	CHECK_FALSE(HasSpell(pet->GetAI()->GetPetSpells(), HowlSpellId));

	// The class curve asks for 100 xp per level.
	player->AddPetExperience(150);
	CHECK(pet->GetLevel() == 2);
	CHECK(player->GetStoredPets().front().level == 2);
	CHECK(player->GetStoredPets().front().xp == 50);
	CHECK(watcher.lastXp == 50);
	CHECK(watcher.lastNextLevelXp == 100);

	// Reaching level 3 teaches the level spell.
	player->AddPetExperience(50);
	CHECK(pet->GetLevel() == HowlLevel);
	CHECK(HasSpell(pet->GetAI()->GetPetSpells(), HowlSpellId));
	CHECK(std::find(watcher.learned.begin(), watcher.learned.end(), HowlSpellId) != watcher.learned.end());

	// Capped at the master's level 4, with the leftover kept below the next level.
	player->AddPetExperience(100000);
	CHECK(pet->GetLevel() == 4);
	CHECK(player->GetStoredPets().front().level == 4);
	CHECK(player->GetStoredPets().front().xp < 100);

	// When the master's level rises, the pet resumes gaining.
	player->SetLevel(5);
	player->AddPetExperience(100);
	CHECK(pet->GetLevel() == 5);
}

TEST_CASE_METHOD(PersistentPetFixture, "an own-level pet is summoned at its stored level, capped by its master", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);
	PersistentPetData stored = MakeStoredPet(BeastPetId, 7);
	stored.unitEntry = BoarUnitId;
	player->SetStoredPets({ stored });

	GameCreatureS* pet = Summon(*player, BeastPetId);
	CHECK(pet->GetLevel() == 4);
}

TEST_CASE_METHOD(PersistentPetFixture, "a master-level pet follows its class level", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);
	REQUIRE(player->GetActiveClassLevel() == 2);
	player->SetStoredPets({ MakeStoredPet(ScholarPetId) });

	GameCreatureS* pet = Summon(*player, ScholarPetId);
	CHECK(pet->GetLevel() == 2);

	player->RewardClassExperience(100);
	REQUIRE(player->GetActiveClassLevel() == 3);
	CHECK(pet->GetLevel() == 3);

	// Experience does nothing for a master-level pet.
	player->AddPetExperience(1000);
	CHECK(pet->GetLevel() == 3);
}

TEST_CASE_METHOD(PersistentPetFixture, "a master-level pet follows its master's character level", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });
	GameCreatureS* pet = Summon(*player, ImpPetId);
	REQUIRE(pet->GetLevel() == 4);

	player->RewardExperience(100);
	REQUIRE(player->GetLevel() == 5);
	CHECK(pet->GetLevel() == 5);
}

TEST_CASE_METHOD(PersistentPetFixture, "taming a creature stores and summons it", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);
	const auto boar = SpawnCreature(*boarUnit);

	CHECK(player->TameCreature(*boar) == pet_result::Ok);

	REQUIRE(requests.size() == 1);
	CHECK(requests.front().dbId == 0);
	CHECK(requests.front().petEntry == BeastPetId);
	CHECK(requests.front().unitEntry == BoarUnitId);
	CHECK(requests.front().classId == MainClassId);
	CHECK(requests.front().level == 2);
	CHECK(requests.front().name == "Forest Boar");

	REQUIRE(player->GetStoredPets().size() == 1);
	CHECK(player->GetStoredPets().front().dbId != 0);

	CHECK(RunUntil([&] { return boar->GetWorldInstance() == nullptr; }));
	REQUIRE(RunUntil([&] { return PersistentPetOf(*player) != nullptr; }));
	GameCreatureS* pet = PersistentPetOf(*player);
	CHECK(pet->GetGuid() == MakePetGuid(player->GetStoredPets().front().dbId, BeastPetId));
	CHECK(pet->GetLevel() == 2);
	CHECK(pet->GetName() == "Forest Boar");
}

TEST_CASE_METHOD(PersistentPetFixture, "taming through the spell effect", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);
	const auto boar = SpawnCreature(*boarUnit);

	REQUIRE(CastAt(*player, TameSpellId, boar.get()) == spell_cast_result::CastOkay);
	CHECK(RunUntil([&] { return PersistentPetOf(*player) != nullptr; }));
	CHECK(player->GetStoredPets().size() == 1);
}

TEST_CASE_METHOD(PersistentPetFixture, "taming refuses what cannot be tamed", "[pets][persistent]")
{
	SECTION("a creature without a pet entry")
	{
		const auto player = SpawnPlayer(4);
		const auto rock = SpawnCreature(*rockUnit);
		CHECK(player->TameCreature(*rock) == pet_result::NotTamable);
	}

	SECTION("a dead creature")
	{
		const auto player = SpawnPlayer(4);
		const auto boar = SpawnCreature(*boarUnit);
		boar->Kill(nullptr);
		CHECK(player->TameCreature(*boar) == pet_result::NotTamable);
	}

	SECTION("somebody else's pet")
	{
		const auto player = SpawnPlayer(4);
		const auto other = SpawnPlayer(4);
		const auto boar = SpawnCreature(*boarUnit);
		boar->SetOwnerGuid(other->GetGuid());
		CHECK(player->TameCreature(*boar) == pet_result::NotTamable);
	}

	SECTION("a creature above the caster's level")
	{
		const auto player = SpawnPlayer(1);
		const auto boar = SpawnCreature(*boarUnit);
		CHECK(player->TameCreature(*boar) == pet_result::TooHighLevel);
	}

	SECTION("without a free stored slot")
	{
		const auto player = SpawnPlayer(4);
		std::vector<PersistentPetData> pets;
		for (uint32 i = 0; i < MaxStoredPets; ++i)
		{
			pets.push_back(MakeStoredPet(ImpPetId));
		}
		player->SetStoredPets(pets);

		const auto boar = SpawnCreature(*boarUnit);
		CHECK(player->TameCreature(*boar) == pet_result::NoFreeSlot);
	}

	SECTION("a creation still pending at the realm counts as a used slot")
	{
		answerCreationRequests = false;
		const auto player = SpawnPlayer(4);
		std::vector<PersistentPetData> pets;
		for (uint32 i = 0; i + 1 < MaxStoredPets; ++i)
		{
			pets.push_back(MakeStoredPet(ImpPetId));
		}
		player->SetStoredPets(pets);

		const auto first = SpawnCreature(*boarUnit);
		const auto second = SpawnCreature(*boarUnit, Vector3(-3.0f, 0.0f, 0.0f));
		CHECK(player->TameCreature(*first) == pet_result::Ok);
		CHECK(player->TameCreature(*second) == pet_result::NoFreeSlot);
	}

	REQUIRE_FALSE(watcher.results.empty());
}

TEST_CASE_METHOD(PersistentPetFixture, "granting a pet stores it once without summoning it", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);

	CHECK(player->GrantPersistentPet(ScholarPetId) == pet_result::Ok);
	REQUIRE(player->GetStoredPets().size() == 1);
	const PersistentPetData& stored = player->GetStoredPets().front();
	CHECK(stored.petEntry == ScholarPetId);
	CHECK(stored.classId == MainClassId);
	CHECK(stored.unitEntry == 0);
	CHECK_FALSE(stored.wasSummoned);
	CHECK((stored.name == "Quill" || stored.name == "Inkwell"));

	RunFor(std::chrono::milliseconds(50));
	CHECK(PersistentPetOf(*player) == nullptr);

	CHECK(player->GrantPersistentPet(ScholarPetId) == pet_result::AlreadyHavePet);
	CHECK(player->GetStoredPets().size() == 1);
}

TEST_CASE_METHOD(PersistentPetFixture, "a creature casting a grant spell binds the pet to its player target", "[pets][persistent]")
{
	constexpr uint32 BindSpellId = 302;
	auto* spell = project.spells.add(BindSpellId);
	spell->set_name("Bind Shadow");
	spell->add_attributes(0);
	spell->add_attributes(0);
	auto* effect = spell->add_effects();
	effect->set_index(0);
	effect->set_type(spell_effects::GrantPersistentPet);
	effect->set_targeta(spell_effect_targets::TargetAny);
	effect->set_miscvaluea(ScholarPetId);

	const auto player = SpawnPlayer(4);
	const auto trainer = SpawnCreature(*boarUnit);

	SpellTargetMap targetMap;
	targetMap.SetTargetMap(spell_cast_target_flags::Unit);
	targetMap.SetUnitTarget(player->GetGuid());
	REQUIRE(trainer->CastSpell(targetMap, *spell, 0, true) == spell_cast_result::CastOkay);

	CHECK(RunUntil([&] { return !player->GetStoredPets().empty(); }));
	REQUIRE(player->GetStoredPets().size() == 1);
	CHECK(player->GetStoredPets().front().petEntry == ScholarPetId);
	CHECK(player->GetStoredPets().front().classId == MainClassId);
	CHECK(PersistentPetOf(*trainer) == nullptr);
}

TEST_CASE_METHOD(PersistentPetFixture, "a dead summoned pet comes back at full health, a dead tamed pet needs a revive", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);
	PersistentPetData tamed = MakeStoredPet(BeastPetId, 2);
	tamed.unitEntry = BoarUnitId;
	player->SetStoredPets({ MakeStoredPet(ImpPetId), tamed });

	SECTION("summon-style pet")
	{
		GameCreatureS* pet = Summon(*player, ImpPetId);
		const std::weak_ptr<GameObjectS> weakPet = pet->shared_from_this();
		pet->Kill(nullptr);

		CHECK(PersistentPetOf(*player) == nullptr);
		const PersistentPetData* stored = player->FindStoredPet(ImpPetId);
		REQUIRE(stored);
		CHECK(stored->isDead);
		CHECK_FALSE(stored->wasSummoned);

		// The corpse goes away shortly.
		CHECK(RunUntil([&] { return weakPet.expired(); }, std::chrono::milliseconds(8000)));

		pet = Summon(*player, ImpPetId);
		CHECK(pet->IsAlive());
		CHECK(pet->GetHealth() == pet->GetMaxHealth());
		CHECK_FALSE(player->FindStoredPet(ImpPetId)->isDead);
	}

	SECTION("tamed pet")
	{
		GameCreatureS* pet = Summon(*player, BeastPetId);
		pet->Kill(nullptr);
		CHECK(player->FindStoredPet(BeastPetId)->isDead);

		CHECK(player->SummonStoredPet(BeastPetId) == pet_result::PetDead);

		CHECK(player->RevivePet(BeastPetId, 50) == pet_result::Ok);
		CHECK_FALSE(player->FindStoredPet(BeastPetId)->isDead);
		REQUIRE(RunUntil([&] { return PersistentPetOf(*player) != nullptr; }, std::chrono::milliseconds(8000)));
		pet = PersistentPetOf(*player);
		CHECK(pet->GetHealth() == pet->GetMaxHealth() / 2);
	}
}

TEST_CASE_METHOD(PersistentPetFixture, "a pet learns a spell", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);
	CHECK(player->LearnPetSpell(ClawSpellId) == pet_result::NoPet);

	player->SetStoredPets({ MakeStoredPet(ImpPetId) });
	GameCreatureS* pet = Summon(*player, ImpPetId);

	CHECK(player->LearnPetSpell(ClawSpellId) == pet_result::Ok);
	CHECK(HasSpell(pet->GetAI()->GetPetSpells(), ClawSpellId));
	const auto& spells = player->GetStoredPets().front().spells;
	CHECK(std::find(spells.begin(), spells.end(), ClawSpellId) != spells.end());
	CHECK(std::find(watcher.learned.begin(), watcher.learned.end(), ClawSpellId) != watcher.learned.end());
}

TEST_CASE_METHOD(PersistentPetFixture, "the owner leaving the world mid-fight stores the pet as summoned", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });
	GameCreatureS* pet = Summon(*player, ImpPetId);
	const std::weak_ptr<GameObjectS> weakPet = pet->shared_from_this();

	const auto boar = SpawnCreature(*boarUnit, Vector3(1.0f, 0.0f, 0.0f));
	pet->GetAI()->OnPetCommand(pet_command::Attack, boar->GetGuid(), 0);
	pet->Set<uint32>(object_fields::Health, 33);
	REQUIRE(RunUntil([&] { return pet->IsInCombat(); }));

	const uint32 changesBefore = storedPetsChangedCount;
	world->RemoveGameObject(*player);

	const PersistentPetData& stored = player->GetStoredPets().front();
	CHECK(stored.wasSummoned);
	CHECK(stored.health > 0);
	CHECK(stored.health <= 33);
	CHECK(storedPetsChangedCount > changesBefore);
	CHECK(RunUntil([&] { return weakPet.expired(); }));

	// Back in the world (login, map change), the pet is summoned again.
	world->AddGameObject(*player);
	CHECK(RunUntil([&] { return PersistentPetOf(*player) != nullptr; }));
}

TEST_CASE_METHOD(PersistentPetFixture, "the pet's state is stored before the owner's despawned signal saves it", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });
	GameCreatureS* pet = Summon(*player, ImpPetId);
	pet->Set<uint32>(object_fields::Health, 41);

	// The world node saves the character (and its pets) from the despawned signal.
	bool saved = false;
	uint32 savedHealth = 0;
	bool savedWasSummoned = false;
	const scoped_connection onDespawned{ player->despawned.connect([&](GameObjectS&)
		{
			saved = true;
			const PersistentPetData& stored = player->GetStoredPets().front();
			savedHealth = stored.health;
			savedWasSummoned = stored.wasSummoned;
		}) };

	world->RemoveGameObject(*player);

	REQUIRE(saved);
	CHECK(savedHealth == 41);
	CHECK(savedWasSummoned);
}

TEST_CASE_METHOD(PersistentPetFixture, "with the PETS subsystem off, pet actions fail and touch no pet data", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);
	PersistentPetData dead = MakeStoredPet(BeastPetId, 2);
	dead.unitEntry = BoarUnitId;
	dead.isDead = true;
	player->SetStoredPets({ MakeStoredPet(ImpPetId), dead });
	const std::vector<PersistentPetData> before = player->GetStoredPets();
	const auto boar = SpawnCreature(*boarUnit);

	watcher.petsAvailable = false;
	const uint32 changesBefore = storedPetsChangedCount;

	CHECK(player->SummonStoredPet(ImpPetId) == pet_result::Disabled);
	CHECK(player->GrantPersistentPet(ScholarPetId) == pet_result::Disabled);
	CHECK(player->TameCreature(*boar) == pet_result::Disabled);
	CHECK(player->RevivePet(BeastPetId, 100) == pet_result::Disabled);

	// Through the spell effects, too: the cast goes through, the pet action does not.
	REQUIRE(CastAt(*player, CallPetSpellId, nullptr) == spell_cast_result::CastOkay);
	REQUIRE(CastAt(*player, TameSpellId, boar.get()) == spell_cast_result::CastOkay);

	RunFor(std::chrono::milliseconds(50));
	CHECK(PersistentPetOf(*player) == nullptr);
	CHECK(requests.empty());
	CHECK(boar->GetWorldInstance() != nullptr);
	CHECK(storedPetsChangedCount == changesBefore);
	CHECK(player->GetStoredPets() == before);

	REQUIRE(watcher.results.size() >= 6);
	CHECK(std::all_of(watcher.results.end() - 6, watcher.results.end(), [](const pet_result::Type result) { return result == pet_result::Disabled; }));

	// A pet that was summoned when it was stored waits until the subsystem is back.
	PersistentPetData summoned = MakeStoredPet(ImpPetId);
	summoned.wasSummoned = true;
	player->SetStoredPets({ summoned });
	RunFor(std::chrono::milliseconds(50));
	CHECK(PersistentPetOf(*player) == nullptr);
	CHECK(player->GetStoredPets().front().wasSummoned);

	watcher.petsAvailable = true;
	CHECK(player->SummonStoredPet(ImpPetId) == pet_result::Ok);
	CHECK(RunUntil([&] { return PersistentPetOf(*player) != nullptr; }));
}

TEST_CASE_METHOD(PersistentPetFixture, "stored pets that arrive after entering the world are summoned if they were", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);
	PersistentPetData stored = MakeStoredPet(ImpPetId);
	stored.wasSummoned = true;
	player->SetStoredPets({ stored });

	CHECK(RunUntil([&] { return PersistentPetOf(*player) != nullptr; }));
}

TEST_CASE_METHOD(PersistentPetFixture, "a persistent pet regenerates out of combat", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });
	GameCreatureS* pet = Summon(*player, ImpPetId);
	REQUIRE_FALSE(pet->IsInCombat());

	pet->Set<uint32>(object_fields::Health, 10);
	pet->SettleRegeneration(pet->GetRegenSettledAt() + 10000);
	CHECK(pet->GetHealth() > 10);
}

TEST_CASE_METHOD(PersistentPetFixture, "switching PETS off dismisses the live pets and switching it on brings the persistent pet back", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });
	GameCreatureS* pet = Summon(*player, ImpPetId);
	REQUIRE(pet);

	// A temporary pet, the way the SummonPet effect adds one.
	auto temporary = world->CreateTemporaryCreature(*rockUnit, Vector3(2.0f, 0.0f, 0.0f), 0.0f, 0.0f);
	temporary->SetGrantsKillRewards(false);
	temporary->SetSummonOwner(player);
	temporary->SetOwnerGuid(player->GetGuid());
	temporary->SetPetGroup(1);
	world->AddGameObject(*temporary);
	player->GetPets().AddTemporaryPet(*temporary, 1, 5, pet_limit_policy::ReplaceOldest);
	temporary->GetAI()->EnterPetMode(*player);
	const std::weak_ptr<GameObjectS> weakTemporary = temporary;
	temporary.reset();

	watcher.petsAvailable = false;
	player->OnPetsAvailabilityChanged(false);

	CHECK(PersistentPetOf(*player) == nullptr);
	CHECK(player->GetStoredPets().front().wasSummoned);
	CHECK(RunUntil([&] { return weakTemporary.expired() && CountCreaturesInWorld(ImpUnitId) == 0; }));

	// No experience while switched off.
	const uint32 xpChangedBefore = watcher.xpChanged;
	player->AddPetExperience(50);
	CHECK(watcher.xpChanged == xpChangedBefore);

	watcher.petsAvailable = true;
	player->OnPetsAvailabilityChanged(true);
	CHECK(RunUntil([&] { return PersistentPetOf(*player) != nullptr; }));
}

TEST_CASE_METHOD(PersistentPetFixture, "an own-level pet gains no experience while PETS is off", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);
	PersistentPetData stored = MakeStoredPet(BeastPetId, 1);
	stored.unitEntry = BoarUnitId;
	player->SetStoredPets({ stored });
	Summon(*player, BeastPetId);

	watcher.petsAvailable = false;
	const uint32 changesBefore = storedPetsChangedCount;
	player->AddPetExperience(150);
	CHECK(player->GetStoredPets().front().level == 1);
	CHECK(player->GetStoredPets().front().xp == 0);
	CHECK(storedPetsChangedCount == changesBefore);
}

TEST_CASE_METHOD(PersistentPetFixture, "before the realm's pet list arrived, grant, tame and summon are refused", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);
	const auto boar = SpawnCreature(*boarUnit);
	watcher.storedPetsLoaded = false;

	CHECK(player->CanGrantPersistentPet(ImpPetId) == pet_result::Disabled);
	CHECK(player->GrantPersistentPet(ImpPetId) == pet_result::Disabled);
	CHECK(player->CanTameCreature(*boar) == pet_result::Disabled);
	CHECK(player->TameCreature(*boar) == pet_result::Disabled);
	CHECK(player->SummonStoredPet(0) == pet_result::Disabled);
	CHECK(requests.empty());
	CHECK(boar->GetWorldInstance() != nullptr);
	REQUIRE_FALSE(watcher.results.empty());
	CHECK(watcher.results.back() == pet_result::Disabled);

	watcher.storedPetsLoaded = true;
	CHECK(player->CanGrantPersistentPet(ImpPetId) == pet_result::Ok);
}

TEST_CASE_METHOD(PersistentPetFixture, "a pet creation the realm refused tells the player", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);
	answerCreationRequests = false;
	REQUIRE(player->GrantPersistentPet(ImpPetId) == pet_result::Ok);
	REQUIRE(requests.size() == 1);

	PersistentPetData refused = requests.front();
	refused.dbId = 0;
	player->OnPetCreated(refused);

	CHECK(player->GetStoredPets().empty());
	REQUIRE_FALSE(watcher.results.empty());
	CHECK(watcher.results.back() == pet_result::NoFreeSlot);

	// The pending creation is gone: the pet can be granted again.
	CHECK(player->CanGrantPersistentPet(ImpPetId) == pet_result::Ok);
}

TEST_CASE_METHOD(PersistentPetFixture, "reviving checks like CanRevivePet and changes nothing on failure", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);
	PersistentPetData tamed = MakeStoredPet(BeastPetId, 2);
	tamed.unitEntry = BoarUnitId;
	tamed.isDead = true;
	tamed.classId = OtherClassId;
	player->SetStoredPets({ tamed });

	CHECK(player->CanRevivePet(BeastPetId) == pet_result::WrongClass);
	CHECK(player->RevivePet(BeastPetId, 100) == pet_result::WrongClass);
	CHECK(player->FindStoredPet(BeastPetId)->isDead);

	// The class may not summon persistent pets: the summon that follows a revive would fail.
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });
	PersistentPetData dead = MakeStoredPet(BeastPetId, 2);
	dead.unitEntry = BoarUnitId;
	dead.isDead = true;
	player->SetStoredPets({ dead });
	mainClass->set_max_summoned_persistent_pets(0);
	player->SetClass(*mainClass);
	CHECK(player->CanRevivePet(BeastPetId) == pet_result::TooManyPets);
	CHECK(player->RevivePet(BeastPetId, 100) == pet_result::TooManyPets);
	CHECK(player->FindStoredPet(BeastPetId)->isDead);
}

TEST_CASE_METHOD(PersistentPetFixture, "only a harmful cast at an enemy sends the pet in", "[pets][persistent]")
{
	const auto player = SpawnPlayer(4);
	player->SetStoredPets({ MakeStoredPet(ImpPetId) });
	GameCreatureS* pet = Summon(*player, ImpPetId);
	// Out of its aggro range, so only the owner's casts can start a fight.
	const auto rock = SpawnCreature(*rockUnit, Vector3(25.0f, 0.0f, 0.0f));
	REQUIRE(pet->GetVictim() == nullptr);

	// A cast that is no attack, like a tame, leaves the pet with its owner.
	constexpr uint32 InspectSpellId = 400;
	auto* inspect = project.spells.add(InspectSpellId);
	inspect->set_name("Inspect");
	inspect->add_attributes(0);
	inspect->add_attributes(0);
	auto* effect = inspect->add_effects();
	effect->set_index(0);
	effect->set_type(spell_effects::Dummy);
	// Aimed at the caster, so the rock is not hit and has no reason to fight back.
	effect->set_targeta(spell_effect_targets::Caster);

	REQUIRE(CastAt(*player, InspectSpellId, rock.get()) == spell_cast_result::CastOkay);
	RunFor(std::chrono::milliseconds(600));
	CHECK_FALSE(rock->IsInCombat());
	CHECK(pet->GetVictim() == nullptr);

	REQUIRE(CastAt(*player, BiteSpellId, rock.get()) == spell_cast_result::CastOkay);
	CHECK(RunUntil([&] { return pet->GetVictim() == rock.get(); }));
}
