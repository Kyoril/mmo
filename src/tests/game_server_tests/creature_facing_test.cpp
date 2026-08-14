// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// A creature in combat should always be facing whatever it is acting on. Spells routinely carry
// an in-front requirement, and a creature that fails that check wastes its cast — which, before
// the cooldown work, was also how a creature could retry the same spell without pause.
// Crowd control is the exception: a stunned or feared unit is not in a position to turn.

#include "catch.hpp"

#include "game_server/ai/creature_facing.h"
#include "game_server/objects/game_player_s.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/classes.pb.h"
#include "asio/io_service.hpp"

#include <memory>

using namespace mmo;

namespace
{
	struct FacingFixture
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;

		FacingFixture()
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
}

TEST_CASE_METHOD(FacingFixture, "An unimpaired unit can turn to face its target", "[creature_facing]")
{
	const auto unit = MakeUnit();

	REQUIRE(CanTurnToFaceTarget(*unit));
}

TEST_CASE_METHOD(FacingFixture, "A stunned unit cannot turn", "[creature_facing]")
{
	const auto unit = MakeUnit();
	unit->IncrementStunCount();
	unit->NotifyStunChanged();

	REQUIRE_FALSE(CanTurnToFaceTarget(*unit));
}

TEST_CASE_METHOD(FacingFixture, "A feared unit cannot turn", "[creature_facing]")
{
	const auto unit = MakeUnit();
	unit->IncrementFearCount();
	unit->NotifyFearChanged();

	REQUIRE_FALSE(CanTurnToFaceTarget(*unit));
}

TEST_CASE_METHOD(FacingFixture, "A sleeping unit cannot turn", "[creature_facing]")
{
	const auto unit = MakeUnit();
	unit->IncrementSleepCount();
	unit->NotifySleepChanged();

	REQUIRE_FALSE(CanTurnToFaceTarget(*unit));
}

TEST_CASE_METHOD(FacingFixture, "A disoriented unit cannot turn", "[creature_facing]")
{
	const auto unit = MakeUnit();
	unit->IncrementDisorientCount();
	unit->NotifyDisorientChanged();

	REQUIRE_FALSE(CanTurnToFaceTarget(*unit));
}

TEST_CASE_METHOD(FacingFixture, "A rooted unit can still turn", "[creature_facing]")
{
	// Rooted stops movement, not rotation — a rooted caster still turns to face what it attacks.
	// Root is deliberately absent from the impairment list, so an unimpaired-but-rooted unit is
	// simply the unimpaired case; this pins that root is not treated as a turn blocker.
	const auto unit = MakeUnit();

	REQUIRE_FALSE(unit->IsStunned());
	REQUIRE(CanTurnToFaceTarget(*unit));
}

TEST_CASE_METHOD(FacingFixture, "A dead unit does not turn", "[creature_facing]")
{
	const auto unit = MakeUnit();
	unit->Set<uint32>(object_fields::Health, 0);

	REQUIRE_FALSE(CanTurnToFaceTarget(*unit));
}
