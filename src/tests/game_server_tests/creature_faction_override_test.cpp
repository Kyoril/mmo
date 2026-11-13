// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// A trigger can give a creature another faction for one fight (the SetFaction action): the toll
// keeper you can talk to turns into an enemy when the talk goes wrong. The override must change who
// the creature is hostile to, and it must end by itself when the creature evades - otherwise the
// next player finds a permanently hostile quest npc.

#include "catch.hpp"

#include "game_server/ai/creature_ai.h"
#include "game_server/condition_mgr.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/trigger_handler.h"
#include "game_server/world/universe.h"
#include "game_server/world/world_instance.h"
#include "game_server/world/world_instance_manager.h"
#include "base/id_generator.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "asio/io_service.hpp"

#include <memory>

using namespace mmo;

namespace
{
	constexpr uint32 FriendlyTemplate = 1;
	constexpr uint32 BanditTemplate = 2;
	constexpr uint32 TownsfolkFaction = 10;
	constexpr uint32 BanditFaction = 20;

	struct NullTriggerHandler final : ITriggerHandler
	{
		void ExecuteTrigger(const proto::TriggerEntry&, TriggerContext, uint32, bool) override {}
	};

	struct FactionOverrideFixture
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

		proto::UnitEntry* tollKeeperEntry = nullptr;
		proto::UnitEntry* travellerEntry = nullptr;

		FactionOverrideFixture()
		{
			// Townsfolk and bandits are enemies of each other
			auto* friendly = project.factionTemplates.add(FriendlyTemplate);
			friendly->set_name("Townsfolk");
			friendly->set_flags(0);
			friendly->set_faction(TownsfolkFaction);
			friendly->add_enemies(BanditFaction);

			auto* bandit = project.factionTemplates.add(BanditTemplate);
			bandit->set_name("Bandits");
			bandit->set_flags(0);
			bandit->set_faction(BanditFaction);
			bandit->add_enemies(TownsfolkFaction);

			tollKeeperEntry = AddUnit(1, "Toll Keeper");
			travellerEntry = AddUnit(2, "Traveller");

			// No map files exist for this directory: no nav mesh, so a reset teleports home.
			auto* map = project.maps.add(0);
			map->set_name("Faction Override Test");
			map->set_directory("Worlds/__faction_override_test__/__faction_override_test__");

			conditionMgr = std::make_unique<ConditionMgr>(project.conditions);
			manager = std::make_unique<WorldInstanceManager>(io, universe, project, objectIds, triggerHandler, *conditionMgr);
			world = &manager->CreateInstance(0);
		}

		proto::UnitEntry* AddUnit(const uint32 id, const char* name)
		{
			auto* entry = project.units.add(id);
			entry->set_name(name);
			entry->set_minlevel(7);
			entry->set_maxlevel(7);
			entry->set_factiontemplate(FriendlyTemplate);
			entry->set_malemodel(0);
			entry->set_femalemodel(0);
			entry->set_type(0);
			entry->set_family(0);
			entry->set_minlevelhealth(100);
			entry->set_maxlevelhealth(100);
			return entry;
		}

		std::shared_ptr<GameCreatureS> Spawn(const proto::UnitEntry& entry, const float x)
		{
			auto creature = world->CreateCreature(entry, Vector3(x, 0.0f, 0.0f), 0.0f, 0.0f);
			world->AddGameObject(*creature);
			return creature;
		}

		/// Runs everything that was posted to the io service.
		void RunPosted()
		{
			for (int i = 0; i < 4; ++i)
			{
				io.restart();
				io.poll();
			}
		}

		~FactionOverrideFixture()
		{
			io.stop();
		}
	};
}

TEST_CASE_METHOD(FactionOverrideFixture, "A faction override turns a friendly creature into an enemy", "[creature][faction]")
{
	auto tollKeeper = Spawn(*tollKeeperEntry, 0.0f);
	auto traveller = Spawn(*travellerEntry, 3.0f);

	REQUIRE_FALSE(tollKeeper->HasFactionTemplateOverride());
	REQUIRE_FALSE(tollKeeper->UnitIsEnemy(*traveller));

	REQUIRE(tollKeeper->SetFactionTemplateOverride(BanditTemplate));

	CHECK(tollKeeper->HasFactionTemplateOverride());
	CHECK(tollKeeper->Get<uint32>(object_fields::FactionTemplate) == BanditTemplate);
	CHECK(tollKeeper->UnitIsEnemy(*traveller));
	CHECK(traveller->UnitIsEnemy(*tollKeeper));
}

TEST_CASE_METHOD(FactionOverrideFixture, "An unknown faction template is refused and changes nothing", "[creature][faction]")
{
	auto tollKeeper = Spawn(*tollKeeperEntry, 0.0f);

	CHECK_FALSE(tollKeeper->SetFactionTemplateOverride(999));
	CHECK_FALSE(tollKeeper->HasFactionTemplateOverride());
	CHECK(tollKeeper->Get<uint32>(object_fields::FactionTemplate) == FriendlyTemplate);
}

TEST_CASE_METHOD(FactionOverrideFixture, "Clearing a faction override restores the entry's faction", "[creature][faction]")
{
	auto tollKeeper = Spawn(*tollKeeperEntry, 0.0f);
	auto traveller = Spawn(*travellerEntry, 3.0f);

	REQUIRE(tollKeeper->SetFactionTemplateOverride(BanditTemplate));
	tollKeeper->ClearFactionTemplateOverride();

	CHECK_FALSE(tollKeeper->HasFactionTemplateOverride());
	CHECK(tollKeeper->Get<uint32>(object_fields::FactionTemplate) == FriendlyTemplate);
	CHECK_FALSE(tollKeeper->UnitIsEnemy(*traveller));
}

TEST_CASE_METHOD(FactionOverrideFixture, "A faction override ends when the creature resets", "[creature][faction][evade]")
{
	auto tollKeeper = Spawn(*tollKeeperEntry, 0.0f);
	auto traveller = Spawn(*travellerEntry, 3.0f);
	tollKeeper->SetMovementType(creature_movement::None);
	tollKeeper->GetAI()->Idle();
	RunPosted();

	REQUIRE(tollKeeper->SetFactionTemplateOverride(BanditTemplate));
	REQUIRE(tollKeeper->UnitIsEnemy(*traveller));

	// The fight is over without a death: the creature evades and walks home
	tollKeeper->GetAI()->Reset();
	RunPosted();

	CHECK_FALSE(tollKeeper->HasFactionTemplateOverride());
	CHECK_FALSE(tollKeeper->UnitIsEnemy(*traveller));
}
