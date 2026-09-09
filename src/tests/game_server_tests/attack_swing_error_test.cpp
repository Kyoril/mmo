// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Auto-attack swing errors are a *state machine*, not a stream of events: the client is told when
// the condition changes and then repeats the message on its own until it is told otherwise. That
// only works if every transition is reported -- including the transition back to a landing swing.
// It previously was not, so a single out-of-range swing left the client repeating "out of range"
// (and playing its voice line) forever, straight through the attacks that followed.
//
// The state lives on GameUnitS so it resets with the attack it belongs to; these tests pin the
// three properties the client relies on: transitions are reported, repeats are not, and starting
// or stopping an attack forgets the previous state.
//
// SCAFFOLDING LIMIT: the hook is driven directly rather than by resolving real swings, and the
// rule that only the main hand reports (the isOffhand guards in ExecuteAutoAttackSwing) is
// therefore not covered here. Reaching it needs a WorldInstance no suite in game_server_tests
// builds -- the same limit auto_attack_swing_timer_test.cpp documents. The E2E scenario
// melee_swing_error_recovery.lua covers the path end to end against a real server.

#include "catch.hpp"

#include "game_server/objects/game_unit_s.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"

#include "asio/io_service.hpp"

#include <memory>
#include <vector>

using namespace mmo;

namespace
{
	/// Records the attack swing events that reach the network layer. Everything else on the
	/// watcher interface is irrelevant here and stubbed out.
	class RecordingWatcher final : public NetUnitWatcherS
	{
	public:
		std::vector<AttackSwingEvent> events;

		void OnAttackSwingEvent(const AttackSwingEvent error) override { events.push_back(error); }

		void OnTeleport(uint32, const Vector3&, const Radian&) override {}
		void OnXpLog(uint32) override {}
		void OnSpellDamageLog(uint64, uint32, uint8, DamageFlags, const proto::SpellEntry&, uint32) override {}
		void OnNonSpellDamageLog(uint64, uint32, DamageFlags) override {}
		void OnEnvironmentalDamageLog(uint64, uint32, EnvironmentalDamageType) override {}
		void OnSpeedChangeApplied(MovementType, float, uint32) override {}
		void OnRootChanged(bool, uint32) override {}
		void OnStunChanged(bool, uint32) override {}
		void OnSleepChanged(bool, uint32) override {}
		void OnFearChanged(bool, uint32) override {}
		void OnDisorientChanged(bool, uint32) override {}
		void OnPendingCharge(float, uint32) override {}
		void OnLevelUp(uint32, int32, int32, int32, int32, int32, int32, int32, int32, int32) override {}
		void OnSpellModChanged(SpellModType, uint8, SpellModOp, int32) override {}
		void OnProficiencyChanged(uint32, bool) override {}
		void OnReviveOffer(uint64, uint32, uint32, uint32, const Vector3&, const Radian&) override {}
	};

	/// A minimal concrete unit; GameUnitS leaves GetName() pure and the swing event hook protected.
	class TestUnit final : public GameUnitS
	{
	public:
		TestUnit(const proto::Project& project, TimerQueue& timers)
			: GameUnitS(project, timers)
		{
		}

		const String& GetName() const override { return m_name; }

		/// Drives the hook the swing resolution calls for each of its outcomes.
		void RaiseSwingEvent(const AttackSwingEvent event) { OnAttackSwingEvent(event); }

		/// Stands in for StartAttack, which cannot run here: it routes through SetTarget, and that
		/// resolves the target guid through the WorldInstance this suite has none of.
		using GameUnitS::SetVictim;

	private:
		String m_name = "TestUnit";
	};

	struct SwingErrorFixture
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;
		RecordingWatcher watcher;

		std::shared_ptr<TestUnit> MakeUnit(const uint64 guid)
		{
			auto unit = std::make_shared<TestUnit>(project, timers);
			unit->Initialize();
			unit->Set<uint64>(object_fields::Guid, guid);
			return unit;
		}

		std::shared_ptr<TestUnit> MakeWatchedUnit(const uint64 guid)
		{
			auto unit = MakeUnit(guid);
			unit->SetNetUnitWatcher(&watcher);
			return unit;
		}
	};
}

TEST_CASE_METHOD(SwingErrorFixture, "A swing that lands after an error reports the recovery", "[swing_error]")
{
	// The bug this suite exists for. The client repeats the last error it was told about until it
	// hears something else, so a landing swing has to be reported -- otherwise walking into range
	// keeps the "out of range" message and its voice line running on top of the attacks.
	const auto attacker = MakeWatchedUnit(1);

	attacker->RaiseSwingEvent(attack_swing_event::OutOfRange);
	attacker->RaiseSwingEvent(attack_swing_event::Success);

	REQUIRE(watcher.events.size() == 2);
	CHECK(watcher.events[0] == attack_swing_event::OutOfRange);
	CHECK(watcher.events[1] == attack_swing_event::Success);
}

TEST_CASE_METHOD(SwingErrorFixture, "A swing outcome that did not change is reported only once", "[swing_error]")
{
	// Out-of-range swings retry several times a second. Only the transition goes on the wire; the
	// client owns the repeat.
	const auto attacker = MakeWatchedUnit(1);

	attacker->RaiseSwingEvent(attack_swing_event::OutOfRange);
	attacker->RaiseSwingEvent(attack_swing_event::OutOfRange);
	attacker->RaiseSwingEvent(attack_swing_event::OutOfRange);

	REQUIRE(watcher.events.size() == 1);
	CHECK(watcher.events[0] == attack_swing_event::OutOfRange);
}

TEST_CASE_METHOD(SwingErrorFixture, "Landing swings after the recovery stay silent", "[swing_error]")
{
	// The recovery is a transition like any other: an ongoing fight must not send one notification
	// per swing.
	const auto attacker = MakeWatchedUnit(1);

	attacker->RaiseSwingEvent(attack_swing_event::OutOfRange);
	attacker->RaiseSwingEvent(attack_swing_event::Success);
	attacker->RaiseSwingEvent(attack_swing_event::Success);
	attacker->RaiseSwingEvent(attack_swing_event::Success);

	CHECK(watcher.events.size() == 2);
}

TEST_CASE_METHOD(SwingErrorFixture, "An error that comes back after a recovery is reported again", "[swing_error]")
{
	// The return leg of the same journey: walking back out of range has to speak up again. An
	// implementation that reported the recovery by forgetting the state instead of recording it
	// would pass every test above and still fail this one.
	const auto attacker = MakeWatchedUnit(1);

	attacker->RaiseSwingEvent(attack_swing_event::OutOfRange);
	attacker->RaiseSwingEvent(attack_swing_event::Success);
	attacker->RaiseSwingEvent(attack_swing_event::OutOfRange);

	REQUIRE(watcher.events.size() == 3);
	CHECK(watcher.events[2] == attack_swing_event::OutOfRange);
}

TEST_CASE_METHOD(SwingErrorFixture, "Stopping the attack forgets the last swing outcome", "[swing_error]")
{
	// Without this, a player who breaks off an out-of-range attack and starts it again -- still out
	// of range -- is told nothing at all, because the remembered state still says "out of range".
	// The events are not cleared in between on purpose: the count also pins that the reset itself
	// puts nothing on the wire, since attack_swing_event::Unknown has no message for the client.
	const auto attacker = MakeWatchedUnit(1);

	attacker->RaiseSwingEvent(attack_swing_event::OutOfRange);
	attacker->StopAttack();
	attacker->RaiseSwingEvent(attack_swing_event::OutOfRange);

	REQUIRE(watcher.events.size() == 2);
	CHECK(watcher.events[0] == attack_swing_event::OutOfRange);
	CHECK(watcher.events[1] == attack_swing_event::OutOfRange);
}

TEST_CASE_METHOD(SwingErrorFixture, "Switching victim forgets the last swing outcome", "[swing_error]")
{
	// Switching to a second target that is out of range for the same reason has to report the
	// error again: the remembered outcome describes a swing against the previous victim.
	const auto attacker = MakeWatchedUnit(1);
	const auto secondVictim = MakeUnit(3);

	attacker->RaiseSwingEvent(attack_swing_event::OutOfRange);
	attacker->SetVictim(secondVictim);
	attacker->RaiseSwingEvent(attack_swing_event::OutOfRange);

	REQUIRE(watcher.events.size() == 2);
	CHECK(watcher.events[1] == attack_swing_event::OutOfRange);
}
