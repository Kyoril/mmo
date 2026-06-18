// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
// CCMovementController state-transition tests — T03 / M018-S03

#include "game_server/objects/game_unit_s.h"
#include "game_server/objects/game_player_s.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/spells.pb.h"
#include "shared/proto_data/classes.pb.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

#include <memory>

using namespace mmo;

namespace
{
	// -----------------------------------------------------------------------
	// Minimal NetUnitWatcherS stub — only stubs pure-virtuals we don't care
	// about.  We deliberately do NOT set this on creature-like units so that
	// IsUnderForcedMovement() can return true.
	// -----------------------------------------------------------------------
	struct StubNetUnitWatcher : NetUnitWatcherS
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
	// Fixture that creates a GamePlayerS WITHOUT a net watcher attached —
	// simulates a server-side creature unit (no client connection).
	// IsUnderForcedMovement() should return true for these units when feared.
	// -----------------------------------------------------------------------
	struct CCMovementFixture
	{
		asio::io_service io;
		TimerQueue       timers{ io };
		proto::Project   project;

		std::shared_ptr<GameUnitS> unit;

		CCMovementFixture()
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

			auto player = std::make_shared<GamePlayerS>(project, timers);
			player->Initialize();
			if (cls) { player->SetClass(*cls); }
			player->SetLevel(1);
			// NOTE: no SetNetUnitWatcher — simulates a non-player / creature unit
			unit = player;
		}
	};

	// Fixture that attaches a watcher — simulates a player unit.
	struct CCMovementPlayerFixture : CCMovementFixture
	{
		StubNetUnitWatcher watcher;

		CCMovementPlayerFixture()
		{
			unit->SetNetUnitWatcher(&watcher);
		}
	};
}

// ===========================================================================
// Fear apply / remove
// ===========================================================================
SCENARIO("CCMovement - Fear apply starts forced movement", "[cc_movement][fear]")
{
	GIVEN("a creature unit with no fear")
	{
		CCMovementFixture f;
		auto& u = *f.unit;

		REQUIRE_FALSE(u.IsFeared());
		REQUIRE_FALSE(u.IsUnderForcedMovement());

		WHEN("fear counter goes 0 -> 1 and NotifyFearChanged() is called")
		{
			u.IncrementFearCount();
			u.NotifyFearChanged();

			THEN("IsUnderForcedMovement() returns true")
			{
				CHECK(u.IsFeared());
				CHECK(u.IsUnderForcedMovement());
			}

			AND_WHEN("fear counter goes 1 -> 0 and NotifyFearChanged() is called")
			{
				u.DecrementFearCount();
				u.NotifyFearChanged();

				THEN("IsUnderForcedMovement() returns false")
				{
					CHECK_FALSE(u.IsFeared());
					CHECK_FALSE(u.IsUnderForcedMovement());
				}
			}
		}
	}
}

// ===========================================================================
// Disorient apply / remove
// ===========================================================================
SCENARIO("CCMovement - Disorient apply starts forced movement", "[cc_movement][disorient]")
{
	GIVEN("a creature unit with no disorient")
	{
		CCMovementFixture f;
		auto& u = *f.unit;

		REQUIRE_FALSE(u.IsDisoriented());
		REQUIRE_FALSE(u.IsUnderForcedMovement());

		WHEN("disorient counter goes 0 -> 1 and NotifyDisorientChanged() is called")
		{
			u.IncrementDisorientCount();
			u.NotifyDisorientChanged();

			THEN("IsUnderForcedMovement() returns true")
			{
				CHECK(u.IsDisoriented());
				CHECK(u.IsUnderForcedMovement());
			}

			AND_WHEN("disorient counter goes 1 -> 0 and NotifyDisorientChanged() is called")
			{
				u.DecrementDisorientCount();
				u.NotifyDisorientChanged();

				THEN("IsUnderForcedMovement() returns false")
				{
					CHECK_FALSE(u.IsDisoriented());
					CHECK_FALSE(u.IsUnderForcedMovement());
				}
			}
		}
	}
}

// ===========================================================================
// Fear priority over Disorient
// ===========================================================================
SCENARIO("CCMovement - Fear takes priority over Disorient", "[cc_movement][priority]")
{
	GIVEN("a creature unit with both fear and disorient active")
	{
		CCMovementFixture f;
		auto& u = *f.unit;

		u.IncrementFearCount();
		u.NotifyFearChanged();
		u.IncrementDisorientCount();
		u.NotifyDisorientChanged();

		REQUIRE(u.IsFeared());
		REQUIRE(u.IsDisoriented());
		REQUIRE(u.IsUnderForcedMovement());

		WHEN("fear is removed (disorient remains)")
		{
			u.DecrementFearCount();
			u.NotifyFearChanged();

			THEN("IsUnderForcedMovement() is still true because disorient is active")
			{
				CHECK_FALSE(u.IsFeared());
				CHECK(u.IsDisoriented());
				CHECK(u.IsUnderForcedMovement());
			}

			AND_WHEN("disorient is also removed")
			{
				u.DecrementDisorientCount();
				u.NotifyDisorientChanged();

				THEN("IsUnderForcedMovement() returns false")
				{
					CHECK_FALSE(u.IsDisoriented());
					CHECK_FALSE(u.IsUnderForcedMovement());
				}
			}
		}
	}
}

// ===========================================================================
// Stun suppresses movement while controller stays active
// ===========================================================================
SCENARIO("CCMovement - Stun suppresses wander but controller stays active", "[cc_movement][stun]")
{
	GIVEN("a feared creature unit")
	{
		CCMovementFixture f;
		auto& u = *f.unit;

		u.IncrementFearCount();
		u.NotifyFearChanged();
		REQUIRE(u.IsUnderForcedMovement());

		WHEN("stun is applied")
		{
			u.IncrementStunCount();
			u.NotifyStunChanged();

			THEN("controller is still active (fear persists) and unit is stunned")
			{
				CHECK(u.IsFeared());
				CHECK(u.IsStunned());
				// Controller remains active even while stunned
				CHECK(u.IsUnderForcedMovement());
			}

			AND_WHEN("stun is removed")
			{
				u.DecrementStunCount();
				u.NotifyStunChanged();

				THEN("unit is no longer stunned and forced movement is still active")
				{
					CHECK_FALSE(u.IsStunned());
					CHECK(u.IsFeared());
					CHECK(u.IsUnderForcedMovement());
				}
			}
		}
	}
}

// ===========================================================================
// No-WorldInstance stand-still: PickNextWanderPoint must not crash
// ===========================================================================
SCENARIO("CCMovement - No WorldInstance: tick does not crash and controller stays active", "[cc_movement][no_world]")
{
	GIVEN("a feared creature unit with no world instance attached")
	{
		CCMovementFixture f;
		auto& u = *f.unit;

		// Unit has no WorldInstance (never placed in a map) — wander tick must
		// degrade gracefully to stand-still instead of crashing.
		u.IncrementFearCount();
		u.NotifyFearChanged();
		REQUIRE(u.IsUnderForcedMovement());

		WHEN("OnWanderTick is triggered via stun-cycle (no map attached)")
		{
			// Cycling stun triggers OnWanderTick via NotifyStunChanged paths
			u.IncrementStunCount();
			u.NotifyStunChanged();
			u.DecrementStunCount();
			u.NotifyStunChanged();

			THEN("controller stays active and no crash occurred")
			{
				CHECK(u.IsUnderForcedMovement());
			}
		}
	}
}

// ===========================================================================
// Negative: player unit (watcher attached) never gets forced movement
// ===========================================================================
SCENARIO("CCMovement - Player unit with watcher never gets IsUnderForcedMovement", "[cc_movement][player_negative]")
{
	GIVEN("a player unit with a net watcher attached")
	{
		CCMovementPlayerFixture f;
		auto& u = *f.unit;

		REQUIRE_FALSE(u.IsUnderForcedMovement());

		WHEN("fear is applied")
		{
			u.IncrementFearCount();
			u.NotifyFearChanged();

			THEN("IsUnderForcedMovement() remains false for player units")
			{
				CHECK(u.IsFeared());
				CHECK_FALSE(u.IsUnderForcedMovement());
			}
		}

		WHEN("disorient is applied")
		{
			u.IncrementDisorientCount();
			u.NotifyDisorientChanged();

			THEN("IsUnderForcedMovement() remains false for player units")
			{
				CHECK(u.IsDisoriented());
				CHECK_FALSE(u.IsUnderForcedMovement());
			}
		}
	}
}
