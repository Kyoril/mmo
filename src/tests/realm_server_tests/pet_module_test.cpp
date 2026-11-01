// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// The realm's PetModule: loading a character's stored pets, saving what the world node reports,
// creating new pets and deleting them with their character.

#include "catch.hpp"
#include "mock_databases.h"

#include "realm_server/modules/pet_module.h"

#include "game/pet_defines.h"

#include <algorithm>

using namespace mmo;

namespace
{
	constexpr uint64 CharacterGuid = 42;
	constexpr uint64 OtherCharacterGuid = 43;

	void SyncResult(std::function<void()> action)
	{
		action();
	}

	PersistentPetData MakePet(const uint64 dbId, const uint32 petEntry)
	{
		PersistentPetData pet;
		pet.dbId = dbId;
		pet.classId = 5;
		pet.petEntry = petEntry;
		pet.name = "Pet" + std::to_string(petEntry);
		pet.level = 3;
		pet.spells = { 100 + petEntry };
		return pet;
	}

	bool ContainsPet(const std::vector<PersistentPetData>& pets, const uint64 dbId)
	{
		return std::any_of(pets.begin(), pets.end(), [dbId](const PersistentPetData& pet) { return pet.dbId == dbId; });
	}

	/// A pet module whose database work runs inline against a mock, or is queued while
	/// deferWork is set (requests in flight) until RunQueuedWork.
	struct PetModuleFixture
	{
		MockPetDatabase database;
		AsyncPetDatabase asyncDatabase{
			[this](uint64 key, std::function<void(IPetDatabase&)> work)
			{
				keys.push_back(key);
				if (deferWork)
				{
					queuedWork.push_back(std::move(work));
					return;
				}
				work(database);
			},
			SyncResult };
		PetModule module{ asyncDatabase };
		std::vector<uint64> keys;
		bool deferWork = false;
		std::vector<std::function<void(IPetDatabase&)>> queuedWork;

		/// Runs the queued requests in order (one key, one FIFO queue), with their results.
		void RunQueuedWork()
		{
			auto work = std::move(queuedWork);
			queuedWork.clear();
			for (auto& item : work)
			{
				item(database);
			}
		}

		/// Loads the character's pets, as on entering the world, and hands them to a world node.
		void EnterWorld(const uint64 characterGuid = CharacterGuid)
		{
			bool loaded = false;
			module.LoadPets(characterGuid, [&loaded]() { loaded = true; });
			REQUIRE(loaded);
			REQUIRE(module.GetPetsForJoin(characterGuid));
		}
	};
}

TEST_CASE_METHOD(PetModuleFixture, "loading caches the character's pets", "[pets][pet_module]")
{
	database.petsByCharacter[CharacterGuid] = { MakePet(1, 3), MakePet(2, 4) };

	bool loaded = false;
	module.LoadPets(CharacterGuid, [&loaded]() { loaded = true; });
	CHECK(loaded);

	// Keyed by the character, like the character's other data.
	REQUIRE(keys.size() == 1);
	CHECK(keys.front() == CharacterGuid);

	const auto pets = module.GetPetsForJoin(CharacterGuid);
	REQUIRE(pets);
	REQUIRE(pets->size() == 2);
	CHECK((*pets)[0] == MakePet(1, 3));
	CHECK((*pets)[1] == MakePet(2, 4));

	// A character without pets has an empty list, not none.
	module.LoadPets(OtherCharacterGuid, []() {});
	const auto none = module.GetPetsForJoin(OtherCharacterGuid);
	REQUIRE(none);
	CHECK(none->empty());
}

TEST_CASE_METHOD(PetModuleFixture, "a failed load still continues the login, but hands nothing to the world", "[pets][pet_module]")
{
	database.loadFails = true;

	bool loaded = false;
	module.LoadPets(CharacterGuid, [&loaded]() { loaded = true; });
	CHECK(loaded);
	CHECK_FALSE(module.GetPetsForJoin(CharacterGuid));
}

TEST_CASE_METHOD(PetModuleFixture, "saving writes only the pets that changed", "[pets][pet_module]")
{
	database.petsByCharacter[CharacterGuid] = { MakePet(1, 3), MakePet(2, 4) };
	EnterWorld();

	PersistentPetData leveled = MakePet(2, 4);
	leveled.level = 4;
	leveled.xp = 10;
	module.SavePets(CharacterGuid, { MakePet(1, 3), leveled });

	REQUIRE(database.saves.size() == 1);
	CHECK(database.saves[0].characterId == CharacterGuid);
	REQUIRE(database.saves[0].pets.size() == 1);
	CHECK(database.saves[0].pets[0] == leveled);
	CHECK(database.saves[0].removedPetIds.empty());
	CHECK(keys.back() == CharacterGuid);

	// The same state again is not written a second time.
	module.SavePets(CharacterGuid, { MakePet(1, 3), leveled });
	CHECK(database.saves.size() == 1);

	// The world node gets the saved state on the next join (map change to another node).
	const auto pets = module.GetPetsForJoin(CharacterGuid);
	REQUIRE(pets);
	REQUIRE(pets->size() == 2);
	CHECK((*pets)[1] == leveled);
}

TEST_CASE_METHOD(PetModuleFixture, "a pet the world node no longer reports is deleted", "[pets][pet_module]")
{
	database.petsByCharacter[CharacterGuid] = { MakePet(1, 3), MakePet(2, 4) };
	EnterWorld();

	module.SavePets(CharacterGuid, { MakePet(1, 3) });

	REQUIRE(database.saves.size() == 1);
	CHECK(database.saves[0].pets.empty());
	CHECK(database.saves[0].removedPetIds == std::vector<uint64>{ 2 });

	const auto pets = module.GetPetsForJoin(CharacterGuid);
	REQUIRE(pets);
	REQUIRE(pets->size() == 1);
	CHECK(pets->front().dbId == 1);
}

TEST_CASE_METHOD(PetModuleFixture, "creating a pet assigns an id and echoes the request", "[pets][pet_module]")
{
	EnterWorld();
	database.nextPetId = 77;

	PersistentPetData request = MakePet(0, 3);
	request.unitEntry = 12;
	request.wasSummoned = true;

	std::optional<PersistentPetData> answer;
	module.CreatePet(CharacterGuid, request, [&answer](const PersistentPetData& created) { answer = created; });

	REQUIRE(database.creations.size() == 1);
	CHECK(database.creations[0].first == CharacterGuid);
	CHECK(keys.back() == CharacterGuid);

	REQUIRE(answer);
	CHECK(answer->dbId == 77);
	PersistentPetData expected = request;
	expected.dbId = 77;
	CHECK(*answer == expected);

	const auto pets = module.GetPetsForJoin(CharacterGuid);
	REQUIRE(pets);
	REQUIRE(pets->size() == 1);
	CHECK(pets->front().dbId == 77);
}

TEST_CASE_METHOD(PetModuleFixture, "a pet created after the world node got its list is not deleted by a save that predates it", "[pets][pet_module]")
{
	database.petsByCharacter[CharacterGuid] = { MakePet(1, 3) };
	EnterWorld();
	database.nextPetId = 5;

	module.CreatePet(CharacterGuid, MakePet(0, 4), [](const PersistentPetData&) {});

	// The world node saved before it heard of the new pet.
	module.SavePets(CharacterGuid, { MakePet(1, 3) });
	for (const auto& save : database.saves)
	{
		CHECK(save.removedPetIds.empty());
	}

	const std::vector<PersistentPetData>* pets = module.FindPets(CharacterGuid);
	REQUIRE(pets);
	CHECK(ContainsPet(*pets, 5));

	// Once the world node reported it, leaving it out removes it (a GM removed it, say).
	PersistentPetData created = MakePet(5, 4);
	module.SavePets(CharacterGuid, { MakePet(1, 3), created });
	module.SavePets(CharacterGuid, { MakePet(1, 3) });
	REQUIRE_FALSE(database.saves.empty());
	CHECK(database.saves.back().removedPetIds == std::vector<uint64>{ 5 });
}

TEST_CASE_METHOD(PetModuleFixture, "a pet handed to the world node with the join can be removed by the next save", "[pets][pet_module]")
{
	database.petsByCharacter[CharacterGuid] = { MakePet(1, 3) };
	EnterWorld();
	database.nextPetId = 5;
	module.CreatePet(CharacterGuid, MakePet(0, 4), [](const PersistentPetData&) {});

	// The character changed its world node: the new node got the pet with its join.
	REQUIRE(module.GetPetsForJoin(CharacterGuid));
	module.SavePets(CharacterGuid, { MakePet(1, 3) });

	REQUIRE_FALSE(database.saves.empty());
	CHECK(database.saves.back().removedPetIds == std::vector<uint64>{ 5 });
}

TEST_CASE_METHOD(PetModuleFixture, "a failed creation answers without an id", "[pets][pet_module]")
{
	EnterWorld();
	database.nextPetId = 0;

	std::optional<PersistentPetData> answer;
	module.CreatePet(CharacterGuid, MakePet(0, 3), [&answer](const PersistentPetData& created) { answer = created; });

	REQUIRE(answer);
	CHECK(answer->dbId == 0);
	CHECK(answer->petEntry == 3);
	const auto pets = module.GetPetsForJoin(CharacterGuid);
	REQUIRE(pets);
	CHECK(pets->empty());
}

TEST_CASE_METHOD(PetModuleFixture, "creation is refused when every stored pet slot is taken", "[pets][pet_module]")
{
	std::vector<PersistentPetData> full;
	for (uint32 i = 1; i <= MaxStoredPets; ++i)
	{
		full.push_back(MakePet(i, i));
	}
	database.petsByCharacter[CharacterGuid] = full;
	EnterWorld();

	std::optional<PersistentPetData> answer;
	module.CreatePet(CharacterGuid, MakePet(0, 99), [&answer](const PersistentPetData& created) { answer = created; });

	CHECK(database.creations.empty());
	REQUIRE(answer);
	CHECK(answer->dbId == 0);
}

TEST_CASE_METHOD(PetModuleFixture, "deleting a character deletes its pets", "[pets][pet_module]")
{
	database.petsByCharacter[CharacterGuid] = { MakePet(1, 3) };
	EnterWorld();

	module.DeletePets(CharacterGuid);

	CHECK(database.deletedCharacters == std::vector<uint64>{ CharacterGuid });
	CHECK(keys.back() == CharacterGuid);
	CHECK_FALSE(module.GetPetsForJoin(CharacterGuid));
}

TEST_CASE_METHOD(PetModuleFixture, "a save after the character left still writes, but deletes nothing", "[pets][pet_module]")
{
	database.petsByCharacter[CharacterGuid] = { MakePet(1, 3), MakePet(2, 4) };
	EnterWorld();

	// A disconnect: the realm forgets the character before the world node's last save arrives.
	module.ForgetPets(CharacterGuid);

	PersistentPetData dismissed = MakePet(1, 3);
	dismissed.health = 50;
	module.SavePets(CharacterGuid, { dismissed });

	REQUIRE(database.saves.size() == 1);
	REQUIRE(database.saves[0].pets.size() == 1);
	CHECK(database.saves[0].pets[0] == dismissed);
	CHECK(database.saves[0].removedPetIds.empty());
	CHECK_FALSE(module.GetPetsForJoin(CharacterGuid));
}

TEST_CASE_METHOD(PetModuleFixture, "a failed save is written again by the next identical save", "[pets][pet_module]")
{
	database.petsByCharacter[CharacterGuid] = { MakePet(1, 3) };
	EnterWorld();

	PersistentPetData leveled = MakePet(1, 3);
	leveled.level = 4;
	database.failingSaves = { 0 };
	module.SavePets(CharacterGuid, { leveled });
	REQUIRE(database.saves.size() == 1);

	// The same state again (the logout save, say) is not "unchanged": the database never got it.
	module.SavePets(CharacterGuid, { leveled });
	REQUIRE(database.saves.size() == 2);
	REQUIRE(database.saves[1].pets.size() == 1);
	CHECK(database.saves[1].pets[0] == leveled);

	// Once written, it is unchanged.
	module.SavePets(CharacterGuid, { leveled });
	CHECK(database.saves.size() == 2);
}

TEST_CASE_METHOD(PetModuleFixture, "a failed removal is retried by the next save", "[pets][pet_module]")
{
	database.petsByCharacter[CharacterGuid] = { MakePet(1, 3), MakePet(2, 4) };
	EnterWorld();

	database.failingSaves = { 0 };
	module.SavePets(CharacterGuid, { MakePet(1, 3) });
	REQUIRE(database.saves.size() == 1);
	CHECK(database.saves[0].removedPetIds == std::vector<uint64>{ 2 });

	module.SavePets(CharacterGuid, { MakePet(1, 3) });
	REQUIRE(database.saves.size() == 2);
	CHECK(database.saves[1].pets.empty());
	CHECK(database.saves[1].removedPetIds == std::vector<uint64>{ 2 });

	// The world node's list is what it hands over on the next join, failed write or not.
	const std::vector<PersistentPetData>* pets = module.FindPets(CharacterGuid);
	REQUIRE(pets);
	CHECK_FALSE(ContainsPet(*pets, 2));

	module.SavePets(CharacterGuid, { MakePet(1, 3) });
	CHECK(database.saves.size() == 2);
}

TEST_CASE_METHOD(PetModuleFixture, "two saves in flight never make the cache claim more than the database has", "[pets][pet_module]")
{
	database.petsByCharacter[CharacterGuid] = { MakePet(1, 3) };
	EnterWorld();

	PersistentPetData level4 = MakePet(1, 3);
	level4.level = 4;
	PersistentPetData level5 = MakePet(1, 3);
	level5.level = 5;

	SECTION("the older save succeeds, the newer one fails")
	{
		deferWork = true;
		module.SavePets(CharacterGuid, { level4 });
		module.SavePets(CharacterGuid, { level5 });
		database.failingSaves = { 1 };
		deferWork = false;
		RunQueuedWork();

		// The database has level 4, so level 5 must be written again.
		module.SavePets(CharacterGuid, { level5 });
		REQUIRE(database.saves.size() == 3);
		REQUIRE(database.saves[2].pets.size() == 1);
		CHECK(database.saves[2].pets[0] == level5);
	}

	SECTION("the older save fails, the newer one succeeds")
	{
		deferWork = true;
		module.SavePets(CharacterGuid, { level4 });
		module.SavePets(CharacterGuid, { level5 });
		database.failingSaves = { 0 };
		deferWork = false;
		RunQueuedWork();

		// The newer save wrote level 5 (it was compared with the confirmed level 3).
		REQUIRE(database.saves.size() == 2);
		REQUIRE(database.saves[1].pets.size() == 1);
		CHECK(database.saves[1].pets[0] == level5);

		module.SavePets(CharacterGuid, { level5 });
		CHECK(database.saves.size() == 2);
	}

	SECTION("both succeed")
	{
		deferWork = true;
		module.SavePets(CharacterGuid, { level4 });
		module.SavePets(CharacterGuid, { level5 });
		deferWork = false;
		RunQueuedWork();

		module.SavePets(CharacterGuid, { level5 });
		CHECK(database.saves.size() == 2);
	}
}

TEST_CASE_METHOD(PetModuleFixture, "a save confirmed after the character logged in again does not touch the new session", "[pets][pet_module]")
{
	database.petsByCharacter[CharacterGuid] = { MakePet(1, 3) };
	EnterWorld();

	PersistentPetData leveled = MakePet(1, 3);
	leveled.level = 4;
	deferWork = true;
	module.SavePets(CharacterGuid, { leveled });

	// Logout and login again before the save finished: the new load comes after it (same key).
	module.ForgetPets(CharacterGuid);
	bool loaded = false;
	module.LoadPets(CharacterGuid, [&loaded]() { loaded = true; });
	database.failingSaves = { 0 };
	deferWork = false;
	RunQueuedWork();
	REQUIRE(loaded);

	// The new session loaded level 3 (the failed save never reached the database), and the stale
	// failure changed nothing about that: level 4 is written when reported.
	module.SavePets(CharacterGuid, { leveled });
	REQUIRE(database.saves.size() == 2);
	CHECK(database.saves[1].pets[0] == leveled);
}
