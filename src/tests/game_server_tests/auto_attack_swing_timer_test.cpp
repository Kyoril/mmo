// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// The auto-attack swing countdown has exactly one owner. That is not obvious from reading the code,
// because Countdown clears m_running *before* raising `ended`: every guard of the form "re-arm if
// the timer is not running" passes when called from inside a swing, and Countdown::Cancel does
// nothing there for the same reason. That combination produced two bugs -- a swing arming the
// countdown twice, and a killing blow's StopAttack being undone by an unconditional re-arm.
//
// What is covered here is the victim guard on the cast path, which is reachable without a world.
//
// SCAFFOLDING LIMIT: driving an actual swing needs more fixture than this suite has ever built.
// StartAttack requires distinct guids and hostile factions (both cheap), but then reaches
// ShouldDualWield -> GamePlayerS::HasOffhandWeapon -> m_inventory, which needs an inventory
// repository, and the swing itself needs a WorldInstance. No test in game_server_tests constructs
// either -- cc_movement_test and cc_ai_test deliberately test the *no*-WorldInstance paths. So the
// "one swing arms the countdown once" and "casting resets the swing timer" invariants are covered
// at integration level by e2e/scenarios/crypt_phase_transitions.lua and crypt_wing_bosses.lua, and
// the primitive underneath by src/tests/base_tests/test_countdown.cpp. Building a combat fixture
// here would let those move down to unit level.

#include "catch.hpp"

#include "game_server/objects/game_player_s.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/classes.pb.h"
#include "shared/proto_data/spells.pb.h"

#include "asio/io_service.hpp"

#include <memory>

using namespace mmo;

namespace
{
	struct SwingTimerFixture
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;

		SwingTimerFixture()
		{
			auto* cls = project.classes.getById(1);
			if (!cls)
			{
				cls = project.classes.add(1);
				cls->set_powertype(proto::ClassEntry_PowerType_MANA);
				for (int i = 0; i < 2; ++i)
				{
					auto* lbv = cls->add_levelbasevalues();
					lbv->set_health(100); lbv->set_mana(100);
					lbv->set_stamina(10); lbv->set_strength(10);
					lbv->set_agility(10); lbv->set_intellect(10);
					lbv->set_spirit(10);
				}
			}
		}

		std::shared_ptr<GamePlayerS> MakeUnit()
		{
			auto unit = std::make_shared<GamePlayerS>(project, timers);
			unit->Initialize();
			if (auto* cls = project.classes.getById(1))
			{
				unit->SetClass(*cls);
			}
			unit->SetLevel(1);
			return unit;
		}
	};

	/// A spell with no effects at all -- enough to drive a cast to completion without any of the
	/// damage, aura or targeting machinery running.
	proto::SpellEntry MakeInertSpell(const uint32 id)
	{
		proto::SpellEntry spell;
		spell.add_attributes(0);
		spell.add_attributes(0);
		spell.set_id(id);
		spell.set_baseid(id);
		spell.set_rank(1);
		return spell;
	}
}

TEST_CASE_METHOD(SwingTimerFixture, "A finished cast does not arm a swing when there is no victim", "[swing_timer]")
{
	// OnSpellCastEnded re-arms the swing timer so auto-attack resumes after a cast, and that re-arm
	// is guarded on still having a victim. Without the guard a cast finishing after the target died
	// -- StopAttack having just cancelled the countdown and cleared the victim -- would resurrect a
	// swing timer on a unit that has left combat, and it would then keep swinging at nothing.
	const auto attacker = MakeUnit();

	REQUIRE_FALSE(attacker->IsAttackSwingArmed());

	const proto::SpellEntry spell = MakeInertSpell(9001);
	SpellTargetMap targets;
	targets.SetTargetMap(spell_cast_target_flags::Self);
	attacker->CastSpell(targets, spell, 0, true);

	CHECK_FALSE(attacker->IsAttackSwingArmed());
	CHECK(attacker->GetVictim() == nullptr);
}

TEST_CASE_METHOD(SwingTimerFixture, "A unit with no victim reports no scheduled swing", "[swing_timer]")
{
	// Baseline for the accessor the test above leans on: it must report "nothing scheduled" for a
	// unit that has never attacked, or that assertion would pass for the wrong reason.
	const auto attacker = MakeUnit();

	CHECK_FALSE(attacker->IsAttackSwingArmed());
	CHECK_FALSE(attacker->IsAttacking());
}
