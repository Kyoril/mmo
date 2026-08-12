// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/objects/game_player_s.h"
#include "game/spell.h"
#include "game/object_type_id.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/classes.pb.h"
#include "asio/io_service.hpp"

#include <memory>

using namespace mmo;

namespace
{
	/// Build a minimal GamePlayerS with a class entry so SetLevel() works.
	std::shared_ptr<GamePlayerS> MakeUnit(proto::Project& project, TimerQueue& timers, uint32 level = 1)
	{
		// Use class id 1 -- only add once per project instance.
		auto* cls = project.classes.getById(1);
		if (!cls)
		{
			cls = project.classes.add(1);
			if (cls)
			{
				cls->set_powertype(proto::ClassEntry_PowerType_MANA);
				for (uint32 i = 0; i < level + 1; ++i)
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
		}

		auto unit = std::make_shared<GamePlayerS>(project, timers);
		unit->Initialize();
		if (cls) { unit->SetClass(*cls); }
		unit->SetLevel(level);
		return unit;
	}
}

TEST_CASE("Damage reduces health and returns non-zero when godmode is off", "[godmode]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	std::shared_ptr<GamePlayerS> unit = MakeUnit(project, timers);

	unit->Set<uint32>(object_fields::Health, 1000);
	unit->Set<uint32>(object_fields::MaxHealth, 1000);

	REQUIRE_FALSE(unit->IsGodmode());

	const uint32 dealt = unit->Damage(150, spell_school::Normal, nullptr, damage_type::AttackSwing);

	CHECK(dealt == 150u);
	CHECK(unit->GetHealth() == 850u);
}

TEST_CASE("Damage is fully absorbed and health is unchanged when godmode is on", "[godmode]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	std::shared_ptr<GamePlayerS> unit = MakeUnit(project, timers);

	unit->Set<uint32>(object_fields::Health, 1000);
	unit->Set<uint32>(object_fields::MaxHealth, 1000);

	unit->SetGodmode(true);
	REQUIRE(unit->IsGodmode());

	const uint32 dealt = unit->Damage(150, spell_school::Normal, nullptr, damage_type::AttackSwing);

	CHECK(dealt == 0u);
	CHECK(unit->GetHealth() == 1000u);
}

TEST_CASE("Turning godmode back off restores normal damage", "[godmode]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	std::shared_ptr<GamePlayerS> unit = MakeUnit(project, timers);

	unit->Set<uint32>(object_fields::Health, 1000);
	unit->Set<uint32>(object_fields::MaxHealth, 1000);

	// Enable godmode, take a hit -- confirm it is absorbed.
	unit->SetGodmode(true);
	const uint32 absorbed = unit->Damage(150, spell_school::Normal, nullptr, damage_type::AttackSwing);
	CHECK(absorbed == 0u);
	CHECK(unit->GetHealth() == 1000u);

	// Disable godmode again -- damage must resume normally. This is the path the E2E
	// scenario relies on: it disables godmode before asserting the boss actually dies,
	// so a silently-broken disable would make that assertion pass while proving nothing.
	unit->SetGodmode(false);
	REQUIRE_FALSE(unit->IsGodmode());

	const uint32 dealt = unit->Damage(150, spell_school::Normal, nullptr, damage_type::AttackSwing);

	CHECK(dealt == 150u);
	CHECK(unit->GetHealth() == 850u);
}
