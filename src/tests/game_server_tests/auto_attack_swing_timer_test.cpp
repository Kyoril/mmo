// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// The auto-attack swing countdown has exactly one owner. That is not obvious from reading the code,
// because Countdown clears m_running *before* raising `ended`: every guard of the form "re-arm if
// the timer is not running" passes when called from inside a swing, and Countdown::Cancel does
// nothing there for the same reason. That combination produced two bugs -- a swing arming the
// countdown twice, and a killing blow's StopAttack being undone by an unconditional re-arm.
//
// What is covered here is GameUnitS::OnSpellCastEnded, which is where a finished cast decides
// whether to touch the swing timer at all.
//
// SCAFFOLDING LIMIT: no cast can actually be started from this suite. SpellCast::StartCast returns
// FailedError before doing anything when the caster has no WorldInstance, and no test in
// game_server_tests constructs one -- cc_movement_test and cc_ai_test deliberately test the
// *no*-WorldInstance paths. So the hook is driven directly here, with the swing countdown put into
// the state each kind of cast leaves behind, and the wiring that delivers `ended` to it exactly
// once per cast is covered only by the shape of the code (a single lifetime-scoped subscription
// taken in the constructor). Building a WorldInstance fixture would let both move down to unit
// level; the primitive underneath is pinned by src/tests/base_tests/test_countdown.cpp.

#include "catch.hpp"

#include "game_server/objects/game_unit_s.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"

#include "asio/io_service.hpp"

#include <memory>

using namespace mmo;

namespace
{
	/// A minimal concrete unit. GameUnitS leaves GetName() pure and GamePlayerS is final, so the
	/// suite brings its own subclass -- which is also what makes the protected cast-end hook
	/// reachable.
	class TestUnit final : public GameUnitS
	{
	public:
		TestUnit(const proto::Project& project, TimerQueue& timers)
			: GameUnitS(project, timers)
		{
		}

		const String& GetName() const override { return m_name; }

		/// Drives OnSpellCastEnded the way SpellCast's `ended` signal does in production.
		void RaiseCastEnded(const bool succeeded) { OnSpellCastEnded(succeeded); }

		/// Puts the swing countdown into the state CastSpell leaves it in for a cast with a cast time.
		using GameUnitS::StopSwingForCast;

		/// Stands in for StartAttack, which cannot run here: it routes through SetTarget, and that
		/// resolves the target guid through the WorldInstance this suite has none of.
		void BeginAttacking(const std::shared_ptr<GameUnitS>& victim)
		{
			SetVictim(victim);
			TriggerNextAutoAttack();
		}

	private:
		String m_name = "TestUnit";
	};

	struct SwingTimerFixture
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;

		/// @param guid Units are given distinct guids so attacker and victim stay tellable apart.
		std::shared_ptr<TestUnit> MakeUnit(const uint64 guid)
		{
			auto unit = std::make_shared<TestUnit>(project, timers);
			unit->Initialize();
			unit->Set<uint64>(object_fields::Guid, guid);
			return unit;
		}
	};
}

TEST_CASE_METHOD(SwingTimerFixture, "An instant cast leaves the scheduled melee swing where it is", "[swing_timer]")
{
	// An instant ability must not push the melee swing back. It reaches OnSpellCastEnded with the
	// swing countdown still running -- CastSpell stops that countdown only for a cast that has a
	// cast time -- and a running countdown is exactly what tells the two cases apart. Resetting on
	// the instant path meant a rotation firing instants faster than the swing interval never landed
	// an auto-attack at all, and creature rotations do fire instants on cooldown.
	const auto attacker = MakeUnit(1);
	const auto victim = MakeUnit(2);

	attacker->BeginAttacking(victim);
	REQUIRE(attacker->IsAttackSwingArmed());
	const GameTime scheduled = attacker->GetNextAttackSwingTime();

	attacker->RaiseCastEnded(true);

	CHECK(attacker->IsAttackSwingArmed());
	CHECK(attacker->GetNextAttackSwingTime() == scheduled);
}

TEST_CASE_METHOD(SwingTimerFixture, "A cast that stopped the swing resumes it a full interval out", "[swing_timer]")
{
	// The other half of the same rule: a cast with a cast time occupies the attacker, so the next
	// swing lands a full swing interval after the cast ends rather than part-way through the
	// interval it was in. CastSpell signals that by cancelling the countdown for the cast's
	// duration, so a stopped countdown here means "this cast held the attacker up".
	const auto attacker = MakeUnit(1);
	const auto victim = MakeUnit(2);

	attacker->BeginAttacking(victim);
	REQUIRE(attacker->IsAttackSwingArmed());
	const GameTime scheduled = attacker->GetNextAttackSwingTime();

	attacker->StopSwingForCast();
	REQUIRE_FALSE(attacker->IsAttackSwingArmed());

	attacker->RaiseCastEnded(true);

	CHECK(attacker->IsAttackSwingArmed());
	CHECK(attacker->GetNextAttackSwingTime() >= scheduled + attacker->GetAutoAttackTime(weapon_attack::BaseAttack));
}

TEST_CASE_METHOD(SwingTimerFixture, "A finished cast does not arm a swing when there is no victim", "[swing_timer]")
{
	// The re-arm is guarded on still having a victim. Without the guard a cast finishing after the
	// target died -- StopAttack having just cancelled the countdown and cleared the victim -- would
	// resurrect a swing timer on a unit that has left combat, and it would then keep swinging at
	// nothing.
	const auto attacker = MakeUnit(1);
	const auto victim = MakeUnit(2);

	attacker->BeginAttacking(victim);
	attacker->StopAttack();
	REQUIRE_FALSE(attacker->IsAttackSwingArmed());
	REQUIRE(attacker->GetVictim() == nullptr);

	attacker->RaiseCastEnded(true);

	CHECK_FALSE(attacker->IsAttackSwingArmed());
	CHECK(attacker->GetVictim() == nullptr);
}

TEST_CASE_METHOD(SwingTimerFixture, "A unit with no victim reports no scheduled swing", "[swing_timer]")
{
	// Baseline for the accessor the tests above lean on: it must report "nothing scheduled" for a
	// unit that has never attacked, or those assertions would pass for the wrong reason.
	const auto attacker = MakeUnit(1);

	CHECK_FALSE(attacker->IsAttackSwingArmed());
	CHECK_FALSE(attacker->IsAttacking());
}
