// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// An idle creature that gets attacked calls nearby idle allies into the fight
// (CreatureAI::OnThreatened). Damage reaches the AI through takenDamage before threatened, so
// the idle state used to enter combat straight from OnDamage and never called for help. The
// only remaining assist path was the idle unit watcher, which fires on movement only: a
// stationary caster pulled from range fought alone while its camp mates stood next to it.

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
#include "game/movement_info.h"
#include "shared/proto_data/project.h"
#include "asio/io_service.hpp"

#include <memory>

using namespace mmo;

namespace
{
	constexpr uint32 BanditTemplate = 1;
	constexpr uint32 IntruderTemplate = 2;

	struct NullTriggerHandler final : ITriggerHandler
	{
		void ExecuteTrigger(const proto::TriggerEntry&, TriggerContext, uint32, bool) override {}
	};

	struct AssistFixture
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

		proto::UnitEntry* banditEntry = nullptr;
		proto::UnitEntry* intruderEntry = nullptr;

		AssistFixture()
		{
			AddFactionTemplate(BanditTemplate, 1, 2);
			AddFactionTemplate(IntruderTemplate, 2, 1);

			banditEntry = AddUnitEntry(1, "Bandit", BanditTemplate);
			intruderEntry = AddUnitEntry(2, "Intruder", IntruderTemplate);

			// No map files exist for this directory: no nav mesh, no collision, LOS is unblocked.
			auto* map = project.maps.add(0);
			map->set_name("Assist Test");
			map->set_directory("Worlds/__assist_test__/__assist_test__");

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

		/// Spawns a creature that never moves on its own (no idle movement, rooted in combat), so
		/// nothing in the test can wake the movement-driven unit watcher.
		std::shared_ptr<GameCreatureS> SpawnStationary(const proto::UnitEntry& entry, const Vector3& position)
		{
			auto creature = world->CreateCreature(entry, position, 0.0f, 0.0f);
			world->AddGameObject(*creature);

			// After the spawn: the unit finder only knows the creature once it was added. No unit
			// watcher exists yet, so this is not a movement anybody could observe.
			creature->ApplyMovementInfo({ movement_flags::Rooted, GetAsyncTimeMs(), position, Radian(0.0f), Radian(0), 0, Vector3::Zero });
			return creature;
		}

		/// Runs everything that was posted to the io service (assist calls are posted).
		void RunPosted()
		{
			for (int i = 0; i < 4; ++i)
			{
				io.restart();
				io.poll();
			}
		}

		~AssistFixture()
		{
			io.stop();
		}
	};
}

TEST_CASE_METHOD(AssistFixture, "Idle ally joins the fight when a stationary neighbour is attacked", "[creature_ai][assist]")
{
	// The pulled caster and its camp mate stand 4 m apart; the attacker is far outside both
	// creatures' aggro radius, like a player pulling with a ranged spell.
	auto caster = SpawnStationary(*banditEntry, Vector3(0.0f, 0.0f, 0.0f));
	auto campMate = SpawnStationary(*banditEntry, Vector3(4.0f, 0.0f, 0.0f));
	auto intruder = SpawnStationary(*intruderEntry, Vector3(0.0f, 0.0f, 35.0f));

	caster->GetAI()->Idle();
	campMate->GetAI()->Idle();
	RunPosted();

	REQUIRE_FALSE(caster->IsInCombat());
	REQUIRE_FALSE(campMate->IsInCombat());

	caster->Damage(10, 0, intruder.get(), damage_type::MagicalAbility);
	RunPosted();

	REQUIRE(caster->IsInCombat());
	CHECK(campMate->IsInCombat());

	// The camp mate fights the caster's attacker: a creature that puts a unit on its threat
	// list registers itself as one of that unit's attackers.
	bool campMateOnIntruder = false;
	intruder->ForEachAttacker([&](const GameUnitS& attacker)
	{
		campMateOnIntruder = campMateOnIntruder || &attacker == campMate.get();
	});
	CHECK(campMateOnIntruder);
}

TEST_CASE_METHOD(AssistFixture, "Idle ally outside the assist radius stays out of the fight", "[creature_ai][assist]")
{
	auto caster = SpawnStationary(*banditEntry, Vector3(0.0f, 0.0f, 0.0f));
	auto farMate = SpawnStationary(*banditEntry, Vector3(15.0f, 0.0f, 0.0f));
	auto intruder = SpawnStationary(*intruderEntry, Vector3(0.0f, 0.0f, 35.0f));

	caster->GetAI()->Idle();
	farMate->GetAI()->Idle();
	RunPosted();

	caster->Damage(10, 0, intruder.get(), damage_type::MagicalAbility);
	RunPosted();

	REQUIRE(caster->IsInCombat());
	CHECK_FALSE(farMate->IsInCombat());
}
