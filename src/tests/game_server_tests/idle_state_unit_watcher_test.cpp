// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// A creature that is still moving when it enters the idle state used to take the world server
// down. CreatureAIIdleState::OnEnter creates its unit watcher, then calls OnCreatureMovementChanged
// before starting the watcher; for a creature without idle movement that stops the mover, which
// relocates the creature, which reaches CreatureAIIdleState::OnControlledMoved and updates the
// watcher's shape. TiledUnitWatcher::OnShapeUpdated then unwatched the tiles of the shape it was
// constructed with - tiles Start() had never watched.
//
// The player hit this with a feared creature: the fear movement controller keeps the mover running
// while the creature's reset state hands over to idle, so the creature is moving at the moment it
// becomes idle.

#include "catch.hpp"

#include "test_unit_factory.h"
#include "../nav_mesh_tests/synthetic_nav_map.h"

#include "game_server/ai/creature_ai.h"
#include "game_server/condition_mgr.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/trigger_handler.h"
#include "game_server/unit_mover.h"
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
	constexpr uint32 GuardTemplate = 1;
	constexpr uint32 IntruderTemplate = 2;

	struct NullTriggerHandler final : ITriggerHandler
	{
		void ExecuteTrigger(const proto::TriggerEntry&, TriggerContext, uint32, bool) override {}
	};

	struct IdleWatcherFixture
	{
		/// One flat nav page, so creatures can actually be given a path to walk.
		test_nav::SyntheticNavMap navFiles{ { { 32, 32 } } };

		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;
		Universe universe{ io, timers };
		IdGenerator<uint64> objectIds{ 1 };
		NullTriggerHandler triggerHandler;
		std::unique_ptr<ConditionMgr> conditionMgr;
		std::unique_ptr<WorldInstanceManager> manager;
		WorldInstance* world = nullptr;

		proto::UnitEntry* guardEntry = nullptr;
		proto::UnitEntry* intruderEntry = nullptr;

		IdleWatcherFixture()
		{
			AddFactionTemplate(GuardTemplate, 1, 2);
			AddFactionTemplate(IntruderTemplate, 2, 1);

			guardEntry = AddUnitEntry(1, "Guard", GuardTemplate);
			intruderEntry = AddUnitEntry(2, "Intruder", IntruderTemplate);

			auto* map = project.maps.add(0);
			map->set_name("Idle Watcher Test");
			map->set_directory(test_nav::SyntheticNavMap::Name);

			conditionMgr = std::make_unique<ConditionMgr>(project.conditions);
			manager = std::make_unique<WorldInstanceManager>(io, universe, project, objectIds, triggerHandler, *conditionMgr);
			world = &manager->CreateInstance(0);
		}

		void AddFactionTemplate(const uint32 id, const uint32 faction, const uint32 enemyFaction)
		{
			auto* entry = project.factionTemplates.add(id);
			entry->set_name("Test");
			entry->set_flags(0);
			entry->set_faction(faction);
			entry->add_enemies(enemyFaction);
		}

		proto::UnitEntry* AddUnitEntry(const uint32 id, const char* name, const uint32 factionTemplate)
		{
			auto* entry = project.units.add(id);
			entry->set_name(name);
			entry->set_minlevel(3);
			entry->set_maxlevel(3);
			entry->set_factiontemplate(factionTemplate);
			entry->set_malemodel(0);
			entry->set_femalemodel(0);
			entry->set_type(0);
			entry->set_family(0);
			entry->set_minlevelhealth(100);
			entry->set_maxlevelhealth(100);
			return entry;
		}

		std::shared_ptr<GameCreatureS> Spawn(const proto::UnitEntry& entry, const Vector3& position)
		{
			auto creature = world->CreateCreature(entry, position, 0.0f, 0.0f);
			world->AddGameObject(*creature);
			return creature;
		}

		/// Runs everything that was posted to the io service.
		void RunPosted()
		{
			for (int i = 0; i < 6; ++i)
			{
				io.restart();
				io.poll();
			}
		}

		~IdleWatcherFixture()
		{
			io.stop();
		}
	};
}

TEST_CASE_METHOD(IdleWatcherFixture, "A creature entering idle while it is still moving keeps watching its tiles", "[creature_ai][idle][unit_watcher]")
{
	const Vector3 home = test_nav::PageCenter(32, 32);

	auto guard = Spawn(*guardEntry, home);
	auto intruder = Spawn(*intruderEntry, home + Vector3(0.0f, 0.0f, 5.0f));
	RunPosted();

	// The creature is walking when the AI hands over to the idle state - what a fear movement
	// controller does to a creature whose AI is resetting.
	REQUIRE(guard->GetMover().MoveTo(home + Vector3(0.0f, 0.0f, 20.0f), 0.0f));
	REQUIRE(guard->GetMover().IsMoving());

	guard->GetAI()->Idle();
	RunPosted();

	// The idle state stopped the move, which relocated the creature and updated the watcher's
	// shape before the watcher had been started.
	CHECK_FALSE(guard->GetMover().IsMoving());

	// The watcher must still report the units around the creature: the hostile intruder standing
	// next to it drags the guard into combat.
	CHECK(guard->IsInCombat());
}
