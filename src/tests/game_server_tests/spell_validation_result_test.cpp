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
#include "game_server/spells/spell_cast.h"
#include "game_server/player_duel.h"
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

TEST_CASE_METHOD(CastResultFixture, "A duel ending on a channel tick survives a replacement cast from its end callback", "[duel][channel]")
{
	victimEntry->set_factiontemplate(CasterTemplate);
	auto caster = Spawn(*casterEntry, Vector3::Zero);
	auto victim = Spawn(*victimEntry, Vector3(5.0f, 0.0f, 0.0f));
	victim->Set<uint32>(object_fields::FactionTemplate, CasterTemplate);
	for (const auto& unit : { caster, victim })
	{
		unit->Set<uint32>(object_fields::MaxHealth, 1000);
		unit->Set<uint32>(object_fields::Health, 1000);
	}

	auto* channel = project.spells.add(100);
	channel->set_name("Duel channel regression");
	channel->set_rangetype(ShortRange);
	channel->set_duration(500);
	channel->add_attributes(spell_attributes::Channeled);
	channel->add_attributes(0);
	auto* periodic = channel->add_effects();
	periodic->set_type(spell_effects::ApplyAura);
	periodic->set_aura(aura_type::PeriodicDamage);
	periodic->set_targeta(spell_effect_targets::TargetEnemy);
	periodic->set_amplitude(10);
	periodic->set_basepoints(5000);

	auto* replacement = project.spells.add(101);
	replacement->add_attributes(0);
	replacement->add_attributes(0);
	replacement->set_cooldownflags(spell_cooldown_flags::NoGlobalCooldown);

	unsigned outcomes = 0, ended = 0;
	auto duel = std::make_shared<PlayerDuel>(caster, victim,
		[&](uint64, DuelEvent event, uint8, const String&, const Vector3&)
		{
			if (event == DuelEvent::Finished)
			{
				++outcomes;
			}
		}, 1000);
	duel->Start();
	duel->Accept(victim->GetGuid(), 1000);
	duel->Update(4000);
	REQUIRE(caster->IsDuelActive());

	SpellCastResult replacementResult = spell_cast_result::FailedError;
	SpellTargetMap self;
	self.SetTargetMap(spell_cast_target_flags::Self);
	scoped_connection onEnd{ caster->finishedCasting.connect([&](bool)
	{
		if (++ended == 1)
		{
			// Deliberately replace the active state while channel cancellation is notifying
			// listeners, rather than letting the SpellCast keep its last reference alive.
			replacementResult = caster->CastSpell(self, *replacement, 5000, false, 0, true);
		}
	}) };
	SpellTargetMap target;
	target.SetTargetMap(spell_cast_target_flags::Unit);
	target.SetUnitTarget(victim->GetGuid());
	REQUIRE(caster->CastSpell(target, *channel, 500, false, 0, true) == spell_cast_result::CastOkay);
	REQUIRE(victim->HasAuraSpell(channel->id()));
	CHECK_FALSE(duel->IsFinished());
	io.run_for(std::chrono::milliseconds(100));
	CHECK(duel->IsFinished());
	CHECK(victim->IsAlive());
	CHECK(victim->GetHealth() == 10);
	CHECK_FALSE(victim->HasAuraSpell(channel->id()));
	CHECK(outcomes == 2);
	CHECK(ended == 1);
	CHECK(replacementResult == spell_cast_result::CastOkay);
	CHECK(caster->CastSpell(self, *replacement, 0, false, 0, true) == spell_cast_result::FailedSpellInProgress);
	onEnd.disconnect();
	caster->CancelCast(spell_interrupt_flags::Any);
}

TEST_CASE_METHOD(CastResultFixture, "Cancelling a single cast preserves a replacement started by its end listener", "[spell][cast_lifetime]")
{
	auto caster = Spawn(*casterEntry, Vector3::Zero);
	auto* replacement = project.spells.add(101);
	replacement->add_attributes(0);
	replacement->add_attributes(0);
	replacement->set_cooldownflags(spell_cooldown_flags::NoGlobalCooldown);
	SpellTargetMap self;
	self.SetTargetMap(spell_cast_target_flags::Self);
	REQUIRE(caster->CastSpell(self, *bolt, 5000, false, 0, true) == spell_cast_result::CastOkay);
	unsigned ended = 0;
	SpellCastResult replacementResult = spell_cast_result::FailedError;
	scoped_connection onEnd{ caster->finishedCasting.connect([&](bool)
	{
		if (++ended == 1)
		{
			replacementResult = caster->CastSpell(self, *replacement, 5000, false, 0, true);
		}
	}) };
	caster->CancelCast(spell_interrupt_flags::Any);
	CHECK(replacementResult == spell_cast_result::CastOkay);
	CHECK(ended == 1);
	CHECK(caster->CastSpell(self, *replacement, 0, false, 0, true) == spell_cast_result::FailedSpellInProgress);
	onEnd.disconnect();
	caster->CancelCast(spell_interrupt_flags::Any);
}

TEST_CASE_METHOD(CastResultFixture, "GM cooldown bypass allows repeated casts and disabling restores cooldowns", "[spell][gm_cheats]")
{
	auto caster = Spawn(*casterEntry, Vector3::Zero);
	auto victim = Spawn(*victimEntry, Vector3(5.0f, 0.0f, 0.0f));
	bolt->set_cooldown(60000);
	bolt->set_category(7);
	bolt->set_categorycooldown(60000);
	SpellTargetMap targetMap;
	targetMap.SetTargetMap(spell_cast_target_flags::Unit);
	targetMap.SetUnitTarget(victim->GetGuid());
	REQUIRE(caster->CastSpell(targetMap, *bolt, 0, false, 0, true) == spell_cast_result::CastOkay);
	REQUIRE(caster->CastSpell(targetMap, *bolt, 0, false, 0, true) == spell_cast_result::FailedNotReady);
	caster->SetNoCooldown(true);
	for (uint32 i = 0; i < 5; ++i)
	{
		CHECK(caster->CastSpell(targetMap, *bolt, 0, false, 0, true) == spell_cast_result::CastOkay);
		CHECK_FALSE(caster->SpellHasCooldown(BoltSpell, 7, 0));
	}
	caster->SetNoCooldown(false);
	CHECK(caster->CastSpell(targetMap, *bolt, 0, false, 0, true) == spell_cast_result::CastOkay);
	CHECK(caster->CastSpell(targetMap, *bolt, 0, false, 0, true) == spell_cast_result::FailedNotReady);
}

TEST_CASE_METHOD(CastResultFixture, "Godmode casts mana spells for free even with no mana and disabling restores cost", "[spell][gm_cheats]")
{
	auto caster = Spawn(*casterEntry, Vector3::Zero);
	auto victim = Spawn(*victimEntry, Vector3(5.0f, 0.0f, 0.0f));
	bolt->set_cost(35);
	bolt->set_powertype(power_type::Mana);
	bolt->set_cooldownflags(spell_cooldown_flags::NoGlobalCooldown);
	SpellTargetMap targetMap;
	targetMap.SetTargetMap(spell_cast_target_flags::Unit);
	targetMap.SetUnitTarget(victim->GetGuid());
	caster->Set<uint32>(object_fields::Mana, 0);
	caster->SetGodmode(true);
	CHECK((caster->Get<uint32>(object_fields::Flags) & unit_flags::Godmode) != 0);
	for (uint32 i = 0; i < 5; ++i)
	{
		CHECK(caster->CastSpell(targetMap, *bolt, 0, false, 0, true) == spell_cast_result::CastOkay);
		CHECK(caster->Get<uint32>(object_fields::Mana) == 0);
	}
	caster->SetGodmode(false);
	CHECK((caster->Get<uint32>(object_fields::Flags) & unit_flags::Godmode) == 0);
	caster->Set<uint32>(object_fields::Mana, 100);
	CHECK(caster->CastSpell(targetMap, *bolt, 0, false, 0, true) == spell_cast_result::CastOkay);
	CHECK(caster->Get<uint32>(object_fields::Mana) == 65);
}

TEST_CASE_METHOD(CastResultFixture, "Godmode leaves non-mana spell costs unchanged", "[spell][gm_cheats]")
{
	auto caster = Spawn(*casterEntry, Vector3::Zero);
	SpellCast cast(timers, *caster);
	caster->SetGodmode(true);
	bolt->set_cost(35);
	bolt->set_powertype(power_type::Energy);
	CHECK(cast.CalculatePowerCost(*bolt) == 35);
	bolt->set_powertype(power_type::Health);
	CHECK(cast.CalculatePowerCost(*bolt) == 35);
}

TEST_CASE_METHOD(CastResultFixture, "Cooldown bypass preserves cast times and does not allow overlapping casts", "[spell][gm_cheats]")
{
	auto caster = Spawn(*casterEntry, Vector3::Zero);
	auto victim = Spawn(*victimEntry, Vector3(5.0f, 0.0f, 0.0f));
	bolt->set_cooldown(60000);
	bolt->set_cooldownflags(spell_cooldown_flags::StartOnCastStart);
	SpellTargetMap targetMap;
	targetMap.SetTargetMap(spell_cast_target_flags::Unit);
	targetMap.SetUnitTarget(victim->GetGuid());
	caster->SetNoCooldown(true);
	REQUIRE(caster->CastSpell(targetMap, *bolt, 1500, false, 0, true) == spell_cast_result::CastOkay);
	CHECK_FALSE(caster->SpellHasCooldown(BoltSpell, 0, 0));
	CHECK(caster->CastSpell(targetMap, *bolt, 1500, false, 0, true) == spell_cast_result::FailedSpellInProgress);
	caster->CancelCast(spell_interrupt_flags::Any);
}
