// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Persistent pets: a character's stored pets are summoned into the world as pet creatures with a
// stable guid, are dismissed (stored with their health and power) on dismiss, death of the owner,
// logout and class switch, come back with their owner, level with their owner or on their own
// experience, and are created by granting or taming. New stored pets are created by the realm; the
// tests stand in for it by answering petCreationRequested with OnPetCreated.

#include "catch.hpp"

#include "game_server/ai/creature_ai.h"
#include "game_server/condition_mgr.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/pets/persistent_pet.h"
#include "game_server/pets/pet_manager.h"
#include "game_server/trigger_handler.h"
#include "game_server/world/universe.h"
#include "game_server/world/world_instance.h"
#include "game_server/world/world_instance_manager.h"
#include "game/guid_utils.h"
#include "game/pet_defines.h"
#include "game/spell.h"
#include "game/spell_target_map.h"
#include "base/id_generator.h"
#include "base/timer_queue.h"
#include "binary_io/vector_sink.h"
#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/classes.pb.h"
#include "asio/io_service.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <vector>

using namespace mmo;

namespace
{
	constexpr uint32 PlayerTemplate = 1;
	constexpr uint32 MonsterTemplate = 2;

	constexpr uint32 MainClassId = 1;
	constexpr uint32 OtherClassId = 2;

	// Creature entries
	constexpr uint32 ImpUnitId = 10;
	constexpr uint32 BoarUnitId = 11;
	constexpr uint32 RockUnitId = 12;

	// Pet entries
	constexpr uint32 ImpPetId = 1;
	constexpr uint32 BeastPetId = 2;
	constexpr uint32 ScholarPetId = 3;

	// Spells
	constexpr uint32 BiteSpellId = 200;
	constexpr uint32 HowlSpellId = 201;
	constexpr uint32 ClawSpellId = 202;
	constexpr uint32 TameSpellId = 300;
	constexpr uint32 CallPetSpellId = 301;

	constexpr uint32 HowlLevel = 3;

	struct NullTriggerHandler final : ITriggerHandler
	{
		void ExecuteTrigger(const proto::TriggerEntry&, TriggerContext, uint32, bool) override {}
	};

	/// Remembers the pet notifications the player would send to its client.
	struct PetWatcher final : NetPlayerWatcher
	{
		void OnQuestKillCredit(const proto::QuestEntry&, uint64, uint32, uint32, uint32) override {}
		void OnQuestItemCredit(const proto::QuestEntry&, uint32, uint32, uint32) override {}
		void OnQuestObjectRequirementMet(uint32) override {}
		void OnQuestDataChanged(uint32, const QuestStatusData&) override {}
		void OnQuestCompleted(uint64, uint32, uint32, uint32) override {}
		void OnItemAdded(uint16, uint16, bool, bool) override {}
		void OnObjectLoot() override {}

		void OnPetSpellsChanged() override { ++spellsChanged; }
		void OnPetXpChanged(const uint32 xp, const uint32 nextLevelXp) override { lastXp = xp; lastNextLevelXp = nextLevelXp; ++xpChanged; }
		void OnPetLearnedSpell(const uint32 spellId) override { learned.push_back(spellId); }
		void OnPetResult(const pet_result::Type result) override { results.push_back(result); }
		bool ArePetsAvailable() const override { return petsAvailable; }

		/// The PETS subsystem switch the world node would report.
		bool petsAvailable = true;
		uint32 spellsChanged = 0;
		uint32 xpChanged = 0;
		uint32 lastXp = 0;
		uint32 lastNextLevelXp = 0;
		std::vector<uint32> learned;
		std::vector<pet_result::Type> results;
	};

	struct PersistentPetFixture
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;
		Universe universe{ io, timers };
		IdGenerator<uint64> objectIds{ 1 };
		NullTriggerHandler triggerHandler;

		// Declared before the world instance, so it outlives the players registered in it.
		PetWatcher watcher;

		std::unique_ptr<ConditionMgr> conditionMgr;
		std::unique_ptr<WorldInstanceManager> manager;
		WorldInstance* world = nullptr;

		proto::ClassEntry* mainClass = nullptr;
		proto::ClassEntry* otherClass = nullptr;
		proto::RaceEntry* raceEntry = nullptr;
		proto::UnitEntry* impUnit = nullptr;
		proto::UnitEntry* boarUnit = nullptr;
		proto::UnitEntry* rockUnit = nullptr;
		proto::PetEntry* impPet = nullptr;
		proto::PetEntry* beastPet = nullptr;
		proto::PetEntry* scholarPet = nullptr;
		uint64 nextPlayerGuid = 0x4000;
		uint64 nextPetDbId = 1;

		PersistentPetFixture()
		{
			auto* playerFaction = project.factionTemplates.add(PlayerTemplate);
			playerFaction->set_name("Players");
			playerFaction->set_flags(0);
			playerFaction->set_faction(1);
			playerFaction->add_enemies(2);

			auto* monsterFaction = project.factionTemplates.add(MonsterTemplate);
			monsterFaction->set_name("Monsters");
			monsterFaction->set_flags(0);
			monsterFaction->set_faction(2);
			monsterFaction->add_enemies(1);

			mainClass = AddClass(MainClassId);
			otherClass = AddClass(OtherClassId);

			raceEntry = project.races.add(1);
			raceEntry->set_name("Test Race");

			impUnit = AddUnitEntry(ImpUnitId, "Imp", 1);
			boarUnit = AddUnitEntry(BoarUnitId, "Forest Boar", 2);
			rockUnit = AddUnitEntry(RockUnitId, "Rock Elemental", 2);
			boarUnit->set_tame_pet_entry(BeastPetId);

			AddSpell(BiteSpellId, "Bite");
			AddSpell(HowlSpellId, "Howl");
			AddSpell(ClawSpellId, "Claw");

			// A warlock-style pet: fixed name, follows its master's character level.
			impPet = project.pets.add(ImpPetId);
			impPet->set_name("Grimlock");
			impPet->set_unit_entry(ImpUnitId);
			impPet->set_name_mode(pet_name_mode::Fixed);
			impPet->set_level_mode(pet_level_mode::MasterLevel);
			impPet->set_level_source(pet_level_source::CharacterLevel);
			impPet->add_default_spells(BiteSpellId);
			impPet->set_health_multiplier(2.0f);

			// A hunter-style tamed pet: levels on its own, capped by the master's character level.
			beastPet = project.pets.add(BeastPetId);
			beastPet->set_name("Beast");
			beastPet->set_unit_entry(BoarUnitId);
			beastPet->set_name_mode(pet_name_mode::Fixed);
			beastPet->set_level_mode(pet_level_mode::OwnLevel);
			beastPet->set_level_source(pet_level_source::CharacterLevel);
			auto* howl = beastPet->add_level_spells();
			howl->set_spell(HowlSpellId);
			howl->set_level(HowlLevel);

			// A pet following its master's class level, named from a list.
			scholarPet = project.pets.add(ScholarPetId);
			scholarPet->set_name("Scholar");
			scholarPet->set_unit_entry(ImpUnitId);
			scholarPet->set_name_mode(pet_name_mode::RandomFromList);
			scholarPet->add_random_names("Quill");
			scholarPet->add_random_names("Inkwell");
			scholarPet->set_level_mode(pet_level_mode::MasterLevel);
			scholarPet->set_level_source(pet_level_source::ClassLevel);

			AddTameSpell();
			AddCallPetSpell();

			// No map files exist for this directory: no nav mesh, no collision, LOS is unblocked.
			auto* map = project.maps.add(0);
			map->set_name("Persistent Pet Test");
			map->set_directory("Worlds/__persistent_pet_test__/__persistent_pet_test__");

			conditionMgr = std::make_unique<ConditionMgr>(project.conditions);
			manager = std::make_unique<WorldInstanceManager>(io, universe, project, objectIds, triggerHandler, *conditionMgr);
			world = &manager->CreateInstance(0);
		}

		proto::ClassEntry* AddClass(const uint32 id)
		{
			auto* entry = project.classes.add(id);
			entry->set_name(id == MainClassId ? "Main" : "Other");
			entry->set_powertype(proto::ClassEntry_PowerType_MANA);
			for (uint32 i = 0; i < 10; ++i)
			{
				auto* levelValues = entry->add_levelbasevalues();
				levelValues->set_health(100);
				levelValues->set_mana(100);
				for (int a = 0; a < 5; ++a)
				{
					levelValues->add_attributes(10);
				}
				entry->add_xptonextlevel(100);

				// A class level curve: 100 class xp per class level, 10 class levels.
				auto* classLevel = entry->add_classlevels();
				classLevel->set_xptonextlevel(100);
			}
			return entry;
		}

		proto::UnitEntry* AddUnitEntry(const uint32 id, const char* name, const uint32 level)
		{
			auto* entry = project.units.add(id);
			entry->set_name(name);
			entry->set_minlevel(level);
			entry->set_maxlevel(level);
			entry->set_factiontemplate(MonsterTemplate);
			entry->set_malemodel(0);
			entry->set_femalemodel(0);
			entry->set_type(0);
			entry->set_family(0);
			entry->set_minlevelhealth(100);
			entry->set_maxlevelhealth(100);
			entry->set_minlevelmana(50);
			entry->set_maxlevelmana(50);
			return entry;
		}

		void AddSpell(const uint32 id, const char* name)
		{
			auto* spell = project.spells.add(id);
			spell->set_name(name);
			spell->add_attributes(0);
			spell->add_attributes(0);
			auto* effect = spell->add_effects();
			effect->set_index(0);
			effect->set_type(spell_effects::Dummy);
			effect->set_targeta(spell_effect_targets::TargetEnemy);
		}

		void AddTameSpell()
		{
			auto* spell = project.spells.add(TameSpellId);
			spell->set_name("Tame Beast");
			spell->add_attributes(0);
			spell->add_attributes(0);
			auto* effect = spell->add_effects();
			effect->set_index(0);
			effect->set_type(spell_effects::TameCreature);
			effect->set_targeta(spell_effect_targets::TargetAny);
		}

		void AddCallPetSpell()
		{
			auto* spell = project.spells.add(CallPetSpellId);
			spell->set_name("Call Pet");
			spell->add_attributes(0);
			spell->add_attributes(0);
			auto* effect = spell->add_effects();
			effect->set_index(0);
			effect->set_type(spell_effects::SummonPersistentPet);
			effect->set_targeta(spell_effect_targets::Caster);
			effect->set_miscvaluea(0);
		}

		/// Adds a player of the main class (also knowing the other class) to the world.
		std::shared_ptr<GamePlayerS> SpawnPlayer(const uint32 level = 4, const bool addToWorld = true)
		{
			auto player = std::make_shared<GamePlayerS>(project, timers);
			player->Initialize();
			player->Set<uint64>(object_fields::Guid, nextPlayerGuid++, false);

			CharacterClassData mainData;
			mainData.classId = MainClassId;
			mainData.classLevel = 2;
			CharacterClassData otherData;
			otherData.classId = OtherClassId;
			otherData.classLevel = 1;
			player->SetKnownClasses({ mainData, otherData });

			player->SetClass(*mainClass);
			player->SetRace(*raceEntry);
			player->SetLevel(level);
			player->Set<uint32>(object_fields::FactionTemplate, PlayerTemplate);
			player->Relocate(Vector3(0.0f, 0.0f, 0.0f), Radian(0.0f));
			player->SetPlayerWatcher(&watcher);

			// The realm's part: every requested pet is created with a fresh database id.
			std::weak_ptr<GamePlayerS> weakPlayer = player;
			connections.emplace_back(player->petCreationRequested.connect([this, weakPlayer](const PersistentPetData& request)
				{
					requests.push_back(request);
					if (!answerCreationRequests)
					{
						return;
					}

					if (const auto strong = weakPlayer.lock())
					{
						PersistentPetData created = request;
						created.dbId = nextPetDbId++;
						strong->OnPetCreated(created);
					}
				}));
			connections.emplace_back(player->storedPetsChanged.connect([this]()
				{
					++storedPetsChangedCount;
				}));

			if (addToWorld)
			{
				world->AddGameObject(*player);
			}
			return player;
		}

		/// A stored pet as the realm would hand it to the world node.
		PersistentPetData MakeStoredPet(const uint32 petEntry, const uint32 level = 1, const uint32 classId = MainClassId)
		{
			PersistentPetData data;
			data.dbId = nextPetDbId++;
			data.classId = classId;
			data.petEntry = petEntry;
			data.unitEntry = 0;
			data.name = "Fido";
			data.level = level;
			return data;
		}

		std::shared_ptr<GameCreatureS> SpawnCreature(const proto::UnitEntry& entry, const Vector3& position = Vector3(3.0f, 0.0f, 0.0f))
		{
			auto creature = world->CreateCreature(entry, position, 0.0f, 0.0f);
			world->AddGameObject(*creature);
			creature->GetAI()->Idle();
			return creature;
		}

		GameCreatureS* PersistentPetOf(const GameUnitS& owner) const
		{
			return owner.FindPets() ? owner.FindPets()->GetPersistentPet() : nullptr;
		}

		uint32 CountCreaturesInWorld(const uint32 entry) const
		{
			uint32 count = 0;
			world->ForEachObject([&count, entry](GameObjectS& object)
				{
					if (object.IsUnit() && !object.IsPlayer() && object.Get<uint32>(object_fields::Entry) == entry)
					{
						++count;
					}
					return true;
				});
			return count;
		}

		/// Runs the io service until the condition holds or the timeout passes.
		/// @returns Whether the condition holds.
		template<typename Condition>
		bool RunUntil(Condition condition, const std::chrono::milliseconds timeout = std::chrono::milliseconds(1000))
		{
			const auto deadline = std::chrono::steady_clock::now() + timeout;
			while (!condition() && std::chrono::steady_clock::now() < deadline)
			{
				io.restart();
				io.run_for(std::chrono::milliseconds(10));
			}

			return condition();
		}

		/// Runs the io service in real time for the given duration.
		void RunFor(const std::chrono::milliseconds duration)
		{
			const auto deadline = std::chrono::steady_clock::now() + duration;
			while (std::chrono::steady_clock::now() < deadline)
			{
				io.restart();
				io.run_for(std::chrono::milliseconds(10));
			}
		}

		/// Summons a stored pet and waits until it is in the world.
		GameCreatureS* Summon(GamePlayerS& player, const uint32 petEntry)
		{
			REQUIRE(player.SummonStoredPet(petEntry) == pet_result::Ok);
			REQUIRE(RunUntil([&] { return PersistentPetOf(player) != nullptr; }));
			return PersistentPetOf(player);
		}

		/// Casts a spell at a unit target, ignoring cooldowns.
		SpellCastResult CastAt(GamePlayerS& caster, const uint32 spellId, const GameUnitS* target)
		{
			SpellTargetMap targetMap;
			if (target)
			{
				targetMap.SetTargetMap(spell_cast_target_flags::Unit);
				targetMap.SetUnitTarget(target->GetGuid());
			}
			else
			{
				targetMap.SetTargetMap(spell_cast_target_flags::Self);
			}

			caster.ResetCooldowns();
			return caster.CastSpell(targetMap, *project.spells.getById(spellId), 0, false, 0, true);
		}

		~PersistentPetFixture()
		{
			io.stop();
		}

		std::vector<PersistentPetData> requests;
		bool answerCreationRequests = true;
		uint32 storedPetsChangedCount = 0;
		std::vector<scoped_connection> connections;
	};

	bool HasSpell(const std::vector<proto::UnitSpellEntry>& spells, const uint32 spellId)
	{
		return std::any_of(spells.begin(), spells.end(), [spellId](const proto::UnitSpellEntry& entry) { return entry.spellid() == spellId; });
	}
}

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
