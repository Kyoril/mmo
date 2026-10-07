// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/world/tiled_unit_finder.h"
#include "game_server/world/tiled_unit_finder_tile.h"
#include "game_server/world/unit_watcher.h"
#include "test_unit_factory.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

#include <set>

using namespace mmo;

// An idle creature watches a circle around itself and re-evaluates every unit in it when it
// moves (SetShape). The watcher used to stop scanning a tile at the first callback returning
// true - and the creature's callback returns true for every unit, starting with itself. A
// player standing still in the creature's tile was then never checked for aggro; only the
// player's own movement (which ignores the return value) reached the callback.

namespace
{
	constexpr float FinderTileWidth = 33.3333f;
}

TEST_CASE("UnitWatcher reports every unit of a tile on start and on shape updates", "[unit_watcher]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	TiledUnitFinder finder(FinderTileWidth);

	const auto creature = test::MakeUnit(project, timers);
	creature->Relocate(Vector3(1.0f, 0.0f, 1.0f), Radian(0.0f));
	finder.AddUnit(*creature);

	const auto player = test::MakeUnit(project, timers);
	player->Relocate(Vector3(5.0f, 0.0f, 5.0f), Radian(0.0f));
	finder.AddUnit(*player);

	std::set<GameUnitS*> seen;
	auto watcher = finder.WatchUnits(Circle(1.0f, 1.0f, 40.0f), [&seen](GameUnitS& unit, bool isVisible) -> bool
	{
		if (isVisible)
		{
			seen.insert(&unit);
		}

		// Same as CreatureAIIdleState's watcher callback for uninteresting units.
		return true;
	});

	watcher->Start();
	CHECK(seen.count(creature.get()) == 1);
	CHECK(seen.count(player.get()) == 1);

	// The creature walks a bit while the player stands still.
	seen.clear();
	watcher->SetShape(Circle(2.0f, 2.0f, 40.0f));
	CHECK(seen.count(creature.get()) == 1);
	CHECK(seen.count(player.get()) == 1);

	watcher.reset();
	finder.RemoveUnit(*creature);
	finder.RemoveUnit(*player);
}
