// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// A stationary creature (town guard) that resets after combat returns to its spawn point and spawn
// facing. The idle state used to record a "patrol return position" for every creature, so the reset
// state treated stationary creatures as patrollers and left them facing wherever combat had turned them.

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
	constexpr float SpawnFacing = 1.5f;
	constexpr float CombatFacing = 0.3f;

	struct NullTriggerHandler final : ITriggerHandler
	{
		void ExecuteTrigger(const proto::TriggerEntry&, TriggerContext, uint32, bool) override {}
	};

	struct ResetFacingFixture
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

		proto::UnitEntry* guardEntry = nullptr;

		ResetFacingFixture()
		{
			auto* factionTemplate = project.factionTemplates.add(1);
			factionTemplate->set_name("Test");
			factionTemplate->set_flags(0);
			factionTemplate->set_faction(1);

			guardEntry = project.units.add(1);
			guardEntry->set_name("Guard");
			guardEntry->set_minlevel(10);
			guardEntry->set_maxlevel(10);
			guardEntry->set_factiontemplate(1);
			guardEntry->set_malemodel(0);
			guardEntry->set_femalemodel(0);
			guardEntry->set_type(0);
			guardEntry->set_family(0);
			guardEntry->set_minlevelhealth(100);
			guardEntry->set_maxlevelhealth(100);

			// No map files exist for this directory: no nav mesh, so the reset teleports home.
			auto* map = project.maps.add(0);
			map->set_name("Reset Facing Test");
			map->set_directory("Worlds/__reset_facing_test__/__reset_facing_test__");

			conditionMgr = std::make_unique<ConditionMgr>(project.conditions);
			manager = std::make_unique<WorldInstanceManager>(io, universe, project, objectIds, triggerHandler, *conditionMgr);
			world = &manager->CreateInstance(0);
		}

		std::shared_ptr<GameCreatureS> SpawnGuard()
		{
			auto creature = world->CreateCreature(*guardEntry, Vector3(0.0f, 0.0f, 0.0f), SpawnFacing, 0.0f);
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

		~ResetFacingFixture()
		{
			io.stop();
		}
	};
}

TEST_CASE_METHOD(ResetFacingFixture, "A stationary creature resets to its spawn point and spawn facing", "[creature_ai][evade]")
{
	auto guard = SpawnGuard();
	guard->SetMovementType(creature_movement::None);
	guard->GetAI()->Idle();
	RunPosted();

	// Combat pulled the guard a few steps away and turned it towards its attacker
	guard->Relocate(Vector3(3.0f, 0.0f, 2.0f), Radian(CombatFacing));
	guard->GetAI()->Reset();
	RunPosted();

	const Vector3 position = guard->GetPosition();
	CHECK(position.x == Approx(0.0f));
	CHECK(position.z == Approx(0.0f));
	CHECK(guard->GetFacing().GetValueRadians() == Approx(SpawnFacing));
}

TEST_CASE_METHOD(ResetFacingFixture, "A patrolling creature resets to where it left its route", "[creature_ai][evade]")
{
	auto guard = SpawnGuard();
	guard->SetMovementType(creature_movement::Patrol);
	guard->GetAI()->Idle();
	RunPosted();

	// The creature left its route here when combat started
	guard->Relocate(Vector3(3.0f, 0.0f, 2.0f), Radian(CombatFacing));
	guard->GetAI()->Reset();
	RunPosted();

	const Vector3 position = guard->GetPosition();
	CHECK(position.x == Approx(3.0f));
	CHECK(position.z == Approx(2.0f));
}
