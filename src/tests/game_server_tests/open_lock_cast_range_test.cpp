// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Spells aimed at a world object (gather nodes, chests, doors - the "Open" spell family) carry
// no unit target, so SingleCastState's range validation never looked at them: the five second
// gather cast started and finished from any distance. Using the object then runs into the
// interaction path, which silently refuses everything beyond the interaction distance, so the
// player saw the cast complete and nothing happen. A cast out of the spell's range must be
// refused with FailedOutOfRange, and - because both the client and the server present the node
// as gatherable up to the spell's range - the interaction distance has to cover that range, so
// that everything which may be cast can also be acted upon.

#include "catch.hpp"

#include "game_server/condition_mgr.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/objects/game_world_object_s.h"
#include "game/loot.h"
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
	constexpr uint32 GathererTemplate = 1;
	constexpr uint32 OpenRange = 1;
	constexpr uint32 OpenSpell = 1;
	constexpr uint32 HerbNodeEntry = 1;

	/// The "Open" spell family uses Melee Range (range type 6 in data/editor/data/ranges.data).
	/// This is the range both the client and the server validate a gather cast against, so it is
	/// also the distance at which the node must actually be gatherable.
	constexpr float OpenSpellRange = 7.0f;

	struct NullTriggerHandler final : ITriggerHandler
	{
		void ExecuteTrigger(const proto::TriggerEntry&, TriggerContext, uint32, bool) override {}
	};

	struct OpenLockRangeFixture
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

		proto::UnitEntry* gathererEntry = nullptr;
		proto::ObjectEntry* herbNode = nullptr;
		proto::SpellEntry* open = nullptr;

		OpenLockRangeFixture()
		{
			auto* factionTemplate = project.factionTemplates.add(GathererTemplate);
			factionTemplate->set_name("Test");
			factionTemplate->set_flags(0);
			factionTemplate->set_faction(1);

			gathererEntry = project.units.add(1);
			gathererEntry->set_name("Gatherer");
			gathererEntry->set_minlevel(3);
			gathererEntry->set_maxlevel(3);
			gathererEntry->set_factiontemplate(GathererTemplate);
			gathererEntry->set_malemodel(0);
			gathererEntry->set_femalemodel(0);
			gathererEntry->set_type(0);
			gathererEntry->set_family(0);
			gathererEntry->set_minlevelhealth(100);
			gathererEntry->set_maxlevelhealth(100);

			auto* range = project.ranges.add(OpenRange);
			range->set_range(OpenSpellRange);

			open = project.spells.add(OpenSpell);
			open->set_name("Open");
			open->set_rangetype(OpenRange);
			open->add_attributes(0);
			open->add_attributes(0);

			auto* effect = open->add_effects();
			effect->set_index(0);
			effect->set_type(spell_effects::OpenLock);
			effect->set_targeta(spell_effect_targets::ObjectTarget);

			herbNode = project.objects.add(HerbNodeEntry);
			herbNode->set_name("Silverleaf");
			herbNode->set_type(game_world_object_type::Chest);
			herbNode->set_flags(0);
			herbNode->add_data(0);	// data[0] - lock type

			// No map files exist for this directory: no nav mesh, no collision, LOS is unblocked.
			auto* map = project.maps.add(0);
			map->set_name("Open Lock Range Test");
			map->set_directory("Worlds/__open_lock_range_test__/__open_lock_range_test__");

			conditionMgr = std::make_unique<ConditionMgr>(project.conditions);
			manager = std::make_unique<WorldInstanceManager>(io, universe, project, objectIds, triggerHandler, *conditionMgr);
			world = &manager->CreateInstance(0);
		}

		std::shared_ptr<GameCreatureS> SpawnGatherer(const Vector3& position)
		{
			auto creature = world->CreateCreature(*gathererEntry, position, 0.0f, 0.0f);
			world->AddGameObject(*creature);
			return creature;
		}

		std::shared_ptr<GameWorldObjectS> SpawnNode(const Vector3& position)
		{
			auto object = world->SpawnWorldObject(*herbNode, position);
			world->AddGameObject(*object);
			return object;
		}

		SpellCastResult CastAt(GameUnitS& caster, const GameWorldObjectS& node) const
		{
			SpellTargetMap targetMap;
			targetMap.SetTargetMap(spell_cast_target_flags::Object);
			targetMap.SetObjectTarget(node.GetGuid());

			return caster.CastSpell(targetMap, *open, 0, false, 0, true);
		}

		~OpenLockRangeFixture()
		{
			io.stop();
		}
	};
}

TEST_CASE_METHOD(OpenLockRangeFixture, "The interaction distance covers the Open spell range", "[spell][open_lock][cast_result]")
{
	auto caster = SpawnGatherer(Vector3(0.0f, 0.0f, 0.0f));

	// The interaction / loot distance is what the interaction following a finished Open cast is
	// validated against. If it were shorter than the spell's range there would be a band in
	// which the node is presented as gatherable, the cast runs its full five seconds - and the
	// interaction is then silently refused.
	CHECK(caster->GetInteractionDistance() >= OpenSpellRange);
}

TEST_CASE_METHOD(OpenLockRangeFixture, "An Open cast aimed at a world object at the far end of the spell range is accepted", "[spell][open_lock][cast_result]")
{
	auto caster = SpawnGatherer(Vector3(0.0f, 0.0f, 0.0f));

	// Just inside the spell's range: this is the distance at which the gather cast used to run
	// its full five seconds and then quietly do nothing.
	constexpr float distance = OpenSpellRange - 0.25f;
	const auto node = SpawnNode(Vector3(distance, 0.0f, 0.0f));

	CHECK(CastAt(*caster, *node) == spell_cast_result::CastOkay);
	CHECK(caster->GetInteractionDistance() >= distance);
}

TEST_CASE_METHOD(OpenLockRangeFixture, "An Open cast aimed at a world object beyond the spell range is refused", "[spell][open_lock][cast_result]")
{
	auto caster = SpawnGatherer(Vector3(0.0f, 0.0f, 0.0f));
	const auto node = SpawnNode(Vector3(OpenSpellRange + 0.5f, 0.0f, 0.0f));

	CHECK(CastAt(*caster, *node) == spell_cast_result::FailedOutOfRange);
}

TEST_CASE_METHOD(OpenLockRangeFixture, "An Open cast aimed at a world object far beyond the spell range is refused", "[spell][open_lock][cast_result]")
{
	auto caster = SpawnGatherer(Vector3(0.0f, 0.0f, 0.0f));
	const auto node = SpawnNode(Vector3(30.0f, 0.0f, 0.0f));

	CHECK(CastAt(*caster, *node) == spell_cast_result::FailedOutOfRange);
}

TEST_CASE_METHOD(OpenLockRangeFixture, "An Open cast aimed at a world object in reach still starts", "[spell][open_lock][cast_result]")
{
	auto caster = SpawnGatherer(Vector3(0.0f, 0.0f, 0.0f));
	const auto node = SpawnNode(Vector3(2.0f, 0.0f, 0.0f));

	CHECK(CastAt(*caster, *node) == spell_cast_result::CastOkay);
}
