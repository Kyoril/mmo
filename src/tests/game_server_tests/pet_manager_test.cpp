// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Temporary pets: creatures a unit summons with the SummonPet spell effect. Every owner keeps its
// pets in a PetManager, counted per group against the effect's cap, and the pets go away with the
// owner: when it dies, despawns (a player logging out leaves the world the same way) or, for a
// creature, resets after a fight.

#include "catch.hpp"

#include "game_server/character_data.h"
#include "game_server/condition_mgr.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/ai/creature_ai.h"
#include "game_server/pets/pet_manager.h"
#include "game_server/trigger_handler.h"
#include "game_server/world/universe.h"
#include "game_server/world/world_instance.h"
#include "game_server/world/world_instance_manager.h"
#include "game/pet_defines.h"
#include "game/spell.h"
#include "game/spell_target_map.h"
#include "base/id_generator.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/classes.pb.h"
#include "asio/io_service.hpp"

#include <chrono>
#include <memory>
#include <vector>

using namespace mmo;

namespace
{
	constexpr uint32 OwnerTemplate = 1;
	constexpr uint32 PetTemplate = 2;
	constexpr uint32 PetEntryId = 10;
	constexpr uint32 OwnerEntryId = 11;
	constexpr uint32 SummonSpellId = 100;
	constexpr uint32 Group = 1;
	constexpr uint32 OtherGroup = 2;

	struct NullTriggerHandler final : ITriggerHandler
	{
		void ExecuteTrigger(const proto::TriggerEntry&, TriggerContext, uint32, bool) override {}
	};

	struct PetManagerFixture
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;
		Universe universe{ io, timers };
		IdGenerator<uint64> objectIds{ 1 };
		NullTriggerHandler triggerHandler;

		std::unique_ptr<ConditionMgr> conditionMgr;
		std::unique_ptr<WorldInstanceManager> manager;
		WorldInstance* world = nullptr;

		proto::UnitEntry* petEntry = nullptr;
		proto::UnitEntry* ownerEntry = nullptr;
		proto::ClassEntry* classEntry = nullptr;
		proto::RaceEntry* raceEntry = nullptr;
		proto::SpellEntry* summonSpell = nullptr;
		uint64 nextPlayerGuid = 0x4000;

		PetManagerFixture()
		{
			auto* ownerFaction = project.factionTemplates.add(OwnerTemplate);
			ownerFaction->set_name("Owners");
			ownerFaction->set_flags(0);
			ownerFaction->set_faction(1);

			auto* petFaction = project.factionTemplates.add(PetTemplate);
			petFaction->set_name("Pets");
			petFaction->set_flags(0);
			petFaction->set_faction(2);

			petEntry = AddUnitEntry(PetEntryId, "Skeleton", PetTemplate, 2, 2);
			ownerEntry = AddUnitEntry(OwnerEntryId, "Necromancer", OwnerTemplate, 7, 7);

			classEntry = project.classes.add(1);
			classEntry->set_powertype(proto::ClassEntry_PowerType_MANA);
			for (uint32 i = 0; i < 12; ++i)
			{
				auto* levelValues = classEntry->add_levelbasevalues();
				levelValues->set_health(100);
				levelValues->set_mana(100);
				for (int a = 0; a < 5; ++a)
				{
					levelValues->add_attributes(10);
				}
				classEntry->add_xptonextlevel(100000);
			}

			raceEntry = project.races.add(1);
			raceEntry->set_name("Test Race");

			summonSpell = project.spells.add(SummonSpellId);
			summonSpell->set_name("Raise Skeleton");
			summonSpell->add_attributes(0);
			summonSpell->add_attributes(0);
			auto* effect = summonSpell->add_effects();
			effect->set_index(0);
			effect->set_type(spell_effects::SummonPet);
			effect->set_targeta(spell_effect_targets::Caster);
			effect->set_summonunit(PetEntryId);
			effect->set_basepoints(1);

			// No map files exist for this directory: no nav mesh, no collision.
			auto* map = project.maps.add(0);
			map->set_name("Pet Manager Test");
			map->set_directory("Worlds/__pet_manager_test__/__pet_manager_test__");

			conditionMgr = std::make_unique<ConditionMgr>(project.conditions);
			manager = std::make_unique<WorldInstanceManager>(io, universe, project, objectIds, triggerHandler, *conditionMgr);
			world = &manager->CreateInstance(0);
		}

		proto::UnitEntry* AddUnitEntry(const uint32 id, const char* name, const uint32 factionTemplate, const uint32 minLevel, const uint32 maxLevel)
		{
			auto* entry = project.units.add(id);
			entry->set_name(name);
			entry->set_minlevel(minLevel);
			entry->set_maxlevel(maxLevel);
			entry->set_factiontemplate(factionTemplate);
			entry->set_malemodel(0);
			entry->set_femalemodel(0);
			entry->set_type(0);
			entry->set_family(0);
			entry->set_minlevelhealth(100);
			entry->set_maxlevelhealth(100);
			return entry;
		}

		std::shared_ptr<GamePlayerS> SpawnPlayer(const uint32 level = 1)
		{
			auto player = std::make_shared<GamePlayerS>(project, timers);
			player->Initialize();
			player->Set<uint64>(object_fields::Guid, nextPlayerGuid++, false);
			player->SetClass(*classEntry);
			player->SetRace(*raceEntry);
			player->SetLevel(level);
			player->Set<uint32>(object_fields::FactionTemplate, OwnerTemplate);
			player->Relocate(Vector3(0.0f, 0.0f, 0.0f), Radian(0.0f));
			world->AddGameObject(*player);
			return player;
		}

		std::shared_ptr<GameCreatureS> SpawnOwner()
		{
			auto creature = world->CreateCreature(*ownerEntry, Vector3(0.0f, 0.0f, 0.0f), 0.0f, 0.0f);
			world->AddGameObject(*creature);
			return creature;
		}

		/// Spawns a pet creature the way the SummonPet effect does, without registering it anywhere.
		std::shared_ptr<GameCreatureS> SpawnPetCreature()
		{
			auto creature = world->CreateTemporaryCreature(*petEntry, Vector3(2.0f, 0.0f, 0.0f), 0.0f, 0.0f);
			world->AddGameObject(*creature);
			return creature;
		}

		/// Number of creatures of the pet entry currently in the world.
		uint32 CountPetCreaturesInWorld() const
		{
			uint32 count = 0;
			world->ForEachObject([&count](GameObjectS& object)
				{
					if (object.IsUnit() && !object.IsPlayer() && object.Get<uint32>(object_fields::Entry) == PetEntryId)
					{
						++count;
					}
					return true;
				});
			return count;
		}

		/// Collects the creatures of the pet entry currently in the world.
		std::vector<GameCreatureS*> PetCreaturesInWorld() const
		{
			std::vector<GameCreatureS*> result;
			world->ForEachObject([&result](GameObjectS& object)
				{
					if (object.IsUnit() && !object.IsPlayer() && object.Get<uint32>(object_fields::Entry) == PetEntryId)
					{
						result.push_back(static_cast<GameCreatureS*>(&object));
					}
					return true;
				});
			return result;
		}

		SpellCastResult CastSummon(GameUnitS& caster)
		{
			SpellTargetMap targetMap;
			targetMap.SetTargetMap(spell_cast_target_flags::Self);

			// Tests cast the summon repeatedly: the global cooldown must not get in the way.
			caster.ResetCooldowns();
			return caster.CastSpell(targetMap, *summonSpell, 0, false, 0, true);
		}

		/// Runs the io service (posted work and timers) in real time for the given duration.
		void RunFor(const std::chrono::milliseconds duration)
		{
			const auto deadline = std::chrono::steady_clock::now() + duration;
			while (std::chrono::steady_clock::now() < deadline)
			{
				io.restart();
				io.run_for(std::chrono::milliseconds(10));
			}
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

		~PetManagerFixture()
		{
			io.stop();
		}
	};
}

TEST_CASE_METHOD(PetManagerFixture, "replace oldest keeps the group at its cap", "[pets]")
{
	const auto owner = SpawnOwner();
	std::vector<std::shared_ptr<GameCreatureS>> pets;
	for (int i = 0; i < 6; ++i)
	{
		pets.push_back(SpawnPetCreature());
		owner->GetPets().AddTemporaryPet(*pets.back(), Group, 5, pet_limit_policy::ReplaceOldest);
	}

	CHECK(owner->GetPets().GetGroupCount(Group) == 5);
	CHECK(RunUntil([&] { return pets.front()->GetWorldInstance() == nullptr; }));
	for (size_t i = 1; i < pets.size(); ++i)
	{
		CHECK(pets[i]->GetWorldInstance() == world);
	}
	CHECK(owner->GetPets().GetGroupCount(Group) == 5);
}

TEST_CASE_METHOD(PetManagerFixture, "fail at cap refuses the sixth", "[pets]")
{
	const auto owner = SpawnOwner();
	std::vector<std::shared_ptr<GameCreatureS>> pets;
	for (int i = 0; i < 5; ++i)
	{
		CHECK(owner->GetPets().CanAddToGroup(Group, 5, pet_limit_policy::FailAtCap) == pet_result::Ok);
		pets.push_back(SpawnPetCreature());
		owner->GetPets().AddTemporaryPet(*pets.back(), Group, 5, pet_limit_policy::FailAtCap);
	}

	CHECK(owner->GetPets().CanAddToGroup(Group, 5, pet_limit_policy::FailAtCap) == pet_result::TooManyPets);
	CHECK(owner->GetPets().CanAddToGroup(Group, 5, pet_limit_policy::ReplaceOldest) == pet_result::Ok);
	CHECK(owner->GetPets().GetGroupCount(Group) == 5);
}

TEST_CASE_METHOD(PetManagerFixture, "fail at cap fails the summon cast before it starts", "[pets]")
{
	auto* effect = summonSpell->mutable_effects(0);
	effect->set_pet_max_count(1);
	effect->set_pet_limit_policy(pet_limit_policy::FailAtCap);

	const auto owner = SpawnOwner();
	REQUIRE(CastSummon(*owner) == spell_cast_result::CastOkay);
	REQUIRE(RunUntil([&] { return CountPetCreaturesInWorld() == 1; }));

	CHECK(CastSummon(*owner) == spell_cast_result::FailedTooManyPets);
	RunFor(std::chrono::milliseconds(50));
	CHECK(CountPetCreaturesInWorld() == 1);
	CHECK(owner->GetPets().GetGroupCount(SummonSpellId) == 1);
}

TEST_CASE_METHOD(PetManagerFixture, "groups are counted separately", "[pets]")
{
	const auto owner = SpawnOwner();
	const auto a = SpawnPetCreature();
	const auto b = SpawnPetCreature();
	const auto c = SpawnPetCreature();
	owner->GetPets().AddTemporaryPet(*a, Group, 1, pet_limit_policy::FailAtCap);
	owner->GetPets().AddTemporaryPet(*b, OtherGroup, 2, pet_limit_policy::FailAtCap);
	owner->GetPets().AddTemporaryPet(*c, OtherGroup, 2, pet_limit_policy::FailAtCap);

	CHECK(owner->GetPets().GetGroupCount(Group) == 1);
	CHECK(owner->GetPets().GetGroupCount(OtherGroup) == 2);
	CHECK(owner->GetPets().CanAddToGroup(Group, 1, pet_limit_policy::FailAtCap) == pet_result::TooManyPets);
	CHECK(owner->GetPets().CanAddToGroup(OtherGroup, 3, pet_limit_policy::FailAtCap) == pet_result::Ok);
	CHECK(owner->GetPets().CanAddToGroup(3, 1, pet_limit_policy::FailAtCap) == pet_result::Ok);
}

TEST_CASE_METHOD(PetManagerFixture, "a pet that despawns on its own leaves its group", "[pets]")
{
	const auto owner = SpawnOwner();
	const auto pet = SpawnPetCreature();
	owner->GetPets().AddTemporaryPet(*pet, Group, 5, pet_limit_policy::ReplaceOldest);
	REQUIRE(owner->GetPets().GetGroupCount(Group) == 1);

	world->RemoveGameObject(*pet);

	CHECK(owner->GetPets().GetGroupCount(Group) == 0);
}

TEST_CASE_METHOD(PetManagerFixture, "owner death despawns temporary pets", "[pets]")
{
	const auto owner = SpawnPlayer();
	const auto a = SpawnPetCreature();
	const auto b = SpawnPetCreature();
	owner->GetPets().AddTemporaryPet(*a, Group, 5, pet_limit_policy::ReplaceOldest);
	owner->GetPets().AddTemporaryPet(*b, OtherGroup, 5, pet_limit_policy::ReplaceOldest);

	owner->Kill(nullptr);

	CHECK(owner->GetPets().GetGroupCount(Group) == 0);
	CHECK(owner->GetPets().GetGroupCount(OtherGroup) == 0);
	CHECK(RunUntil([&] { return a->GetWorldInstance() == nullptr && b->GetWorldInstance() == nullptr; }));
}

TEST_CASE_METHOD(PetManagerFixture, "owner despawn despawns temporary pets", "[pets]")
{
	// A player logging out is removed from the world the same way.
	const auto owner = SpawnPlayer();
	const auto pet = SpawnPetCreature();
	owner->GetPets().AddTemporaryPet(*pet, Group, 5, pet_limit_policy::ReplaceOldest);

	world->RemoveGameObject(*owner);

	CHECK(RunUntil([&] { return pet->GetWorldInstance() == nullptr; }));
}

TEST_CASE_METHOD(PetManagerFixture, "npc owner reset despawns its pets", "[pets]")
{
	const auto owner = SpawnOwner();
	const auto pet = SpawnPetCreature();
	owner->GetPets().AddTemporaryPet(*pet, Group, 5, pet_limit_policy::ReplaceOldest);

	owner->GetAI()->Reset();

	CHECK(owner->GetPets().GetGroupCount(Group) == 0);
	CHECK(RunUntil([&] { return pet->GetWorldInstance() == nullptr; }));
	CHECK(owner->GetWorldInstance() == world);
}

TEST_CASE_METHOD(PetManagerFixture, "summon pet spawns a pet that belongs to its owner", "[pets]")
{
	const auto owner = SpawnOwner();
	REQUIRE(CastSummon(*owner) == spell_cast_result::CastOkay);
	REQUIRE(RunUntil([&] { return CountPetCreaturesInWorld() == 1; }));

	GameCreatureS* pet = PetCreaturesInWorld().front();
	CHECK(pet->IsPet());
	CHECK(pet->GetPetGroup() == SummonSpellId);
	CHECK(pet->GetOwnerGuid() == owner->GetGuid());
	CHECK(pet->GetSummonOwner() == owner);
	CHECK_FALSE(pet->GrantsKillRewards());
	CHECK(pet->GetFactionTemplate() == project.factionTemplates.getById(PetTemplate));
	CHECK(owner->GetPets().GetGroupCount(SummonSpellId) == 1);

	uint32 visited = 0;
	owner->GetPets().ForEachPet([&](GameCreatureS& each) { CHECK(&each == pet); ++visited; });
	CHECK(visited == 1);
}

TEST_CASE_METHOD(PetManagerFixture, "summon posted after the owner is gone spawns nothing", "[pets]")
{
	auto owner = SpawnOwner();
	REQUIRE(CastSummon(*owner) == spell_cast_result::CastOkay);

	// The spawn is deferred; the owner leaves the world and is destroyed before it runs.
	world->RemoveGameObject(*owner);
	owner.reset();

	RunFor(std::chrono::milliseconds(50));
	CHECK(CountPetCreaturesInWorld() == 0);
}

TEST_CASE_METHOD(PetManagerFixture, "summon posted after the owner left the world spawns nothing", "[pets]")
{
	const auto owner = SpawnOwner();
	REQUIRE(CastSummon(*owner) == spell_cast_result::CastOkay);

	world->RemoveGameObject(*owner);

	RunFor(std::chrono::milliseconds(50));
	CHECK(CountPetCreaturesInWorld() == 0);
	CHECK(owner->GetPets().GetGroupCount(SummonSpellId) == 0);
}

TEST_CASE_METHOD(PetManagerFixture, "temporary pet without duration lives while the owner lives", "[pets]")
{
	const auto owner = SpawnOwner();
	REQUIRE(CastSummon(*owner) == spell_cast_result::CastOkay);
	REQUIRE(RunUntil([&] { return CountPetCreaturesInWorld() == 1; }));

	RunFor(std::chrono::milliseconds(200));
	CHECK(CountPetCreaturesInWorld() == 1);

	owner->Kill(nullptr);
	CHECK(RunUntil([&] { return CountPetCreaturesInWorld() == 0; }));
}

TEST_CASE_METHOD(PetManagerFixture, "temporary pet with a duration despawns when it ends", "[pets]")
{
	summonSpell->set_duration(50);

	const auto owner = SpawnOwner();
	REQUIRE(CastSummon(*owner) == spell_cast_result::CastOkay);
	REQUIRE(RunUntil([&] { return CountPetCreaturesInWorld() == 1; }));

	CHECK(RunUntil([&] { return CountPetCreaturesInWorld() == 0; }));
	CHECK(owner->GetPets().GetGroupCount(SummonSpellId) == 0);
}

TEST_CASE_METHOD(PetManagerFixture, "summon pet with replace oldest renews a full group", "[pets]")
{
	auto* effect = summonSpell->mutable_effects(0);
	effect->set_basepoints(2);
	effect->set_pet_group(7);
	effect->set_pet_max_count(3);
	effect->set_pet_limit_policy(pet_limit_policy::ReplaceOldest);

	const auto owner = SpawnOwner();
	REQUIRE(CastSummon(*owner) == spell_cast_result::CastOkay);
	REQUIRE(RunUntil([&] { return CountPetCreaturesInWorld() == 2; }));
	REQUIRE(CastSummon(*owner) == spell_cast_result::CastOkay);

	CHECK(RunUntil([&] { return CountPetCreaturesInWorld() == 3 && owner->GetPets().GetGroupCount(7) == 3; }));
	RunFor(std::chrono::milliseconds(50));
	CHECK(CountPetCreaturesInWorld() == 3);
}

TEST_CASE_METHOD(PetManagerFixture, "ResolvePetLevel uses class level for players and creature level for npcs", "[pets]")
{
	const auto player = SpawnPlayer(10);
	CharacterClassData classData;
	classData.classId = classEntry->id();
	classData.classLevel = 4;
	player->SetKnownClasses({ classData });

	CHECK(ResolvePetLevel(pet_level_source::ClassLevel, *player, *petEntry) == 4);
	CHECK(ResolvePetLevel(pet_level_source::CharacterLevel, *player, *petEntry) == 10);
	CHECK(ResolvePetLevel(pet_level_source::Creature, *player, *petEntry) == 2);

	const auto npc = SpawnOwner();
	CHECK(ResolvePetLevel(pet_level_source::ClassLevel, *npc, *petEntry) == 7);
	CHECK(ResolvePetLevel(pet_level_source::CharacterLevel, *npc, *petEntry) == 7);
	CHECK(ResolvePetLevel(pet_level_source::Creature, *npc, *petEntry) == 2);
}

TEST_CASE_METHOD(PetManagerFixture, "summoned pet takes its level from the level source", "[pets]")
{
	summonSpell->mutable_effects(0)->set_pet_level_source(pet_level_source::CharacterLevel);

	const auto owner = SpawnOwner();
	REQUIRE(CastSummon(*owner) == spell_cast_result::CastOkay);
	REQUIRE(RunUntil([&] { return CountPetCreaturesInWorld() == 1; }));

	CHECK(PetCreaturesInWorld().front()->GetLevel() == 7);
}
