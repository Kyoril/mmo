// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Shared fixture of the persistent pet tests: a world with a player template, two classes, pet
// entries for a summoned (Imp), a tamed (Beast) and a class level (Scholar) pet, and a watcher that
// records the pet notifications the player would send to its client.

#pragma once


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

namespace mmo::pet_tests
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

	inline bool HasSpell(const std::vector<proto::UnitSpellEntry>& spells, const uint32 spellId)
	{
		return std::any_of(spells.begin(), spells.end(), [spellId](const proto::UnitSpellEntry& entry) { return entry.spellid() == spellId; });
	}
}
