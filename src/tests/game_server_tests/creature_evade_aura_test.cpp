// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// A creature that evades (CreatureAI::Reset) drops the auras its attackers left on it. A player's
// damage-over-time effect used to stay on an evading creature and keep ticking: once the creature
// was home and idle, the next tick put it straight back into combat with that player.

#include "catch.hpp"

#include "test_unit_factory.h"

#include "game_server/ai/creature_ai.h"
#include "game_server/condition_mgr.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/spells/aura_container.h"
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
	constexpr uint32 BanditTemplate = 1;
	constexpr uint32 IntruderTemplate = 2;

	struct NullTriggerHandler final : ITriggerHandler
	{
		void ExecuteTrigger(const proto::TriggerEntry&, TriggerContext, uint32, bool) override {}
	};

	struct EvadeFixture
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

		EvadeFixture()
		{
			AddFactionTemplate(BanditTemplate, 1, 2);
			AddFactionTemplate(IntruderTemplate, 2, 1);

			banditEntry = AddUnitEntry(1, "Bandit", BanditTemplate);
			intruderEntry = AddUnitEntry(2, "Intruder", IntruderTemplate);

			// No map files exist for this directory: no nav mesh, no collision.
			auto* map = project.maps.add(0);
			map->set_name("Evade Test");
			map->set_directory("Worlds/__evade_test__/__evade_test__");

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
			for (int i = 0; i < 4; ++i)
			{
				io.restart();
				io.poll();
			}
		}

		~EvadeFixture()
		{
			io.stop();
		}
	};

	proto::SpellEntry MakeAuraSpell(const uint32 id)
	{
		proto::SpellEntry spell = test::MakeSpell();
		spell.set_id(id);
		spell.set_baseid(id);
		spell.set_rank(1);
		return spell;
	}
}

TEST_CASE_METHOD(EvadeFixture, "An evading creature drops the auras its attackers left on it", "[creature_ai][evade]")
{
	// The attacker's damage-over-time effect, its own buff and a passive aura. Declared before the
	// creatures: an AuraContainer holds its SpellEntry by reference, and the bandit's destructor
	// still reads it while tearing down the auras that survive the evade.
	const proto::SpellEntry dot = MakeAuraSpell(600);
	const proto::SpellEntry selfBuff = MakeAuraSpell(601);
	proto::SpellEntry passive = MakeAuraSpell(602);
	passive.set_attributes(0, spell_attributes::Passive);

	auto bandit = Spawn(*banditEntry, Vector3(0.0f, 0.0f, 0.0f));
	auto intruder = Spawn(*intruderEntry, Vector3(0.0f, 0.0f, 5.0f));

	bandit->GetAI()->Idle();
	RunPosted();

	bandit->ApplyAura(std::make_shared<AuraContainer>(*bandit, intruder->GetGuid(), dot, 30000, 0));
	bandit->ApplyAura(std::make_shared<AuraContainer>(*bandit, bandit->GetGuid(), selfBuff, 30000, 0));
	bandit->ApplyAura(std::make_shared<AuraContainer>(*bandit, intruder->GetGuid(), passive, 0, 0));

	REQUIRE(bandit->HasAuraSpell(600));
	REQUIRE(bandit->HasAuraSpell(601));
	REQUIRE(bandit->HasAuraSpell(602));

	bandit->GetAI()->Reset();

	CHECK_FALSE(bandit->HasAuraSpell(600));
	CHECK(bandit->HasAuraSpell(601));
	CHECK(bandit->HasAuraSpell(602));

	RunPosted();
}
