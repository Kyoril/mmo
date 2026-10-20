// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// RemoveGameObject takes an object out of the visibility tile its current position maps to. An
// object that is in some other tile - moved without the grid being told, or moved off the grid,
// for which removal used to skip the tile cleanup altogether - stayed there after it was freed,
// and the next unit whose view swept that tile read freed memory (world server crash in
// Player::OnTileChangePending, 2026-10-20). Removal must leave no tile holding the object.

#include "catch.hpp"

#include "game_server/condition_mgr.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/trigger_handler.h"
#include "game_server/world/universe.h"
#include "game_server/world/visibility_grid.h"
#include "game_server/world/visibility_tile.h"
#include "game_server/world/world_instance.h"
#include "game_server/world/world_instance_manager.h"
#include "base/id_generator.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "asio/io_service.hpp"

#include <memory>

using namespace mmo;

namespace
{
	struct NullTriggerHandler final : ITriggerHandler
	{
		void ExecuteTrigger(const proto::TriggerEntry&, TriggerContext, uint32, bool) override {}
	};

	struct TileCleanupFixture
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;
		Universe universe{ io, timers };
		IdGenerator<uint64> objectIds{ 1 };
		NullTriggerHandler triggerHandler;
		std::unique_ptr<ConditionMgr> conditionMgr;
		std::unique_ptr<WorldInstanceManager> manager;
		WorldInstance* world = nullptr;
		proto::UnitEntry* unitEntry = nullptr;

		TileCleanupFixture()
		{
			auto* factionTemplate = project.factionTemplates.add(1);
			factionTemplate->set_name("Test");
			factionTemplate->set_flags(0);
			factionTemplate->set_faction(1);

			unitEntry = project.units.add(1);
			unitEntry->set_name("Wanderer");
			unitEntry->set_minlevel(1);
			unitEntry->set_maxlevel(1);
			unitEntry->set_factiontemplate(1);
			unitEntry->set_malemodel(0);
			unitEntry->set_femalemodel(0);
			unitEntry->set_type(0);
			unitEntry->set_family(0);
			unitEntry->set_minlevelhealth(100);
			unitEntry->set_maxlevelhealth(100);

			// No map files exist for this directory: no nav mesh, no collision, LOS is unblocked.
			auto* map = project.maps.add(0);
			map->set_name("Tile Cleanup Test");
			map->set_directory("Worlds/__tile_cleanup_test__/__tile_cleanup_test__");

			conditionMgr = std::make_unique<ConditionMgr>(project.conditions);
			manager = std::make_unique<WorldInstanceManager>(io, universe, project, objectIds, triggerHandler, *conditionMgr);
			world = &manager->CreateInstance(0);
		}

		std::shared_ptr<GameCreatureS> Spawn(const Vector3& position)
		{
			auto creature = world->CreateCreature(*unitEntry, position, 0.0f, 0.0f);
			world->AddGameObject(*creature);
			return creature;
		}

		TileIndex2D TileOf(const Vector3& position) const
		{
			TileIndex2D index;
			world->GetGrid().GetTilePosition(position, index[0], index[1]);
			return index;
		}

		size_t CountTilesHolding(GameObjectS& object) const
		{
			size_t count = 0;
			world->GetGrid().ForEachExistingTile([&object, &count](VisibilityTile& tile)
			{
				if (tile.GetGameObjects().contains(&object))
				{
					++count;
				}
			});
			return count;
		}

		~TileCleanupFixture()
		{
			io.stop();
		}
	};
}

TEST_CASE_METHOD(TileCleanupFixture, "A removed object leaves its tile", "[world][visibility]")
{
	auto unit = Spawn(Vector3(0.0f, 0.0f, 0.0f));
	REQUIRE(CountTilesHolding(*unit) == 1);

	world->RemoveGameObject(*unit);

	CHECK(CountTilesHolding(*unit) == 0);
}

TEST_CASE_METHOD(TileCleanupFixture, "A removed object filed under the wrong tile leaves that tile too", "[world][visibility]")
{
	auto unit = Spawn(Vector3(0.0f, 0.0f, 0.0f));

	// Simulate an object that moved without the grid being told: it sits in a tile far from the
	// one its position maps to.
	const TileIndex2D home = TileOf(unit->GetPosition());
	const TileIndex2D elsewhere = TileOf(Vector3(500.0f, 0.0f, 500.0f));
	REQUIRE(home != elsewhere);

	world->GetGrid().GetTile(home)->GetGameObjects().remove(unit.get());
	world->GetGrid().GetTile(elsewhere)->GetGameObjects().add(unit.get());

	world->RemoveGameObject(*unit);

	CHECK(CountTilesHolding(*unit) == 0);
}

TEST_CASE_METHOD(TileCleanupFixture, "A removed object that moved off the grid leaves its edge tile", "[world][visibility]")
{
	auto unit = Spawn(Vector3(0.0f, 0.0f, 0.0f));

	// Moving off the grid files the unit under the nearest edge tile.
	unit->Relocate(Vector3(1.0e7f, 0.0f, 0.0f), Radian(0.0f));
	REQUIRE(CountTilesHolding(*unit) == 1);

	world->RemoveGameObject(*unit);

	CHECK(CountTilesHolding(*unit) == 0);
}

TEST_CASE_METHOD(TileCleanupFixture, "A creature moved before it is added to the world is not filed in a tile", "[world][visibility]")
{
	// CreateCreature points the creature at the world before AddGameObject files it. A move in
	// between used to file it in a tile (and spawn it to nearby players) ahead of the add.
	auto creature = world->CreateCreature(*unitEntry, Vector3(0.0f, 0.0f, 0.0f), 0.0f, 0.0f);
	creature->Relocate(Vector3(500.0f, 0.0f, 500.0f), Radian(0.0f));

	CHECK(CountTilesHolding(*creature) == 0);

	world->AddGameObject(*creature);
	CHECK(CountTilesHolding(*creature) == 1);

	world->RemoveGameObject(*creature);
	CHECK(CountTilesHolding(*creature) == 0);
}

TEST_CASE_METHOD(TileCleanupFixture, "An object filed under the wrong tile ends up in exactly one tile when it moves on", "[world][visibility]")
{
	auto unit = Spawn(Vector3(0.0f, 0.0f, 0.0f));

	const TileIndex2D home = TileOf(unit->GetPosition());
	const TileIndex2D elsewhere = TileOf(Vector3(500.0f, 0.0f, 500.0f));
	REQUIRE(home != elsewhere);

	world->GetGrid().GetTile(home)->GetGameObjects().remove(unit.get());
	world->GetGrid().GetTile(elsewhere)->GetGameObjects().add(unit.get());

	unit->Relocate(Vector3(-500.0f, 0.0f, -500.0f), Radian(0.0f));

	CHECK(CountTilesHolding(*unit) == 1);
	CHECK(world->GetGrid().GetTile(TileOf(unit->GetPosition()))->GetGameObjects().contains(unit.get()));
}

TEST_CASE_METHOD(TileCleanupFixture, "Adding an object twice files it once", "[world][visibility]")
{
	auto unit = Spawn(Vector3(0.0f, 0.0f, 0.0f));

	world->AddGameObject(*unit);

	CHECK(CountTilesHolding(*unit) == 1);
}
