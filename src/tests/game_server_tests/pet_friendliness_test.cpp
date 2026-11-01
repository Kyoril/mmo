// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Pets are units whose Owner field names the unit they belong to. Whether a unit is friendly or
// hostile is decided by the faction of its top owner, so a pet follows its owner's allegiances no
// matter which faction template the pet's own creature entry carries.

#include "catch.hpp"

#include "game_server/condition_mgr.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/trigger_handler.h"
#include "game_server/world/universe.h"
#include "game_server/world/world_instance.h"
#include "game_server/world/world_instance_manager.h"
#include "base/id_generator.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/classes.pb.h"
#include "asio/io_service.hpp"

#include <memory>

using namespace mmo;

namespace
{
	constexpr uint32 PlayerTemplate = 1;
	constexpr uint32 MonsterTemplate = 2;

	struct NullTriggerHandler final : ITriggerHandler
	{
		void ExecuteTrigger(const proto::TriggerEntry&, TriggerContext, uint32, bool) override {}
	};

	struct PetFixture
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

		proto::UnitEntry* unitEntry = nullptr;
		proto::ClassEntry* classEntry = nullptr;
		proto::RaceEntry* raceEntry = nullptr;
		uint64 nextPlayerGuid = 0x4000;

		PetFixture()
		{
			// Faction 1 (players) and faction 2 (monsters) are enemies of each other.
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

			unitEntry = project.units.add(1);
			unitEntry->set_name("Critter");
			unitEntry->set_minlevel(1);
			unitEntry->set_maxlevel(1);
			unitEntry->set_factiontemplate(MonsterTemplate);
			unitEntry->set_malemodel(0);
			unitEntry->set_femalemodel(0);
			unitEntry->set_type(0);
			unitEntry->set_family(0);
			unitEntry->set_minlevelhealth(100);
			unitEntry->set_maxlevelhealth(100);

			classEntry = project.classes.add(1);
			classEntry->set_powertype(proto::ClassEntry_PowerType_MANA);
			for (uint32 i = 0; i < 5; ++i)
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

			auto* map = project.maps.add(0);
			map->set_name("Pet Test");
			map->set_directory("Worlds/__pet_test__/__pet_test__");

			conditionMgr = std::make_unique<ConditionMgr>(project.conditions);
			manager = std::make_unique<WorldInstanceManager>(io, universe, project, objectIds, triggerHandler, *conditionMgr);
			world = &manager->CreateInstance(0);
		}

		std::shared_ptr<GamePlayerS> SpawnPlayer()
		{
			auto player = std::make_shared<GamePlayerS>(project, timers);
			player->Initialize();
			player->Set<uint64>(object_fields::Guid, nextPlayerGuid++, false);
			player->SetClass(*classEntry);
			player->SetRace(*raceEntry);
			player->SetLevel(1);
			player->Set<uint32>(object_fields::FactionTemplate, PlayerTemplate);
			player->Relocate(Vector3(0.0f, 0.0f, 0.0f), Radian(0.0f));
			world->AddGameObject(*player);
			return player;
		}

		std::shared_ptr<GameCreatureS> SpawnCreature()
		{
			auto creature = world->CreateCreature(*unitEntry, Vector3(1.0f, 0.0f, 0.0f), 0.0f, 0.0f);
			world->AddGameObject(*creature);
			return creature;
		}

		~PetFixture()
		{
			io.stop();
		}
	};
}

TEST_CASE_METHOD(PetFixture, "pet of a player is friendly to its owner even with a hostile faction", "[pets]")
{
	const auto player = SpawnPlayer();
	const auto pet = SpawnCreature();

	REQUIRE(pet->UnitIsEnemy(*player));
	pet->SetOwnerGuid(player->GetGuid());

	CHECK(pet->UnitIsFriendly(*player));
	CHECK(player->UnitIsFriendly(*pet));
	CHECK_FALSE(pet->UnitIsEnemy(*player));
	CHECK_FALSE(player->UnitIsEnemy(*pet));
}

TEST_CASE_METHOD(PetFixture, "pet is hostile to its owner's enemies", "[pets]")
{
	const auto player = SpawnPlayer();
	const auto pet = SpawnCreature();
	const auto other = SpawnCreature();
	pet->SetOwnerGuid(player->GetGuid());

	CHECK(pet->UnitIsEnemy(*other));
	CHECK(other->UnitIsEnemy(*pet));
	CHECK_FALSE(pet->UnitIsFriendly(*other));
}

TEST_CASE_METHOD(PetFixture, "pets of the same owner are friendly", "[pets]")
{
	const auto player = SpawnPlayer();
	const auto petA = SpawnCreature();
	const auto petB = SpawnCreature();
	REQUIRE(petA->UnitIsFriendly(*petB));

	petA->SetOwnerGuid(player->GetGuid());
	petB->SetOwnerGuid(player->GetGuid());

	CHECK(petA->SharesTopOwnerWith(*petB));
	CHECK(petA->UnitIsFriendly(*petB));
	CHECK_FALSE(petA->UnitIsEnemy(*petB));
}

TEST_CASE_METHOD(PetFixture, "units without an owner keep their faction behavior", "[pets]")
{
	const auto player = SpawnPlayer();
	const auto monster = SpawnCreature();

	CHECK(&monster->GetTopOwner() == monster.get());
	CHECK(monster->UnitIsEnemy(*player));
	CHECK_FALSE(monster->UnitIsFriendly(*player));
}

TEST_CASE_METHOD(PetFixture, "owner chain deeper than the limit or looping terminates", "[pets]")
{
	const auto a = SpawnCreature();
	const auto b = SpawnCreature();
	a->SetOwnerGuid(b->GetGuid());
	b->SetOwnerGuid(a->GetGuid());

	// Neither call may hang, and a loop still resolves to some unit of the loop.
	CHECK_FALSE(a->UnitIsEnemy(*b));
	CHECK(a->UnitIsFriendly(*b));
	const GameUnitS& top = a->GetTopOwner();
	CHECK((&top == a.get() || &top == b.get()));

	// A chain longer than the limit stops after MaxOwnerChainDepth links.
	std::vector<std::shared_ptr<GameCreatureS>> chain;
	for (uint32 i = 0; i < GameUnitS::MaxOwnerChainDepth + 3; ++i)
	{
		chain.push_back(SpawnCreature());
		if (i > 0)
		{
			chain[i]->SetOwnerGuid(chain[i - 1]->GetGuid());
		}
	}
	CHECK(&chain.back()->GetTopOwner() == chain[chain.size() - 1 - GameUnitS::MaxOwnerChainDepth].get());
}

TEST_CASE_METHOD(PetFixture, "a unit outside any world is its own top owner", "[pets]")
{
	const auto player = SpawnPlayer();

	// Never added to the world instance, so the owner guid cannot be resolved.
	GamePlayerS loose(project, timers);
	loose.Initialize();
	loose.Set<uint64>(object_fields::Guid, 0x9000, false);
	loose.SetOwnerGuid(player->GetGuid());

	CHECK(&loose.GetTopOwner() == &loose);
}
