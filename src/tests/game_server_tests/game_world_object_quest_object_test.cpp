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

// A road lantern the player is asked to light must light up, not vanish from its post. A quest
// object whose entry names a used display (data[1]) therefore stays in the world and switches to
// that display until it resets, instead of despawning.
TEST_CASE("GameWorldObjectS - a quest object with a used display switches to it instead of vanishing", "[world_object][quest_object]")
{
	asio::io_service io;
	TimerQueue timers{ io };

	constexpr uint32 unlitDisplay = 10;
	constexpr uint32 litDisplay = 11;

	proto::Project project;
	auto* entry = MakeObjectEntry(project, game_world_object_type::QuestObject);
	entry->set_displayid(unlitDisplay);
	entry->add_data(0);				// data[0]: lock type, none
	entry->add_data(litDisplay);	// data[1]: display while used

	const auto player = MakePlayer(project, timers);

	auto lantern = std::make_shared<GameWorldObjectS>(project, *entry);
	lantern->Initialize();
	REQUIRE(lantern->Get<uint32>(object_fields::ObjectDisplayId) == unlitDisplay);
	REQUIRE(lantern->IsUsable(*player));

	lantern->Use(*player);

	// Lit, and still standing: only the display changed, and it cannot be used a second time.
	CHECK(lantern->Get<uint32>(object_fields::ObjectDisplayId) == litDisplay);
	CHECK((lantern->Get<uint32>(object_fields::ObjectFlags) & world_object_flags::Disabled) != 0);
	CHECK_FALSE(lantern->IsUsable(*player));

	// The reset puts it back on the post unlit, so the next player can light it again.
	lantern->ResetAfterUse();
	CHECK(lantern->Get<uint32>(object_fields::ObjectDisplayId) == unlitDisplay);
	CHECK(lantern->IsUsable(*player));
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
