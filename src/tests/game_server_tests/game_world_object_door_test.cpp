// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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
	/// Helper: creates a door ObjectEntry inside the given project.
	proto::ObjectEntry* MakeDoorEntry(proto::Project& project, const uint32 flags = 0, const uint32 autoCloseMs = 0)
	{
		auto* entry = project.objects.add();
		entry->set_name("Test Door");
		entry->set_type(game_world_object_type::Door);
		entry->set_flags(flags);
		entry->add_data(0);            // data[0] — lock type
		entry->add_data(0);            // data[1] — post-unlock lock type
		entry->add_data(autoCloseMs);  // data[2] — auto close time in ms
		return entry;
	}

	/// Helper: build a minimal shared GamePlayerS (mirrors stealth_visibility_test.cpp).
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
		unit->SetLevel(1);
		return unit;
	}
}

TEST_CASE("GameWorldObjectS - SetObjectState fires stateChanged once per actual change", "[world_object][door]")
{
	proto::Project project;
	const auto* entry = MakeDoorEntry(project);

	auto door = std::make_shared<GameWorldObjectS>(project, *entry);
	door->Initialize();

	uint32 signalCount = 0;
	uint32 lastState = 0xffffffff;
	scoped_connection connection;
	connection = door->stateChanged.connect([&signalCount, &lastState](GameWorldObjectS&, const uint32 newState)
	{
		++signalCount;
		lastState = newState;
	});

	REQUIRE_FALSE(door->IsOpen());

	door->SetObjectState(1);
	REQUIRE(signalCount == 1);
	REQUIRE(lastState == 1);
	REQUIRE(door->IsOpen());

	// Setting the same state again is a no-op and must not re-fire.
	door->SetObjectState(1);
	REQUIRE(signalCount == 1);

	door->SetObjectState(0);
	REQUIRE(signalCount == 2);
	REQUIRE(lastState == 0);
	REQUIRE_FALSE(door->IsOpen());
}

TEST_CASE("GameWorldObjectS - entry flags are applied to the ObjectFlags field", "[world_object][door]")
{
	proto::Project project;
	const auto* entry = MakeDoorEntry(project, world_object_flags::NotInteractable);

	auto door = std::make_shared<GameWorldObjectS>(project, *entry);
	door->Initialize();

	REQUIRE((door->Get<uint32>(object_fields::ObjectFlags) & world_object_flags::NotInteractable) != 0);
}

TEST_CASE("GameWorldObjectS - NotInteractable doors are never usable by players", "[world_object][door]")
{
	asio::io_service io;
	TimerQueue timers{ io };

	proto::Project project;
	const auto* interactableEntry = MakeDoorEntry(project);
	const auto* triggerOnlyEntry = MakeDoorEntry(project, world_object_flags::NotInteractable);

	const auto player = MakePlayer(project, timers);

	auto normalDoor = std::make_shared<GameWorldObjectS>(project, *interactableEntry);
	normalDoor->Initialize();
	REQUIRE(normalDoor->IsUsable(*player));

	auto bossDoor = std::make_shared<GameWorldObjectS>(project, *triggerOnlyEntry);
	bossDoor->Initialize();
	REQUIRE_FALSE(bossDoor->IsUsable(*player));

	// Trigger-driven state changes still work on trigger-only doors.
	bossDoor->SetObjectState(1);
	REQUIRE(bossDoor->IsOpen());
}

TEST_CASE("GameWorldObjectS - auto close time is read from data[2] for doors only", "[world_object][door]")
{
	proto::Project project;
	const auto* doorEntry = MakeDoorEntry(project, 0, 5000);

	auto door = std::make_shared<GameWorldObjectS>(project, *doorEntry);
	door->Initialize();
	REQUIRE(door->GetAutoCloseTimeMs() == 5000);

	// Chests never auto close, even with data[2] set.
	auto* chestEntry = project.objects.add();
	chestEntry->set_name("Test Chest");
	chestEntry->set_type(game_world_object_type::Chest);
	chestEntry->add_data(0);
	chestEntry->add_data(0);
	chestEntry->add_data(5000);

	auto chest = std::make_shared<GameWorldObjectS>(project, *chestEntry);
	chest->Initialize();
	REQUIRE(chest->GetAutoCloseTimeMs() == 0);
}
