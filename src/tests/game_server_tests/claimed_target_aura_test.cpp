// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// A quest item that makes a creature follow whoever used it hands that creature - and the quest
// progress it carries - to one player. Without a mark on the creature, a second use took it over:
// another player offered the same item to a sow already following someone and simply walked off
// with her ("Pigs in the Corn"). A PreventAuraApplication aura marks the creature as claimed, and
// every aura-applying cast another unit aims at it fails with "Invalid target" while it lasts.

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
#include "game/aura.h"
#include "game/spell.h"
#include "game/spell_target_map.h"
#include "shared/proto_data/project.h"
#include "asio/io_service.hpp"

#include <memory>

using namespace mmo;

namespace
{
	constexpr uint32 LivestockTemplate = 1;
	constexpr uint32 MarkSpell = 1;
	constexpr uint32 SecondMarkSpell = 2;
	constexpr uint32 PlainSpell = 3;

	struct ClaimTriggerHandler final : ITriggerHandler
	{
		void ExecuteTrigger(const proto::TriggerEntry&, TriggerContext, uint32, bool) override {}
	};

	/// Two farmhands and a sow, all of one faction that is nobody's enemy, and the spells a quest
	/// item could carry: two that mark their target, one that does not touch auras at all.
	struct ClaimedTargetFixture
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;
		Universe universe{ io, timers };
		IdGenerator<uint64> objectIds{ 1 };
		ClaimTriggerHandler triggerHandler;
		std::unique_ptr<ConditionMgr> conditionMgr;
		std::unique_ptr<WorldInstanceManager> manager;
		WorldInstance* world = nullptr;

		proto::UnitEntry* farmhandEntry = nullptr;
		proto::UnitEntry* sowEntry = nullptr;
		proto::SpellEntry* mark = nullptr;
		proto::SpellEntry* secondMark = nullptr;
		proto::SpellEntry* plain = nullptr;

		ClaimedTargetFixture()
		{
			auto* factionTemplate = project.factionTemplates.add(LivestockTemplate);
			factionTemplate->set_name("Livestock, Friendly");
			factionTemplate->set_flags(0);
			factionTemplate->set_faction(1);

			farmhandEntry = AddUnitEntry(1, "Farmhand");
			sowEntry = AddUnitEntry(2, "Runaway Sow");

			mark = AddMarkSpell(MarkSpell, "Offer Turnip");

			// The same unit casting twice in a row would be stopped by the global cooldown long
			// before the claim is looked at, so the second offer ignores it.
			secondMark = AddMarkSpell(SecondMarkSpell, "Offer Another Turnip");
			secondMark->set_cooldownflags(spell_cooldown_flags::NoGlobalCooldown);

			plain = AddSpell(PlainSpell, "Whistle");
			auto* plainEffect = plain->add_effects();
			plainEffect->set_index(0);
			plainEffect->set_type(spell_effects::Dummy);
			plainEffect->set_targeta(spell_effect_targets::TargetAny);

			// No map files exist for this directory: no nav mesh, no collision, LOS is unblocked.
			auto* map = project.maps.add(0);
			map->set_name("Claimed Target Test");
			map->set_directory("Worlds/__claimed_target_test__/__claimed_target_test__");

			conditionMgr = std::make_unique<ConditionMgr>(project.conditions);
			manager = std::make_unique<WorldInstanceManager>(io, universe, project, objectIds, triggerHandler, *conditionMgr);
			world = &manager->CreateInstance(0);
		}

		proto::UnitEntry* AddUnitEntry(const uint32 id, const char* name)
		{
			auto* entry = project.units.add(id);
			entry->set_name(name);
			entry->set_minlevel(5);
			entry->set_maxlevel(5);
			entry->set_factiontemplate(LivestockTemplate);
			entry->set_malemodel(0);
			entry->set_femalemodel(0);
			entry->set_type(0);
			entry->set_family(0);
			entry->set_minlevelhealth(100);
			entry->set_maxlevelhealth(100);
			return entry;
		}

		proto::SpellEntry* AddMarkSpell(const uint32 id, const char* name)
		{
			proto::SpellEntry* spell = AddSpell(id, name);
			auto* effect = spell->add_effects();
			effect->set_index(0);
			effect->set_type(spell_effects::ApplyAura);
			effect->set_aura(aura_type::PreventAuraApplication);
			effect->set_targeta(spell_effect_targets::TargetAny);
			spell->set_duration(300000);
			return spell;
		}

		proto::SpellEntry* AddSpell(const uint32 id, const char* name)
		{
			auto* spell = project.spells.add(id);
			spell->set_name(name);
			spell->add_attributes(0);
			spell->add_attributes(0);
			return spell;
		}

		std::shared_ptr<GameCreatureS> Spawn(const proto::UnitEntry& entry, const Vector3& position)
		{
			auto creature = world->CreateCreature(entry, position, 0.0f, 0.0f);
			world->AddGameObject(*creature);
			return creature;
		}

		SpellCastResult Cast(GameCreatureS& caster, const GameCreatureS& target, const proto::SpellEntry& spell)
		{
			SpellTargetMap targetMap;
			targetMap.SetTargetMap(spell_cast_target_flags::Unit);
			targetMap.SetUnitTarget(target.GetGuid());
			return caster.CastSpell(targetMap, spell, 0, false, 0, true);
		}

		~ClaimedTargetFixture()
		{
			io.stop();
		}
	};
}

TEST_CASE_METHOD(ClaimedTargetFixture, "The first aura application claims the target", "[spell][claimed_target]")
{
	auto farmhand = Spawn(*farmhandEntry, Vector3(0.0f, 0.0f, 0.0f));
	auto sow = Spawn(*sowEntry, Vector3(2.0f, 0.0f, 0.0f));

	CHECK(Cast(*farmhand, *sow, *mark) == spell_cast_result::CastOkay);
	CHECK(sow->HasAuraSpell(MarkSpell));
}

TEST_CASE_METHOD(ClaimedTargetFixture, "A second aura application at a claimed target fails", "[spell][claimed_target]")
{
	auto farmhand = Spawn(*farmhandEntry, Vector3(0.0f, 0.0f, 0.0f));
	auto otherFarmhand = Spawn(*farmhandEntry, Vector3(-2.0f, 0.0f, 0.0f));
	auto sow = Spawn(*sowEntry, Vector3(2.0f, 0.0f, 0.0f));

	REQUIRE(Cast(*farmhand, *sow, *mark) == spell_cast_result::CastOkay);

	// Another player offering the same item takes the sow over no more.
	CHECK(Cast(*otherFarmhand, *sow, *mark) == spell_cast_result::FailedBadTargets);

	// Neither does the one who claimed her: the mark refuses every aura, not just a repeat of
	// the spell that set it.
	CHECK(Cast(*farmhand, *sow, *secondMark) == spell_cast_result::FailedBadTargets);

	// The claim still belongs to the one who made it.
	CHECK(sow->HasAuraSpellFromCaster(MarkSpell, farmhand->GetGuid()));
}

TEST_CASE_METHOD(ClaimedTargetFixture, "A claimed target still takes spells that apply no aura", "[spell][claimed_target]")
{
	auto farmhand = Spawn(*farmhandEntry, Vector3(0.0f, 0.0f, 0.0f));
	auto otherFarmhand = Spawn(*farmhandEntry, Vector3(-2.0f, 0.0f, 0.0f));
	auto sow = Spawn(*sowEntry, Vector3(2.0f, 0.0f, 0.0f));

	REQUIRE(Cast(*farmhand, *sow, *mark) == spell_cast_result::CastOkay);

	CHECK(Cast(*otherFarmhand, *sow, *plain) == spell_cast_result::CastOkay);
}

TEST_CASE_METHOD(ClaimedTargetFixture, "A follower stops following and goes home when the claim ends", "[spell][claimed_target]")
{
	auto farmhand = Spawn(*farmhandEntry, Vector3(0.0f, 0.0f, 0.0f));
	auto sow = Spawn(*sowEntry, Vector3(2.0f, 0.0f, 0.0f));

	REQUIRE(sow->GetAI() != nullptr);
	const Vector3 home = sow->GetAI()->GetHome().position;

	REQUIRE(Cast(*farmhand, *sow, *mark) == spell_cast_result::CastOkay);

	// What the quest item's OnSpellHit trigger does in the live game.
	sow->SetFollowedUnit(std::static_pointer_cast<GameUnitS>(farmhand), 2.5f);
	REQUIRE(sow->GetFollowedUnit().get() == farmhand.get());

	// Led a good way from her spawn before the claim ends.
	sow->GetMover().Teleport(Vector3(40.0f, 0.0f, 40.0f), Radian(0.0f));

	// The same removal path a run-out claim takes (AuraContainer::SetApplied(false)); the real
	// expiry waits out the spell's duration, which a unit test cannot sit through.
	REQUIRE(sow->RemoveAuraBySpellId(MarkSpell, farmhand->GetGuid()));
	io.poll();

	CHECK_FALSE(sow->GetFollowedUnit());

	// She heads back to where she was spawned - either already teleported there, because the
	// test world has no navigation mesh to walk on, or walking towards it.
	const bool atHome = (sow->GetPosition() - home).GetSquaredLength() < 0.25f;
	const bool walkingHome = (sow->GetMover().GetTarget() - home).GetSquaredLength() < 0.25f;
	CHECK((atHome || walkingHome));
}
