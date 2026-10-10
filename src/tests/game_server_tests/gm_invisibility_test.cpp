// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/objects/game_player_s.h"
#include "game/aura.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

#include <memory>

using namespace mmo;

namespace
{
	/// Helper: build a minimal shared GamePlayerS with a class entry set up
	/// so RefreshStats() / SetLevel() work without asserting.
	std::shared_ptr<GamePlayerS> MakeUnit(proto::Project& project, TimerQueue& timers, uint32 level = 1)
	{
		auto* cls = project.classes.getById(1);
		if (!cls)
		{
			cls = project.classes.add(1);
		}

		if (cls)
		{
			cls->set_powertype(proto::ClassEntry_PowerType_MANA);
			while (static_cast<uint32>(cls->levelbasevalues_size()) < level + 1)
			{
				auto* lbv = cls->add_levelbasevalues();
				lbv->set_health(100);
				lbv->set_mana(100);
				lbv->set_stamina(10);
				lbv->set_strength(10);
				lbv->set_agility(10);
				lbv->set_intellect(10);
				lbv->set_spirit(10);
			}
		}

		auto unit = std::make_shared<GamePlayerS>(project, timers);
		unit->Initialize();
		if (cls)
		{
			unit->SetClass(*cls);
		}
		unit->SetLevel(level);
		return unit;
	}
}

TEST_CASE("GM-invisible unit is hidden from players and visible to GMs", "[gm_invisibility]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	const auto gm = MakeUnit(project, timers, 10);
	const auto player = MakeUnit(project, timers, 60);
	const auto otherGm = MakeUnit(project, timers, 10);
	otherGm->SetIsGameMaster(true);

	gm->SetGmInvisible(true);
	CHECK(gm->IsGmInvisible());
	CHECK(gm->GetVisibility() == unit_visibility::Off);

	player->Relocate(Vector3(0.0f, 0.0f, 0.0f), Radian(0.0f));
	otherGm->Relocate(Vector3(0.0f, 0.0f, 0.0f), Radian(0.0f));
	gm->Relocate(Vector3(1.0f, 0.0f, 0.0f), Radian(0.0f));

	CHECK_FALSE(gm->CanBeSeenBy(*player));
	CHECK(gm->CanBeSeenBy(*otherGm));
}

TEST_CASE("Visibility recomputation keeps a GM-invisible unit Off", "[gm_invisibility]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	const auto gm = MakeUnit(project, timers, 10);
	gm->SetGmInvisible(true);

	// Aura add/remove and interrupt-removal all funnel into NotifyVisibilityChanged; without
	// any visibility aura it used to fall through to On.
	gm->NotifyVisibilityChanged();
	CHECK(gm->GetVisibility() == unit_visibility::Off);
}

TEST_CASE("Turning GM invisibility off restores normal visibility", "[gm_invisibility]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	const auto gm = MakeUnit(project, timers, 10);
	gm->SetGmInvisible(true);
	gm->SetGmInvisible(false);

	CHECK_FALSE(gm->IsGmInvisible());
	CHECK(gm->GetVisibility() == unit_visibility::On);
}
