// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
// CC counter boundary semantics tests — T05 / M018-S01

#include "game_server/objects/game_unit_s.h"
#include "game_server/objects/game_player_s.h"
#include "game/aura.h"
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
	// Mock watcher — records apply/remove call counts per CC type
	// -----------------------------------------------------------------------
	struct MockNetUnitWatcher : NetUnitWatcherS
	{
		int stunApply   = 0; int stunRemove   = 0;
		int sleepApply  = 0; int sleepRemove  = 0;
		int fearApply   = 0; int fearRemove   = 0;
		int disoApply   = 0; int disoRemove   = 0;

		void OnStunChanged(bool applied, uint32 /*ackId*/) override
		{ applied ? ++stunApply : ++stunRemove; }

		void OnSleepChanged(bool applied, uint32 /*ackId*/) override
		{ applied ? ++sleepApply : ++sleepRemove; }

		void OnFearChanged(bool applied, uint32 /*ackId*/) override
		{ applied ? ++fearApply : ++fearRemove; }

		void OnDisorientChanged(bool applied, uint32 /*ackId*/) override
		{ applied ? ++disoApply : ++disoRemove; }

		// Pure virtuals from NetUnitWatcherS we don't need — stub them out.
		void OnTeleport(uint32 /*mapId*/, const Vector3& /*pos*/, const Radian& /*facing*/) override {}
		void OnAttackSwingEvent(AttackSwingEvent /*error*/) override {}
		void OnXpLog(uint32 /*amount*/) override {}
		void OnSpellDamageLog(uint64 /*tgt*/, uint32 /*amt*/, uint8 /*school*/, DamageFlags /*flags*/, const proto::SpellEntry& /*spell*/) override {}
		void OnNonSpellDamageLog(uint64 /*tgt*/, uint32 /*amt*/, DamageFlags /*flags*/) override {}
		void OnEnvironmentalDamageLog(uint64 /*tgt*/, uint32 /*amt*/, EnvironmentalDamageType /*type*/) override {}
		void OnSpeedChangeApplied(MovementType /*type*/, float /*speed*/, uint32 /*ackId*/) override {}
		void OnRootChanged(bool /*applied*/, uint32 /*ackId*/) override {}
		void OnLevelUp(uint32, int32, int32, int32, int32, int32, int32, int32, int32, int32) override {}
		void OnSpellModChanged(SpellModType /*type*/, uint8 /*effectIndex*/, SpellModOp /*op*/, int32 /*value*/) override {}
		void OnProficiencyChanged(uint32 /*id*/, bool /*added*/) override {}
	};

	// -----------------------------------------------------------------------
	// Helper: build a minimal GameUnitS subclass (via GamePlayerS) with a
	// mock watcher attached.  We reuse GamePlayerS because GameUnitS itself
	// is abstract; GamePlayerS is the smallest concrete subclass.
	// -----------------------------------------------------------------------
	struct CCTestFixture
	{
		asio::io_service     io;
		TimerQueue           timers{ io };
		proto::Project       project;
		MockNetUnitWatcher   watcher;

		// Forward-declare; populated in ctor
		std::shared_ptr<GameUnitS> unit;

		CCTestFixture()
		{
			// Minimal class entry so GamePlayerS::Initialize() doesn't assert
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
			player->SetNetUnitWatcher(&watcher);
			unit = player;
		}
	};
}

// ===========================================================================
// STUN
// ===========================================================================
SCENARIO("CC counter transitions - Stun", "[cc_counter][stun]")
{
	GIVEN("a unit with no stun")
	{
		CCTestFixture f;
		auto& u = *f.unit;
		auto& w = f.watcher;

		REQUIRE_FALSE(u.IsStunned());

		WHEN("stun counter goes 0 -> 1")
		{
			u.IncrementStunCount();
			u.NotifyStunChanged();

			THEN("unit is stunned and watcher fired once (apply)")
			{
				CHECK(u.IsStunned());
				CHECK(w.stunApply == 1);
				CHECK(w.stunRemove == 0);
			}

			AND_WHEN("stun counter goes 1 -> 2 (stack)")
			{
				int applyBefore = w.stunApply;
				u.IncrementStunCount();
				u.NotifyStunChanged();

				THEN("no additional watcher call — mid-stack transition is silent")
				{
					CHECK(u.IsStunned());
					CHECK(w.stunApply == applyBefore);   // no new apply call
					CHECK(w.stunRemove == 0);
				}

				AND_WHEN("stun counter goes 2 -> 1 (partial remove)")
				{
					u.DecrementStunCount();
					u.NotifyStunChanged();

					THEN("still stunned, watcher still silent")
					{
						CHECK(u.IsStunned());
						CHECK(w.stunApply == applyBefore);
						CHECK(w.stunRemove == 0);
					}

					AND_WHEN("stun counter goes 1 -> 0 (full remove)")
					{
						u.DecrementStunCount();
						u.NotifyStunChanged();

						THEN("unit no longer stunned, watcher fired once (remove)")
						{
							CHECK_FALSE(u.IsStunned());
							CHECK(w.stunRemove == 1);
							CHECK(w.stunApply == applyBefore);  // total apply unchanged
						}
					}
				}
			}
		}
	}
}

// ===========================================================================
// SLEEP
// ===========================================================================
SCENARIO("CC counter transitions - Sleep", "[cc_counter][sleep]")
{
	GIVEN("a unit with no sleep")
	{
		CCTestFixture f;
		auto& u = *f.unit;
		auto& w = f.watcher;

		REQUIRE_FALSE(u.IsSleeping());

		WHEN("sleep counter goes 0 -> 1")
		{
			u.IncrementSleepCount();
			u.NotifySleepChanged();

			THEN("unit is sleeping and watcher fired once (apply)")
			{
				CHECK(u.IsSleeping());
				CHECK(w.sleepApply == 1);
				CHECK(w.sleepRemove == 0);
			}

			AND_WHEN("sleep counter goes 1 -> 2 (stack)")
			{
				int applyBefore = w.sleepApply;
				u.IncrementSleepCount();
				u.NotifySleepChanged();

				THEN("mid-stack is silent")
				{
					CHECK(u.IsSleeping());
					CHECK(w.sleepApply == applyBefore);
					CHECK(w.sleepRemove == 0);
				}

				AND_WHEN("sleep counter goes 2 -> 1")
				{
					u.DecrementSleepCount();
					u.NotifySleepChanged();

					THEN("still sleeping, watcher silent")
					{
						CHECK(u.IsSleeping());
						CHECK(w.sleepApply == applyBefore);
						CHECK(w.sleepRemove == 0);
					}

					AND_WHEN("sleep counter goes 1 -> 0")
					{
						u.DecrementSleepCount();
						u.NotifySleepChanged();

						THEN("unit no longer sleeping, watcher fired once (remove)")
						{
							CHECK_FALSE(u.IsSleeping());
							CHECK(w.sleepRemove == 1);
						}
					}
				}
			}
		}
	}
}

// ===========================================================================
// FEAR
// ===========================================================================
SCENARIO("CC counter transitions - Fear", "[cc_counter][fear]")
{
	GIVEN("a unit with no fear")
	{
		CCTestFixture f;
		auto& u = *f.unit;
		auto& w = f.watcher;

		REQUIRE_FALSE(u.IsFeared());

		WHEN("fear counter goes 0 -> 1")
		{
			u.IncrementFearCount();
			u.NotifyFearChanged();

			THEN("unit is feared and watcher fired once (apply)")
			{
				CHECK(u.IsFeared());
				CHECK(w.fearApply == 1);
				CHECK(w.fearRemove == 0);
			}

			AND_WHEN("fear counter goes 1 -> 2 (stack)")
			{
				int applyBefore = w.fearApply;
				u.IncrementFearCount();
				u.NotifyFearChanged();

				THEN("mid-stack is silent")
				{
					CHECK(u.IsFeared());
					CHECK(w.fearApply == applyBefore);
					CHECK(w.fearRemove == 0);
				}

				AND_WHEN("fear counter goes 2 -> 1")
				{
					u.DecrementFearCount();
					u.NotifyFearChanged();

					THEN("still feared, watcher silent")
					{
						CHECK(u.IsFeared());
						CHECK(w.fearApply == applyBefore);
						CHECK(w.fearRemove == 0);
					}

					AND_WHEN("fear counter goes 1 -> 0")
					{
						u.DecrementFearCount();
						u.NotifyFearChanged();

						THEN("unit no longer feared, watcher fired once (remove)")
						{
							CHECK_FALSE(u.IsFeared());
							CHECK(w.fearRemove == 1);
						}
					}
				}
			}
		}
	}
}

// ===========================================================================
// DISORIENT
// ===========================================================================
SCENARIO("CC counter transitions - Disorient", "[cc_counter][disorient]")
{
	GIVEN("a unit with no disorient")
	{
		CCTestFixture f;
		auto& u = *f.unit;
		auto& w = f.watcher;

		REQUIRE_FALSE(u.IsDisoriented());

		WHEN("disorient counter goes 0 -> 1")
		{
			u.IncrementDisorientCount();
			u.NotifyDisorientChanged();

			THEN("unit is disoriented and watcher fired once (apply)")
			{
				CHECK(u.IsDisoriented());
				CHECK(w.disoApply == 1);
				CHECK(w.disoRemove == 0);
			}

			AND_WHEN("disorient counter goes 1 -> 2 (stack)")
			{
				int applyBefore = w.disoApply;
				u.IncrementDisorientCount();
				u.NotifyDisorientChanged();

				THEN("mid-stack is silent")
				{
					CHECK(u.IsDisoriented());
					CHECK(w.disoApply == applyBefore);
					CHECK(w.disoRemove == 0);
				}

				AND_WHEN("disorient counter goes 2 -> 1")
				{
					u.DecrementDisorientCount();
					u.NotifyDisorientChanged();

					THEN("still disoriented, watcher silent")
					{
						CHECK(u.IsDisoriented());
						CHECK(w.disoApply == applyBefore);
						CHECK(w.disoRemove == 0);
					}

					AND_WHEN("disorient counter goes 1 -> 0")
					{
						u.DecrementDisorientCount();
						u.NotifyDisorientChanged();

						THEN("unit no longer disoriented, watcher fired once (remove)")
						{
							CHECK_FALSE(u.IsDisoriented());
							CHECK(w.disoRemove == 1);
						}
					}
				}
			}
		}
	}
}
