// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// A cast refused by validation reports why. Creature combat AI reads the result of its own cast
// to react to it: a caster whose spell was blocked by line of sight walks towards its victim
// until it can see it again (CreatureAICombatState::CastSpell). SingleCastState::Activate used to
// replace every validation failure with FailedError, so that branch never ran and a caster whose
// victim stepped behind a wall stood in place, retrying its spells forever.

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

#include <memory>

using namespace mmo;

namespace
{
	constexpr uint32 CasterTemplate = 1;
	constexpr uint32 VictimTemplate = 2;
	constexpr uint32 ShortRange = 1;
	constexpr uint32 BoltSpell = 1;

	struct NullTriggerHandler final : ITriggerHandler
	{
		void ExecuteTrigger(const proto::TriggerEntry&, TriggerContext, uint32, bool) override {}
	};

	struct CastResultFixture
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

		CastResultFixture()
		{
			AddFactionTemplate(CasterTemplate, 1, 2);
			AddFactionTemplate(VictimTemplate, 2, 1);

			casterEntry = AddUnitEntry(1, "Caster", CasterTemplate);
			victimEntry = AddUnitEntry(2, "Victim", VictimTemplate);

			auto* range = project.ranges.add(ShortRange);
			range->set_range(10.0f);

			bolt = project.spells.add(BoltSpell);
			bolt->set_name("Bolt");
			bolt->set_rangetype(ShortRange);
			bolt->add_attributes(0);
			bolt->add_attributes(0);

			// No map files exist for this directory: no nav mesh, no collision, LOS is unblocked.
			auto* map = project.maps.add(0);
			map->set_name("Cast Result Test");
			map->set_directory("Worlds/__cast_result_test__/__cast_result_test__");

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

		~CastResultFixture()
		{
			io.stop();
		}
	};
}

TEST_CASE_METHOD(CastResultFixture, "A cast refused by validation reports the reason, not a generic error", "[spell][cast_result]")
{
	auto caster = Spawn(*casterEntry, Vector3(0.0f, 0.0f, 0.0f));
	auto victim = Spawn(*victimEntry, Vector3(30.0f, 0.0f, 0.0f));

	SpellTargetMap targetMap;
	targetMap.SetTargetMap(spell_cast_target_flags::Unit);
	targetMap.SetUnitTarget(victim->GetGuid());

	const SpellCastResult result = caster->CastSpell(targetMap, *bolt, 0, false, 0, true);

	CHECK(result == spell_cast_result::FailedOutOfRange);
}

TEST_CASE_METHOD(CastResultFixture, "A cast that passes validation still reports success", "[spell][cast_result]")
{
	auto caster = Spawn(*casterEntry, Vector3(0.0f, 0.0f, 0.0f));
	auto victim = Spawn(*victimEntry, Vector3(5.0f, 0.0f, 0.0f));

	SpellTargetMap targetMap;
	targetMap.SetTargetMap(spell_cast_target_flags::Unit);
	targetMap.SetUnitTarget(victim->GetGuid());

	const SpellCastResult result = caster->CastSpell(targetMap, *bolt, 0, false, 0, true);

	CHECK(result == spell_cast_result::CastOkay);
}
