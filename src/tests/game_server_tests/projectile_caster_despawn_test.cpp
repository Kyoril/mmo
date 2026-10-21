// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// A spell with a travel speed finishes its cast at once, hands the caster back to idle and then
// lives on by itself until the projectile lands. Despawning the caster abandons the caster's
// current cast and its procs, but a projectile already in flight was neither: it landed later on
// a caster that had left the world - or been freed - and resolved its effects through it. That is
// the world server crash in SingleCastState::CalculateFinalCooldown (2026-10-20, bots fighting in
// a dungeon). Despawning the caster must stop its projectiles too.

#include "catch.hpp"

#include "game_server/condition_mgr.h"
#include "game_server/objects/game_creature_s.h"
#include "game/spell_target_map.h"
#include "game_server/trigger_handler.h"
#include "game_server/world/universe.h"
#include "game_server/world/world_instance.h"
#include "game_server/world/world_instance_manager.h"
#include "base/id_generator.h"
#include "base/timer_queue.h"
#include "game/spell.h"
#include "shared/proto_data/project.h"
#include "asio/io_service.hpp"

#include <chrono>
#include <memory>

using namespace mmo;

namespace
{
	constexpr uint32 CasterTemplate = 1;
	constexpr uint32 VictimTemplate = 2;
	constexpr uint32 LongRange = 1;
	constexpr uint32 BoltSpell = 1;
	constexpr int32 BoltDamage = 10;

	struct NullTriggerHandler final : ITriggerHandler
	{
		void ExecuteTrigger(const proto::TriggerEntry&, TriggerContext, uint32, bool) override {}
	};

	struct ProjectileFixture
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

		proto::UnitEntry* casterEntry = nullptr;
		proto::UnitEntry* victimEntry = nullptr;
		proto::SpellEntry* bolt = nullptr;

		ProjectileFixture()
		{
			AddFactionTemplate(CasterTemplate, 1, 2);
			AddFactionTemplate(VictimTemplate, 2, 1);

			casterEntry = AddUnitEntry(1, "Caster", CasterTemplate);
			victimEntry = AddUnitEntry(2, "Victim", VictimTemplate);

			auto* range = project.ranges.add(LongRange);
			range->set_range(40.0f);

			// An instant bolt that flies for about four seconds and deals its damage on impact.
			bolt = project.spells.add(BoltSpell);
			bolt->set_name("Bolt");
			bolt->set_rangetype(LongRange);
			bolt->set_speed(5.0f);
			bolt->add_attributes(0);
			bolt->add_attributes(0);

			auto* effect = bolt->add_effects();
			effect->set_index(0);
			effect->set_type(spell_effects::SchoolDamage);
			effect->set_targeta(spell_effect_targets::TargetEnemy);
			effect->set_basepoints(BoltDamage);

			// No map files exist for this directory: no nav mesh, no collision, LOS is unblocked.
			auto* map = project.maps.add(0);
			map->set_name("Projectile Caster Despawn Test");
			map->set_directory("Worlds/__projectile_despawn_test__/__projectile_despawn_test__");

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

		/// Runs the timer queue in real time for the given duration.
		void RunFor(const std::chrono::milliseconds duration)
		{
			const auto deadline = std::chrono::steady_clock::now() + duration;
			while (std::chrono::steady_clock::now() < deadline)
			{
				io.restart();
				io.run_for(std::chrono::milliseconds(20));
			}
		}

		~ProjectileFixture()
		{
			io.stop();
		}
	};
}

TEST_CASE_METHOD(ProjectileFixture, "A projectile lands and resolves while its caster stays in the world", "[spell][projectile]")
{
	auto caster = Spawn(*casterEntry, Vector3(0.0f, 0.0f, 0.0f));
	auto victim = Spawn(*victimEntry, Vector3(20.0f, 0.0f, 0.0f));

	SpellTargetMap targetMap;
	targetMap.SetTargetMap(spell_cast_target_flags::Unit);
	targetMap.SetUnitTarget(victim->GetGuid());

	REQUIRE(caster->CastSpell(targetMap, *bolt, 0, false, 0, true) == spell_cast_result::CastOkay);

	// In flight: the damage is dealt on impact.
	const uint32 healthBefore = victim->GetHealth();

	RunFor(std::chrono::milliseconds(5000));

	CHECK(victim->GetHealth() < healthBefore);
}

TEST_CASE_METHOD(ProjectileFixture, "A projectile in flight is dropped when its caster leaves the world", "[spell][projectile]")
{
	auto caster = Spawn(*casterEntry, Vector3(0.0f, 0.0f, 0.0f));
	auto victim = Spawn(*victimEntry, Vector3(20.0f, 0.0f, 0.0f));

	SpellTargetMap targetMap;
	targetMap.SetTargetMap(spell_cast_target_flags::Unit);
	targetMap.SetUnitTarget(victim->GetGuid());

	REQUIRE(caster->CastSpell(targetMap, *bolt, 0, false, 0, true) == spell_cast_result::CastOkay);

	// The next cast replaces the finished one as the caster's current cast, while the first
	// projectile is still in the air: that one is no longer reachable through the caster. Wait out
	// the global cooldown first, well before the first bolt lands.
	RunFor(std::chrono::milliseconds(1700));
	REQUIRE(victim->GetHealth() == victim->GetMaxHealth());
	REQUIRE(caster->CastSpell(targetMap, *bolt, 0, false, 0, true) == spell_cast_result::CastOkay);

	const uint32 healthBefore = victim->GetHealth();

	// Gone for good, like a despawned corpse or a player who logged out: a projectile still
	// landing now resolves through freed memory.
	world->RemoveGameObject(*caster);
	const std::weak_ptr<GameCreatureS> weakCaster = caster;
	caster.reset();
	REQUIRE(weakCaster.expired());

	RunFor(std::chrono::milliseconds(5000));

	CHECK(victim->GetHealth() == healthBefore);
}
