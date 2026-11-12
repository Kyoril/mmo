// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// A quest object is consumed by its use: one spawn must not be clicked to full quest credit. It
// used to remove itself from the world inside Use(), while the Open spell's effect that called
// Use() still held it - a use-after-free that took the world server down on the first quest
// object any content placed. It is now disabled at once and leaves the world a tick later.

#include "catch.hpp"

#include "game_server/objects/game_player_s.h"
#include "game_server/objects/game_world_object_s.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "asio/io_service.hpp"

#include <memory>

using namespace mmo;

namespace
{
	proto::ObjectEntry* MakeObjectEntry(proto::Project& project, const uint32 type)
	{
		auto* entry = project.objects.add();
		entry->set_name("Test Object");
		entry->set_type(type);
		entry->set_displayid(0);
		entry->set_factionid(0);
		return entry;
	}

	/// Helper: build a minimal shared GamePlayerS (mirrors game_world_object_door_test.cpp).
	std::shared_ptr<GamePlayerS> MakePlayer(proto::Project& project, TimerQueue& timers)
	{
		auto* cls = project.classes.getById(1);
		if (!cls)
		{
			cls = project.classes.add(1);
		}

		if (cls)
		{
			cls->set_powertype(proto::ClassEntry_PowerType_MANA);
			while (cls->levelbasevalues_size() < 2)
			{
				auto* lbv = cls->add_levelbasevalues();
				lbv->set_health(100);
				lbv->set_mana(100);
				lbv->add_attributes(10);
				lbv->add_attributes(10);
				lbv->add_attributes(10);
				lbv->add_attributes(10);
				lbv->add_attributes(10);
			}
		}

		auto unit = std::make_shared<GamePlayerS>(project, timers);
		unit->Initialize();
		if (cls)
		{
			unit->SetClass(*cls);
		}
		unit->SetLevel(1);
		return unit;
	}
}

TEST_CASE("GameWorldObjectS - a used quest object is disabled at once and stays valid", "[world_object][quest_object]")
{
	asio::io_service io;
	TimerQueue timers{ io };

	proto::Project project;
	const auto* entry = MakeObjectEntry(project, game_world_object_type::QuestObject);
	const auto player = MakePlayer(project, timers);

	auto questObject = std::make_shared<GameWorldObjectS>(project, *entry);
	questObject->Initialize();
	REQUIRE(questObject->IsUsable(*player));

	questObject->Use(*player);

	// The caller (the Open spell's effect) still holds the object and reads from it after Use().
	CHECK(questObject->GetType() == GameWorldObjectType::QuestObject);
	CHECK(questObject->GetPostUnlockLockType() == 0);

	// Consumed: a second use in the same tick must not be possible.
	CHECK((questObject->Get<uint32>(object_fields::ObjectFlags) & world_object_flags::Disabled) != 0);
	CHECK_FALSE(questObject->IsUsable(*player));
}

TEST_CASE("GameWorldObjectS - other object types stay usable after a use", "[world_object][quest_object]")
{
	asio::io_service io;
	TimerQueue timers{ io };

	proto::Project project;
	const auto* entry = MakeObjectEntry(project, game_world_object_type::Door);
	const auto player = MakePlayer(project, timers);

	auto door = std::make_shared<GameWorldObjectS>(project, *entry);
	door->Initialize();
	door->Use(*player);

	CHECK((door->Get<uint32>(object_fields::ObjectFlags) & world_object_flags::Disabled) == 0);
	CHECK(door->IsUsable(*player));
}
