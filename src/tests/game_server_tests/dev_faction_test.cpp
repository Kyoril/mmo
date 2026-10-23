// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#include "catch.hpp"
#include "game/dev_faction.h"
#include "game/player_faction.h"
#include "test_unit_factory.h"
#include "asio/io_service.hpp"

using namespace mmo;

namespace
{
	struct FactionFixture
	{
		asio::io_service io;
		TimerQueue timers{io};
		proto::Project project;
		std::shared_ptr<GamePlayerS> a, b;

		FactionFixture()
		{
			// Existing player factions: Human (template 0, faction 9) and Orc (template 7, faction 7).
			for (uint32 alias = 0; alias < DevFactionTemplates.size(); ++alias)
			{
				auto* faction = project.factionTemplates.add(DevFactionTemplates[alias]);
				faction->set_faction(alias == 0 ? 9 : 7);
				faction->add_enemies(alias == 0 ? 7 : 9);
			}
			a = test::MakeUnit(project, timers);
			b = test::MakeUnit(project, timers);
			a->Set<uint64>(object_fields::Guid, 1);
			b->Set<uint64>(object_fields::Guid, 2);
			for (auto unit : {a, b})
			{
				unit->Set<uint32>(object_fields::Health, 1000);
				unit->Set<uint32>(object_fields::MaxHealth, 1000);
				unit->Set<uint32>(object_fields::FactionTemplate, DevFactionTemplates[0]);
			}
		}
	};
}

TEST_CASE("Faction aliases are mutually hostile and changes invalidate the faction cache", "[faction]")
{
	FactionFixture f;
	CHECK(f.a->UnitIsFriendly(*f.b));
	CHECK_FALSE(f.a->UnitIsEnemy(*f.b));
	f.b->Set<uint32>(object_fields::FactionTemplate, DevFactionTemplates[1]);
	CHECK(f.a->UnitIsEnemy(*f.b));
	CHECK(f.b->UnitIsEnemy(*f.a));
	CHECK_FALSE(f.a->UnitIsFriendly(*f.b));
	CHECK_FALSE(f.b->UnitIsFriendly(*f.a));
	f.a->Set<uint32>(object_fields::FactionTemplate, DevFactionTemplates[1]);
	CHECK(f.a->UnitIsFriendly(*f.b));
	CHECK(f.b->UnitIsFriendly(*f.a));
	CHECK_FALSE(f.a->UnitIsEnemy(*f.b));
}

TEST_CASE("Player social permissions require a known matching faction regardless of friendship or duel state", "[faction]")
{
	FactionFixture f;
	const auto* first = f.a->GetFactionTemplate();
	const auto* second = f.b->GetFactionTemplate();
	CHECK(CanPlayerFactionsInteract(first, second));
	CHECK_FALSE(CanPlayerFactionsInteract(first, static_cast<const proto::FactionTemplateEntry*>(nullptr)));
	CHECK_FALSE(CanPlayerFactionsInteract(static_cast<const proto::FactionTemplateEntry*>(nullptr), second));
	f.b->Set<uint32>(object_fields::FactionTemplate, DevFactionTemplates[1]);
	CHECK_FALSE(CanPlayerFactionsInteract(first, f.b->GetFactionTemplate()));
	CHECK_FALSE(CanPlayerFactionsInteract(f.b->GetFactionTemplate(), first));
	auto* sameFaction = f.project.factionTemplates.add(20);
	sameFaction->set_faction(first->faction());
	CHECK(CanPlayerFactionsInteract(first, sameFaction));
	// A duel's temporary hostility must not change faction-based social permissions.
	f.b->Set<uint32>(object_fields::FactionTemplate, DevFactionTemplates[0]);
	f.a->SetDuelPartner(f.b->GetGuid(), true);
	CHECK(f.a->UnitIsEnemy(*f.b));
	CHECK(CanPlayerFactionsInteract(f.a->GetFactionTemplate(), f.b->GetFactionTemplate()));
}

TEST_CASE("Opposing faction combat reaches zero health and raises death instead of duel defeat", "[faction]")
{
	FactionFixture f;
	f.b->Set<uint32>(object_fields::FactionTemplate, DevFactionTemplates[1]);
	SECTION("Alias 1 defeats Alias 2") {}
	SECTION("Alias 2 defeats Alias 1") { std::swap(f.a, f.b); }
	bool died = false, duelDefeat = false;
	scoped_connection death{f.b->killed.connect([&](GameUnitS* killer) { died = killer == f.a.get(); })};
	scoped_connection defeat{f.b->duelDefeated.connect([&]() { duelDefeat = true; })};
	CHECK(f.b->Damage(995, spell_school::Normal, f.a.get(), damage_type::AttackSwing) == 995);
	CHECK(f.b->GetHealth() == 5); // Already below the duel's one-percent floor.
	CHECK(f.b->IsAlive());
	CHECK(f.b->Damage(100, spell_school::Fire, f.a.get(), damage_type::Periodic) == 5);
	CHECK(f.b->GetHealth() == 0);
	CHECK_FALSE(f.b->IsAlive());
	CHECK(died);
	CHECK_FALSE(duelDefeat);
	CHECK(f.b->GetStandState() != unit_stand_state::Kneel);
}
