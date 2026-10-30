// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "test_unit_factory.h"

#include "game/object_type_id.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

using namespace mmo;
using namespace mmo::test;

// Regeneration is continuous. The server settles it whenever something depends on health or power,
// so a spell the client allows once its predicted mana reaches the cost is not refused because the
// server is still waiting for its next regeneration tick. The test unit regenerates 20 mana and 10
// health per two second tick, i.e. 10 mana and 5 health per second; the casting share is 30%.

namespace
{
	std::shared_ptr<GamePlayerS> MakeRegeneratingUnit(proto::Project& project, TimerQueue& timers)
	{
		auto unit = MakeUnit(project, timers);
		unit->StartRegeneration();
		return unit;
	}
}

TEST_CASE("Mana regenerates exactly for the elapsed time, carrying fractions of a point", "[regeneration]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeRegeneratingUnit(project, timers);
	unit->Set<int32>(object_fields::Mana, 0);
	const GameTime start = unit->GetRegenSettledAt();

	unit->SettleRegeneration(start + 1250);
	CHECK(unit->Get<int32>(object_fields::Mana) == 12);	// 12.5

	unit->SettleRegeneration(start + 1300);
	CHECK(unit->Get<int32>(object_fields::Mana) == 13);	// 12.5 + 0.5: the carried half point completes
}

TEST_CASE("The five second rule window may end in the middle of a settled interval", "[regeneration]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeRegeneratingUnit(project, timers);
	unit->Set<int32>(object_fields::Mana, 0);
	const GameTime start = unit->GetRegenSettledAt();

	unit->NotifyManaUsed(start + 1000);
	const int32 afterCast = unit->Get<int32>(object_fields::Mana);
	CHECK(afterCast == 10);

	// The owning client extrapolates with the replicated current rate: the casting one now
	CHECK(unit->Get<float>(object_fields::PowerRegen + power_type::Mana) == Approx(15.0f));

	// 5 s at the casting rate (3/s) plus 2 s at the normal rate (10/s)
	unit->SettleRegeneration(start + 1000 + 7000);
	CHECK(unit->Get<int32>(object_fields::Mana) == afterCast + 15 + 20);
}

TEST_CASE("Health does not regenerate in combat and rage decays only out of combat", "[regeneration]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeRegeneratingUnit(project, timers);
	unit->Set<uint32>(object_fields::MaxRage, 1000);
	unit->Set<int32>(object_fields::Rage, 100);
	unit->Set<uint32>(object_fields::Health, 50);

	unit->SetInCombat(true, false);
	CHECK(unit->GetHealthRegenPerSecond() == Approx(0.0f));
	CHECK(unit->GetPowerRegenPerSecond(power_type::Rage) == Approx(0.0f));
	CHECK(unit->Get<float>(object_fields::HealthRegen) == Approx(0.0f));

	const GameTime start = unit->GetRegenSettledAt();
	unit->SettleRegeneration(start + 4000);
	CHECK(unit->Get<uint32>(object_fields::Health) == 50);
	CHECK(unit->Get<int32>(object_fields::Rage) == 100);

	unit->SetInCombat(false, false);
	CHECK(unit->Get<float>(object_fields::HealthRegen) == Approx(25.0f));	// per five seconds
	CHECK(unit->Get<float>(object_fields::PowerRegen + power_type::Rage) == Approx(-7.5f));
	const GameTime leftCombat = unit->GetRegenSettledAt();
	unit->SettleRegeneration(leftCombat + 2000);
	CHECK(unit->Get<uint32>(object_fields::Health) == 60);
	CHECK(unit->Get<int32>(object_fields::Rage) == 97);	// 1.5 per second decay
}

TEST_CASE("Settling regeneration does not by itself queue an object update", "[regeneration]")
{
	// Health and power are settled before every cost check, hit and heal. Doing that must not
	// make the unit send its values to nearby clients each time: settled values are only marked
	// as changed and go out with the next update that is sent anyway, at the latest with the sync.
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeRegeneratingUnit(project, timers);
	unit->Set<int32>(object_fields::Mana, 0);
	unit->ClearFieldChanges();
	REQUIRE_FALSE(unit->HasUnsentRegeneration());

	unit->SettleRegeneration(unit->GetRegenSettledAt() + 1000);
	CHECK(unit->Get<int32>(object_fields::Mana) == 10);
	CHECK(unit->HasUnsentRegeneration());
}

TEST_CASE("Regeneration stops moving once every resource is full", "[regeneration]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeRegeneratingUnit(project, timers);
	unit->Set<int32>(object_fields::Mana, 90);
	CHECK(unit->IsRegenerationMoving());

	unit->SettleRegeneration(unit->GetRegenSettledAt() + 60000);
	CHECK(unit->Get<int32>(object_fields::Mana) == unit->Get<int32>(object_fields::MaxMana));
	CHECK(unit->Get<uint32>(object_fields::Health) == unit->Get<uint32>(object_fields::MaxHealth));
	CHECK_FALSE(unit->IsRegenerationMoving());
}

TEST_CASE("A stopped unit regenerates nothing, and nothing is owed once it starts again", "[regeneration]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers);
	unit->Set<int32>(object_fields::Mana, 0);

	unit->SettleRegeneration(unit->GetRegenSettledAt() + 10000);
	CHECK(unit->Get<int32>(object_fields::Mana) == 0);
	CHECK_FALSE(unit->IsRegenerationMoving());
}
