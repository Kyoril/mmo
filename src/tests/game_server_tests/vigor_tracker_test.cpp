// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Vigor is the traversal resource spent by gliding and dashing. The server keeps it in a
// VigorTracker that only settles on transitions (glide start/stop, drain changes, combat, landing)
// and between them predicts when it runs out or regenerates to full.

#include "test_unit_factory.h"

#include "game_server/objects/vigor_tracker.h"
#include "game_server/spells/aura_container.h"
#include "game/aura.h"
#include "game/spell.h"
#include "game/traversal.h"
#include "game/spell_target_map.h"
#include "game/movement_info.h"
#include "game_server/condition_mgr.h"
#include "game_server/trigger_handler.h"
#include "game_server/world/universe.h"
#include "game_server/world/world_instance.h"
#include "game_server/world/world_instance_manager.h"
#include "base/id_generator.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

using namespace mmo;
using namespace mmo::test;

namespace
{
	struct NullTriggerHandler final : ITriggerHandler
	{
		void ExecuteTrigger(const proto::TriggerEntry&, TriggerContext, uint32, bool) override {}
	};

	/// Adds a spell the player can learn to the project.
	void AddProjectSpell(proto::Project& project, const uint32 id, const char* name)
	{
		auto* spell = project.spells.add(id);
		spell->set_name(name);
		spell->add_attributes(0);
		spell->add_attributes(0);
	}

	/// The Dash spell as authored in the data: a speed buff that drains 12 Vigor per second and
	/// breaks on entering combat.
	proto::SpellEntry MakeDashSpell()
	{
		proto::SpellEntry spell = MakeSpell();
		spell.set_id(traversal_spells::Dash);
		spell.set_baseid(traversal_spells::Dash);
		spell.set_name("Dash");
		spell.set_attributes(0, spell_attributes::DisabledWhileActive | spell_attributes::NotInCombat | spell_attributes::Ability);
		spell.set_aurainterruptflags(spell_aura_interrupt_flags::EnterCombat);

		auto* speed = spell.add_effects();
		speed->set_index(0);
		speed->set_type(spell_effects::ApplyAura);
		speed->set_aura(aura_type::ModIncreaseSpeed);
		speed->set_basepoints(35);
		speed->set_targeta(spell_effect_targets::Caster);

		auto* drain = spell.add_effects();
		drain->set_index(1);
		drain->set_type(spell_effects::ApplyAura);
		drain->set_aura(aura_type::DrainVigor);
		drain->set_basepoints(12);
		drain->set_targeta(spell_effect_targets::Caster);

		return spell;
	}
}

TEST_CASE("Tracker drains while gliding and reports empty time", "[vigor]")
{
	VigorTracker tracker;
	tracker.Reset(0, 100.0f);

	tracker.SetGrounded(0, false);
	tracker.SetGliding(0, true, false);

	// The start cost is paid right away.
	CHECK(tracker.GetCurrent(0) == Approx(95.0f));

	// 95 Vigor at 8 per second.
	CHECK(tracker.GetEmptyTime(0) == 11875);

	CHECK(tracker.GetCurrent(2000) == Approx(79.0f));
	CHECK(tracker.GetCurrent(20000) == Approx(0.0f));

	// Changing into an updraft costs nothing extra and halves the drain.
	tracker.SetGliding(2000, true, true);
	CHECK(tracker.GetCurrent(2000) == Approx(79.0f));
	CHECK(tracker.GetCurrent(4000) == Approx(71.0f));

	// Not draining: no empty time.
	tracker.SetGliding(4000, false, false);
	CHECK(tracker.GetEmptyTime(4000) == 0);
}

TEST_CASE("Tracker regen waits for delay and grounded", "[vigor]")
{
	VigorTracker tracker;
	tracker.Reset(0, 100.0f);

	tracker.SetGrounded(0, false);
	tracker.SetGliding(0, true, false);
	tracker.SetGliding(1000, false, false);
	REQUIRE(tracker.GetCurrent(1000) == Approx(87.0f));

	// Still in the air after closing the glider: no regen at all.
	CHECK(tracker.GetCurrent(5000) == Approx(87.0f));
	CHECK(tracker.GetFullTime(1000) == 0);

	// Landing right after the glide: the regen delay of one second applies first.
	tracker.SetGrounded(1000, true);
	CHECK(tracker.GetCurrent(1500) == Approx(87.0f));
	CHECK(tracker.GetCurrent(2000) == Approx(87.0f));
	CHECK(tracker.GetCurrent(2400) == Approx(97.0f));
	CHECK(tracker.GetCurrent(3000) == Approx(100.0f));

	// 13 missing Vigor at 25 per second after the delay: 1 s + 0.52 s.
	CHECK(tracker.GetFullTime(1000) == 2520);
	CHECK(tracker.GetFullTime(3000) == 0);
}

TEST_CASE("Extra drain from aura stacks with glide", "[vigor]")
{
	VigorTracker tracker;
	tracker.Reset(0, 100.0f);

	tracker.SetGrounded(0, false);
	tracker.SetGliding(0, true, false);
	tracker.SetExtraDrain(0, 12.0f);

	// 8 glide + 12 extra.
	CHECK(tracker.GetCurrent(1000) == Approx(75.0f));
	CHECK(tracker.GetEmptyTime(0) == 4750);

	// The extra drain alone keeps draining after the glide stops.
	tracker.SetGliding(1000, false, false);
	CHECK(tracker.GetCurrent(2000) == Approx(63.0f));
	CHECK(tracker.GetEmptyTime(2000) == 2000 + 5250);

	tracker.SetExtraDrain(2000, 0.0f);
	CHECK(tracker.GetEmptyTime(2000) == 0);
	CHECK(tracker.GetCurrent(2000) == Approx(63.0f));
}

TEST_CASE("Combat stops regen", "[vigor]")
{
	VigorTracker tracker;
	tracker.Reset(0, 100.0f);

	tracker.SetExtraDrain(0, 50.0f);
	tracker.SetExtraDrain(1000, 0.0f);
	REQUIRE(tracker.GetCurrent(1000) == Approx(50.0f));

	tracker.SetInCombat(1000, true);
	CHECK(tracker.GetCurrent(5000) == Approx(50.0f));
	CHECK(tracker.GetFullTime(5000) == 0);

	// The delay has long passed when combat ends, so regen starts at once.
	tracker.SetInCombat(5000, false);
	CHECK(tracker.GetCurrent(5000) == Approx(50.0f));
	CHECK(tracker.GetCurrent(6000) == Approx(75.0f));
}

TEST_CASE("Max vigor is zero without traversal spells", "[vigor]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	AddProjectSpell(project, traversal_spells::Glider, "Glider");
	AddProjectSpell(project, traversal_spells::Dash, "Dash");

	auto player = MakeUnit(project, timers);
	player->Set<uint64>(object_fields::Guid, 1);
	CHECK(player->Get<float>(object_fields::MaxVigor) == Approx(0.0f));
	CHECK_FALSE(player->CanUseTraversal(traversal_spells::Glider));

	// Knowing the glider spell alone does nothing: a glider has to be equipped (glider_gate_test).
	player->AddSpell(traversal_spells::Glider);
	CHECK(player->Get<float>(object_fields::MaxVigor) == Approx(0.0f));
	CHECK_FALSE(player->CanUseTraversal(traversal_spells::Glider));

	// Dash grants Vigor on its own.
	player->AddSpell(traversal_spells::Dash);
	CHECK(player->Get<float>(object_fields::MaxVigor) == Approx(100.0f));
	CHECK(player->Get<float>(object_fields::Vigor) == Approx(100.0f));
	CHECK(player->CanUseTraversal(traversal_spells::Dash));
	CHECK_FALSE(player->CanUseTraversal(traversal_spells::Glider));

	player->RemoveSpell(traversal_spells::Dash);
	CHECK(player->Get<float>(object_fields::MaxVigor) == Approx(0.0f));
	CHECK(player->Get<float>(object_fields::Vigor) == Approx(0.0f));
}

TEST_CASE("Entering combat removes Dash", "[vigor]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	AddProjectSpell(project, traversal_spells::Dash, "Dash");

	// Declared before the player: the aura container holds the spell by reference, and the
	// player's destructor still reads it.
	const proto::SpellEntry dash = MakeDashSpell();

	auto player = MakeUnit(project, timers);
	player->Set<uint64>(object_fields::Guid, 1);
	player->AddSpell(traversal_spells::Dash);

	auto aura = std::make_shared<AuraContainer>(*player, player->GetGuid(), dash, 0, 0);
	for (const auto& effect : dash.effects())
	{
		aura->AddAuraEffect(effect, effect.basepoints());
	}
	player->ApplyAura(std::move(aura));

	REQUIRE(player->HasAuraSpell(traversal_spells::Dash));
	CHECK(player->GetVigor().GetExtraDrain() == Approx(12.0f));

	player->SetInCombat(true, false);

	CHECK_FALSE(player->HasAuraSpell(traversal_spells::Dash));
	CHECK(player->GetVigor().GetExtraDrain() == Approx(0.0f));
	CHECK(player->GetVigor().IsInCombat());
}

TEST_CASE("Running out of Vigor removes the drain auras and signals exhaustion", "[vigor]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	AddProjectSpell(project, traversal_spells::Dash, "Dash");

	proto::SpellEntry dash = MakeDashSpell();

	// A fast drain so the test does not wait long: 100 Vigor at 1000 per second.
	dash.mutable_effects(1)->set_basepoints(1000);

	auto player = MakeUnit(project, timers);
	player->Set<uint64>(object_fields::Guid, 1);
	player->AddSpell(traversal_spells::Dash);

	int exhaustedCount = 0;
	scoped_connection onExhausted{ player->GetVigor().exhausted.connect([&exhaustedCount]() { ++exhaustedCount; }) };

	auto aura = std::make_shared<AuraContainer>(*player, player->GetGuid(), dash, 0, 0);
	for (const auto& effect : dash.effects())
	{
		aura->AddAuraEffect(effect, effect.basepoints());
	}
	player->ApplyAura(std::move(aura));
	REQUIRE(player->HasAuraSpell(traversal_spells::Dash));

	const GameTime deadline = GetAsyncTimeMs() + 3000;
	while (player->HasAuraSpell(traversal_spells::Dash) && GetAsyncTimeMs() < deadline)
	{
		io.run_one_for(std::chrono::milliseconds(50));
	}

	CHECK_FALSE(player->HasAuraSpell(traversal_spells::Dash));
	CHECK(exhaustedCount == 1);
	CHECK(player->Get<float>(object_fields::Vigor) == Approx(0.0f));
}

TEST_CASE("Dying in combat lets Vigor regenerate again", "[vigor]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	AddProjectSpell(project, traversal_spells::Dash, "Dash");

	auto player = MakeUnit(project, timers);
	player->Set<uint64>(object_fields::Guid, 1);
	player->AddSpell(traversal_spells::Dash);

	// Spend some Vigor so that there is something to regenerate.
	player->ModifyVigorDrain(1000, true);
	const GameTime drainUntil = GetAsyncTimeMs() + 20;
	while (GetAsyncTimeMs() < drainUntil)
	{
	}
	player->ModifyVigorDrain(1000, false);
	REQUIRE(player->GetVigor().GetCurrent(GetAsyncTimeMs()) < 100.0f);

	player->SetInCombat(true, false);
	REQUIRE(player->GetVigor().IsInCombat());
	CHECK(player->GetVigor().GetFullTime(GetAsyncTimeMs()) == 0);

	// Death clears the combat flag without SetInCombat.
	player->Kill(nullptr);
	CHECK_FALSE(player->IsInCombat());
	CHECK_FALSE(player->GetVigor().IsInCombat());

	// Regeneration is scheduled again, after the regen delay.
	const GameTime now = GetAsyncTimeMs();
	const GameTime fullTime = player->GetVigor().GetFullTime(now);
	CHECK(fullTime != 0);
	CHECK(player->GetVigor().GetCurrent(now + 5000) == Approx(100.0f));
}

TEST_CASE("A Vigor drain spell cannot be cast without Vigor", "[vigor]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	Universe universe{ io, timers };
	IdGenerator<uint64> objectIds{ 1 };
	NullTriggerHandler triggerHandler;

	// The project's Dash is the spell being cast, so it lives as long as the player.
	auto* dash = project.spells.add(traversal_spells::Dash);
	*dash = MakeDashSpell();

	// Casting needs a world. No map files exist for this directory: no nav mesh, no collision.
	auto* map = project.maps.add(0);
	map->set_name("Vigor Test");
	map->set_directory("Worlds/__vigor_test__/__vigor_test__");
	ConditionMgr conditionMgr{ project.conditions };
	WorldInstanceManager manager{ io, universe, project, objectIds, triggerHandler, conditionMgr };
	WorldInstance& world = manager.CreateInstance(0);

	auto player = MakeUnit(project, timers);
	player->Set<uint64>(object_fields::Guid, 1);
	player->Set<uint32>(object_fields::Health, 100);
	player->AddSpell(traversal_spells::Dash);
	world.AddGameObject(*player);
	REQUIRE(player->Get<float>(object_fields::Vigor) == Approx(100.0f));

	// Empty the pool with a fast drain that is not an aura, then stop it.
	player->ModifyVigorDrain(100000, true);
	const GameTime drainUntil = GetAsyncTimeMs() + 10;
	while (GetAsyncTimeMs() < drainUntil)
	{
	}
	player->ModifyVigorDrain(100000, false);
	REQUIRE(player->GetVigor().IsEmpty(GetAsyncTimeMs()));

	SpellTargetMap targetMap;
	targetMap.SetTargetMap(spell_cast_target_flags::Self);
	CHECK(player->CastSpell(targetMap, *dash, 0) == spell_cast_result::FailedNotReady);
	CHECK_FALSE(player->HasAuraSpell(traversal_spells::Dash));

	// With Vigor the same cast is not refused for it.
	player->GetVigor().Reset(GetAsyncTimeMs(), 100.0f);
	CHECK(player->CastSpell(targetMap, *dash, 0) == spell_cast_result::CastOkay);
	CHECK(player->HasAuraSpell(traversal_spells::Dash));

	world.RemoveGameObject(*player);
	io.stop();
}

TEST_CASE("A gliding player cannot cast", "[vigor][glide]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	Universe universe{ io, timers };
	IdGenerator<uint64> objectIds{ 1 };
	NullTriggerHandler triggerHandler;

	auto* dash = project.spells.add(traversal_spells::Dash);
	*dash = MakeDashSpell();

	// A plain instant spell on the caster, cast by a trigger action (server-authoritative).
	auto* scripted = project.spells.add(9001);
	*scripted = MakeSpell();
	scripted->set_id(9001);
	scripted->set_name("Scripted");

	auto* map = project.maps.add(0);
	map->set_name("Vigor Test");
	map->set_directory("Worlds/__vigor_test__/__vigor_test__");
	ConditionMgr conditionMgr{ project.conditions };
	WorldInstanceManager manager{ io, universe, project, objectIds, triggerHandler, conditionMgr };
	WorldInstance& world = manager.CreateInstance(0);

	auto player = MakeUnit(project, timers);
	player->Set<uint64>(object_fields::Guid, 1);
	player->Set<uint32>(object_fields::Health, 100);
	player->AddSpell(traversal_spells::Dash);
	world.AddGameObject(*player);

	MovementInfo gliding;
	gliding.movementFlags = movement_flags::Falling | movement_flags::Gliding;
	gliding.position = Vector3(0.0f, 50.0f, 0.0f);
	player->ApplyMovementInfo(gliding);

	SpellTargetMap targetMap;
	targetMap.SetTargetMap(spell_cast_target_flags::Self);
	CHECK(player->CastSpell(targetMap, *dash, 0) == spell_cast_result::FailedMoving);
	CHECK_FALSE(player->HasAuraSpell(traversal_spells::Dash));

	// Back on the ground the same cast goes off.
	MovementInfo grounded;
	grounded.position = Vector3(0.0f, 0.0f, 0.0f);
	player->ApplyMovementInfo(grounded);
	CHECK(player->CastSpell(targetMap, *dash, 0) == spell_cast_result::CastOkay);
	CHECK(player->HasAuraSpell(traversal_spells::Dash));

	// Scripted casts are not the glider's own action: they still go off while gliding.
	// (The Dash cast started the global cooldown, which is not what this checks.)
	player->ResetCooldowns();
	player->ApplyMovementInfo(gliding);
	CHECK(player->CastSpell(targetMap, *scripted, 0, false, 0, true) == spell_cast_result::CastOkay);

	world.RemoveGameObject(*player);
	io.stop();
}
