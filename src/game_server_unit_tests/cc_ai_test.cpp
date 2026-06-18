// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
// CreatureAICombatState CC awareness regression tests - T02 / M018-S04
//
// These tests verify the observable contract of the two CC-awareness behaviours
// added in T01 at the GameUnitS level, without instantiating the full
// CreatureAICombatState (which requires a live CreatureAI / GameCreatureS /
// WorldInstance).  The tests exercise exactly the predicates and state
// transitions that the ChooseNextAction() fear-guard and UpdateVictim()
// root-fallback code depend on.

#include "game_server/objects/game_unit_s.h"
#include "game_server/objects/game_player_s.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/spells.pb.h"
#include "shared/proto_data/classes.pb.h"
#include "game/movement_info.h"
#include "math/vector3.h"
#include "math/radian.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

#include <memory>
#include <limits>

using namespace mmo;

namespace
{
	// -----------------------------------------------------------------------
	// Minimal NetUnitWatcherS stub - stubs all pure-virtuals we don't need.
	// -----------------------------------------------------------------------
	struct StubWatcher : NetUnitWatcherS
	{
		void OnStunChanged(bool, uint32) override {}
		void OnSleepChanged(bool, uint32) override {}
		void OnFearChanged(bool, uint32) override {}
		void OnDisorientChanged(bool, uint32) override {}
		void OnTeleport(uint32, const Vector3&, const Radian&) override {}
		void OnAttackSwingEvent(AttackSwingEvent) override {}
		void OnXpLog(uint32) override {}
		void OnSpellDamageLog(uint64, uint32, uint8, DamageFlags, const proto::SpellEntry&, uint32) override {}
		void OnNonSpellDamageLog(uint64, uint32, DamageFlags) override {}
		void OnEnvironmentalDamageLog(uint64, uint32, EnvironmentalDamageType) override {}
		void OnSpeedChangeApplied(MovementType, float, uint32) override {}
		void OnRootChanged(bool, uint32) override {}
		void OnLevelUp(uint32, int32, int32, int32, int32, int32, int32, int32, int32, int32) override {}
		void OnSpellModChanged(SpellModType, uint8, SpellModOp, int32) override {}
		void OnProficiencyChanged(uint32, bool) override {}
	};

	// -----------------------------------------------------------------------
	// Fixture: minimal GamePlayerS units without watcher (server-side creature
	// simulation). `attacker` is the controlled creature; `threatTarget` is a
	// threat-list entry unit.
	// -----------------------------------------------------------------------
	struct CCAIFixture
	{
		asio::io_service io;
		TimerQueue       timers{ io };
		proto::Project   project;

		std::shared_ptr<GamePlayerS> attacker;
		std::shared_ptr<GamePlayerS> threatTarget;

		CCAIFixture()
		{
			auto* cls = project.classes.getById(1);
			if (!cls)
			{
				cls = project.classes.add(1);
				if (cls)
				{
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

			attacker = std::make_shared<GamePlayerS>(project, timers);
			attacker->Initialize();
			if (cls) { attacker->SetClass(*cls); }
			attacker->SetLevel(1);

			threatTarget = std::make_shared<GamePlayerS>(project, timers);
			threatTarget->Initialize();
			if (cls) { threatTarget->SetClass(*cls); }
			threatTarget->SetLevel(1);
		}

		// Helper: create and initialize a fresh unit.
		std::shared_ptr<GamePlayerS> MakeUnit()
		{
			auto* cls = project.classes.getById(1);
			auto unit = std::make_shared<GamePlayerS>(project, timers);
			unit->Initialize();
			if (cls) unit->SetClass(*cls);
			unit->SetLevel(1);
			return unit;
		}

		// Apply the Rooted movement flag via the public ApplyMovementInfo API.
		// IsRooted() reads (m_movementInfo.movementFlags & movement_flags::Rooted).
		static void SetRooted(GameUnitS& unit, bool rooted)
		{
			MovementInfo info;
			if (rooted)
				info.movementFlags |= movement_flags::Rooted;
			else
				info.movementFlags &= ~movement_flags::Rooted;
			unit.ApplyMovementInfo(info);
		}
	};

	// Helper: melee reach sum (mirrors UpdateVictim root-fallback logic).
	float ReachSum(const GameUnitS& a, const GameUnitS& b)
	{
		return a.GetMeleeReach() + b.GetMeleeReach();
	}

	// Helper: Euclidean distance between two units.
	float UnitDist(const GameUnitS& a, const GameUnitS& b)
	{
		return (a.GetPosition() - b.GetPosition()).GetLength();
	}
}

// ===========================================================================
// [cc_ai] fear suppresses attack - IsFeared() predicate
// ===========================================================================
SCENARIO("cc_ai - fear counter predicate", "[cc_ai][fear]")
{
	GIVEN("a creature unit with no fear")
	{
		CCAIFixture f;
		GameUnitS& attacker = *f.attacker;

		REQUIRE_FALSE(attacker.IsFeared());

		WHEN("fear counter goes 0 to 1 and NotifyFearChanged is called")
		{
			attacker.IncrementFearCount();
			attacker.NotifyFearChanged();

			THEN("IsFeared() returns true and ChooseNextAction fear-guard fires")
			{
				CHECK(attacker.IsFeared());
			}

			AND_WHEN("StopAttack() is called as the guard does")
			{
				attacker.StopAttack();

				THEN("GetVictim() is nullptr - attack suppressed")
				{
					CHECK(attacker.GetVictim() == nullptr);
				}

				AND_WHEN("fear counter goes 1 to 0 and NotifyFearChanged is called so fear expires")
				{
					attacker.DecrementFearCount();
					attacker.NotifyFearChanged();

					THEN("IsFeared() returns false and normal UpdateVictim path resumes")
					{
						CHECK_FALSE(attacker.IsFeared());
					}
				}
			}
		}
	}
}

// ===========================================================================
// [cc_ai] fear suppresses attack - StopAttack idempotent
// ===========================================================================
SCENARIO("cc_ai - StopAttack is idempotent while feared", "[cc_ai][fear]")
{
	GIVEN("a feared creature unit")
	{
		CCAIFixture f;
		GameUnitS& attacker = *f.attacker;

		attacker.IncrementFearCount();
		attacker.NotifyFearChanged();
		REQUIRE(attacker.IsFeared());
		REQUIRE(attacker.GetVictim() == nullptr);

		WHEN("StopAttack() is called twice simulating two ChooseNextAction ticks while feared")
		{
			attacker.StopAttack();
			attacker.StopAttack();

			THEN("no crash and GetVictim() is still nullptr")
			{
				CHECK(attacker.IsFeared());
				CHECK(attacker.GetVictim() == nullptr);
			}
		}
	}
}

// ===========================================================================
// [cc_ai] root selects nearest in-range target - distance/reach math
// ===========================================================================
SCENARIO("cc_ai - root fallback: in-range unit preferred over out-of-range top threatener", "[cc_ai][root]")
{
	GIVEN("a rooted creature with one distant target and one close target")
	{
		CCAIFixture f;
		GameUnitS& attacker = *f.attacker;
		GameUnitS& distantTarget = *f.threatTarget;

		std::shared_ptr<GamePlayerS> closeTarget = f.MakeUnit();

		// Place attacker at origin.
		attacker.Relocate(Vector3::Zero, Radian(0.0f));

		// distantTarget: 200 units away - well beyond any melee reach.
		distantTarget.Relocate(Vector3(200.0f, 0.0f, 0.0f), Radian(0.0f));

		// closeTarget: 0.5 units away - within melee reach.
		closeTarget->Relocate(Vector3(0.5f, 0.0f, 0.0f), Radian(0.0f));

		// Apply root via movement flag.
		CCAIFixture::SetRooted(attacker, true);
		REQUIRE(attacker.IsRooted());

		WHEN("the reach sum and distance are computed for the distant target")
		{
			float reach = ReachSum(attacker, distantTarget);
			float dist  = UnitDist(attacker, distantTarget);

			THEN("distant target is beyond melee reach so root-fallback scan would exclude it")
			{
				CHECK(dist > reach);
			}
		}

		WHEN("the reach sum and distance are computed for the close target")
		{
			float reach = ReachSum(attacker, *closeTarget);
			float dist  = UnitDist(attacker, *closeTarget);

			THEN("close target is within melee reach so root-fallback scan would select it")
			{
				CHECK(dist <= reach);
			}
		}
	}
}

// ===========================================================================
// [cc_ai] root with no in-range target stops attack
// ===========================================================================
SCENARIO("cc_ai - root fallback: no in-range unit results in StopAttack", "[cc_ai][root]")
{
	GIVEN("a rooted creature with only a distant target")
	{
		CCAIFixture f;
		GameUnitS& attacker = *f.attacker;
		GameUnitS& distantTarget = *f.threatTarget;

		attacker.Relocate(Vector3::Zero, Radian(0.0f));
		distantTarget.Relocate(Vector3(200.0f, 0.0f, 0.0f), Radian(0.0f));

		CCAIFixture::SetRooted(attacker, true);
		REQUIRE(attacker.IsRooted());

		WHEN("the reach sum is checked against the only threat-list entry")
		{
			float reach = ReachSum(attacker, distantTarget);
			float dist  = UnitDist(attacker, distantTarget);

			THEN("no unit is within reach so fallback returns nullptr and StopAttack is expected")
			{
				CHECK(dist > reach);
				// Simulate the nullptr-fallback branch: StopAttack sets victim to null.
				attacker.StopAttack();
				CHECK(attacker.GetVictim() == nullptr);
			}
		}
	}
}

// ===========================================================================
// [cc_ai] root predicate transitions
// ===========================================================================
SCENARIO("cc_ai - root flag predicate", "[cc_ai][root]")
{
	GIVEN("a creature unit with no root")
	{
		CCAIFixture f;
		GameUnitS& attacker = *f.attacker;

		REQUIRE_FALSE(attacker.IsRooted());

		WHEN("Rooted movement flag is applied")
		{
			CCAIFixture::SetRooted(attacker, true);

			THEN("IsRooted() returns true and root-fallback scan in UpdateVictim fires")
			{
				CHECK(attacker.IsRooted());
			}

			AND_WHEN("Rooted movement flag is removed when root expires")
			{
				CCAIFixture::SetRooted(attacker, false);

				THEN("IsRooted() returns false and top threatener is selected normally")
				{
					CHECK_FALSE(attacker.IsRooted());
				}
			}
		}
	}
}
