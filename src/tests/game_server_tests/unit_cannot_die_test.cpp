// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/objects/game_unit_s.h"
#include "game/object_type_id.h"
#include "game/spell.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "asio/io_service.hpp"

#include <memory>

using namespace mmo;

namespace
{
	/// A unit that refuses to die, like a training dummy in combat.
	class ImmortalUnit final : public GameUnitS
	{
	public:
		using GameUnitS::GameUnitS;

		bool CanDie() const override { return m_canDie; }

		const String& GetName() const override
		{
			static const String name = "Immortal";
			return name;
		}

		bool m_canDie = false;
	};

	std::shared_ptr<ImmortalUnit> MakeUnit(proto::Project& project, TimerQueue& timers)
	{
		auto unit = std::make_shared<ImmortalUnit>(project, timers);
		unit->Initialize();
		unit->Set<uint32>(object_fields::MaxHealth, 1000);
		unit->Set<uint32>(object_fields::Health, 1000);
		return unit;
	}
}

TEST_CASE("A unit that can not die is refilled instead of killed", "[cannot_die]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	const auto unit = MakeUnit(project, timers);

	int32 kills = 0;
	scoped_connection onKilled{ unit->killed.connect([&kills](GameUnitS*) { ++kills; }) };

	SECTION("Lethal damage reports the damage, refills the unit and does not kill it")
	{
		unit->Set<uint32>(object_fields::Health, 300);
		CHECK(unit->Damage(5000, spell_school::Normal, nullptr, damage_type::AttackSwing) == 300);
		CHECK(kills == 0);
		CHECK(unit->IsAlive());
		CHECK(unit->GetHealth() == 1000);
	}

	SECTION("Kill refills the unit and does not kill it")
	{
		unit->Set<uint32>(object_fields::Health, 300);
		unit->Kill(nullptr);
		CHECK(kills == 0);
		CHECK(unit->IsAlive());
		CHECK(unit->GetHealth() == 1000);
	}

	SECTION("A unit that can die still dies")
	{
		unit->m_canDie = true;
		unit->Damage(5000, spell_school::Normal, nullptr, damage_type::AttackSwing);
		CHECK(kills == 1);
		CHECK_FALSE(unit->IsAlive());
	}
}
