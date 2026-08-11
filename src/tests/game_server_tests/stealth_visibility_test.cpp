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

	constexpr float Pif = 3.14159265f;
}

TEST_CASE("Unit visibility defaults to On", "[stealth]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	const auto unit = MakeUnit(project, timers);
	CHECK(unit->GetVisibility() == unit_visibility::On);
}

TEST_CASE("Stealthed unit cannot be seen from behind even at melee range", "[stealth]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	const auto stealthed = MakeUnit(project, timers, 10);
	const auto observer = MakeUnit(project, timers, 10);

	stealthed->SetVisibility(unit_visibility::GroupStealth);

	// Observer at origin facing -X (yaw = pi -> forward (-1, 0, 0)); stealthed unit
	// 2m in +X direction, which is behind the observer.
	observer->Relocate(Vector3(0.0f, 0.0f, 0.0f), Radian(Pif));
	stealthed->Relocate(Vector3(2.0f, 0.0f, 0.0f), Radian(0.0f));

	CHECK(!stealthed->CanBeSeenBy(*observer));
}

TEST_CASE("Stealthed unit is seen inside the front cone within detection range", "[stealth]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	const auto stealthed = MakeUnit(project, timers, 10);
	const auto observer = MakeUnit(project, timers, 10);

	stealthed->SetVisibility(unit_visibility::GroupStealth);

	// Observer at origin facing +X (yaw 0 -> forward (1, 0, 0)); stealthed 5m ahead.
	observer->Relocate(Vector3(0.0f, 0.0f, 0.0f), Radian(0.0f));
	stealthed->Relocate(Vector3(5.0f, 0.0f, 0.0f), Radian(0.0f));

	CHECK(stealthed->CanBeSeenBy(*observer));
}

TEST_CASE("Stealthed unit outside detection range is not seen even in front cone", "[stealth]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	const auto stealthed = MakeUnit(project, timers, 10);
	const auto observer = MakeUnit(project, timers, 10);

	stealthed->SetVisibility(unit_visibility::GroupStealth);

	// Same level: detection range == stealth::BaseDetectionRange (10m). Place at 15m.
	observer->Relocate(Vector3(0.0f, 0.0f, 0.0f), Radian(0.0f));
	stealthed->Relocate(Vector3(15.0f, 0.0f, 0.0f), Radian(0.0f));

	CHECK(!stealthed->CanBeSeenBy(*observer));
}

TEST_CASE("Higher level observer detects stealth from further away", "[stealth]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	const auto stealthed = MakeUnit(project, timers, 5);
	const auto observer = MakeUnit(project, timers, 10);	// +5 levels -> 10 + 5 * 1 = 15m range

	stealthed->SetVisibility(unit_visibility::GroupStealth);

	observer->Relocate(Vector3(0.0f, 0.0f, 0.0f), Radian(0.0f));
	stealthed->Relocate(Vector3(14.0f, 0.0f, 0.0f), Radian(0.0f));

	CHECK(stealthed->CanBeSeenBy(*observer));
}

TEST_CASE("Lower level observer has reduced stealth detection range", "[stealth]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	const auto stealthed = MakeUnit(project, timers, 10);
	const auto observer = MakeUnit(project, timers, 5);		// -5 levels -> 10 - 5 * 1.5 = 2.5m range

	stealthed->SetVisibility(unit_visibility::GroupStealth);

	observer->Relocate(Vector3(0.0f, 0.0f, 0.0f), Radian(0.0f));
	stealthed->Relocate(Vector3(5.0f, 0.0f, 0.0f), Radian(0.0f));
	CHECK(!stealthed->CanBeSeenBy(*observer));

	stealthed->Relocate(Vector3(2.0f, 0.0f, 0.0f), Radian(0.0f));
	CHECK(stealthed->CanBeSeenBy(*observer));
}

TEST_CASE("Party members always see stealthed group mates regardless of cone and range", "[stealth]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	const auto stealthed = MakeUnit(project, timers, 10);
	const auto observer = MakeUnit(project, timers, 10);

	stealthed->SetGroupId(42);
	observer->SetGroupId(42);
	stealthed->SetVisibility(unit_visibility::GroupStealth);

	// Behind the observer and far away - still visible to the party member.
	observer->Relocate(Vector3(0.0f, 0.0f, 0.0f), Radian(Pif));
	stealthed->Relocate(Vector3(30.0f, 0.0f, 0.0f), Radian(0.0f));

	CHECK(stealthed->CanBeSeenBy(*observer));
}

TEST_CASE("Units in different groups do not get party stealth vision", "[stealth]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	const auto stealthed = MakeUnit(project, timers, 10);
	const auto observer = MakeUnit(project, timers, 10);

	stealthed->SetGroupId(42);
	observer->SetGroupId(43);
	stealthed->SetVisibility(unit_visibility::GroupStealth);

	// Behind the observer: no detection possible, and no shared group.
	observer->Relocate(Vector3(0.0f, 0.0f, 0.0f), Radian(Pif));
	stealthed->Relocate(Vector3(2.0f, 0.0f, 0.0f), Radian(0.0f));

	CHECK(!stealthed->CanBeSeenBy(*observer));
}

TEST_CASE("Invisibility (Off) still hides from everyone except GMs", "[stealth]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	const auto invisible = MakeUnit(project, timers, 10);
	const auto observer = MakeUnit(project, timers, 60);

	invisible->SetVisibility(unit_visibility::Off);
	observer->Relocate(Vector3(0.0f, 0.0f, 0.0f), Radian(0.0f));
	invisible->Relocate(Vector3(1.0f, 0.0f, 0.0f), Radian(0.0f));

	CHECK(!invisible->CanBeSeenBy(*observer));

	observer->SetIsGameMaster(true);
	CHECK(invisible->CanBeSeenBy(*observer));
}
