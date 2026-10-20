// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// The visibility grid files a unit under the tile of its position. GameUnitS::GetPosition is the
// mover's interpolated location, though, while the stored movement snapshot only advances when the
// unit is relocated (every 500 ms while walking). OnObjectMoved took the old tile from the snapshot
// and the new tile from GetPosition, so a movement info change in the middle of a walk (a creature
// switching to run as it engages, say) filed the unit a tile ahead of its snapshot. The next move
// then looked for it in the wrong tile ("was not in its tile"), and in between value updates went
// to the watchers around the snapshot tile, some of which had never been sent the unit: their
// client dropped the session over an update for an unknown object (2026-10-20, Oakenshire).

#include "catch.hpp"

#include "../nav_mesh_tests/synthetic_nav_map.h"

#include "game_server/condition_mgr.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/trigger_handler.h"
#include "game_server/unit_mover.h"
#include "game_server/world/tile_subscriber.h"
#include "game_server/world/universe.h"
#include "game_server/world/visibility_grid.h"
#include "game_server/world/visibility_tile.h"
#include "game_server/world/world_instance.h"
#include "game_server/world/world_instance_manager.h"
#include "base/id_generator.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "asio/io_service.hpp"

#include <chrono>
#include <memory>
#include <thread>

using namespace mmo;

namespace
{
	struct NullTriggerHandler final : ITriggerHandler
	{
		void ExecuteTrigger(const proto::TriggerEntry&, TriggerContext, uint32, bool) override {}
	};

	/// A watcher that counts what it is sent and claims to know only what it is told it knows.
	struct CountingSubscriber final : TileSubscriber
	{
		explicit CountingSubscriber(GameUnitS& unit)
			: unit(unit)
		{
		}

		GameUnitS& GetGameUnit() const override { return unit; }
		void NotifyObjectsUpdated(const std::vector<GameObjectS*>& objects) override { updates += objects.size(); }
		void NotifyObjectsSpawned(const std::vector<GameObjectS*>&) override {}
		void NotifyObjectsDespawned(const std::vector<GameObjectS*>&) override {}
		GameTime ClientToServerTime(const GameTime clientTimestamp) const override { return clientTimestamp; }
		GameTime ServerToClientTime(const GameTime serverTimestamp) const override { return serverTimestamp; }
		bool HasReceivedTimeSyncResponse() const override { return true; }
		void SendPacket(game::Protocol::OutgoingPacket&, const std::vector<char>&, bool) override {}
		bool IsObjectKnown(const uint64 guid) const override { return knownGuid != 0 && guid == knownGuid; }

		GameUnitS& unit;
		uint64 knownGuid = 0;
		size_t updates = 0;
	};

	struct TileSnapshotFixture
	{
		/// One flat nav page, so creatures can actually be given a path to walk.
		test_nav::SyntheticNavMap navFiles{ { { 32, 32 } } };

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

		TileSnapshotFixture()
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

			auto* map = project.maps.add(0);
			map->set_name("Tile Snapshot Test");
			map->set_directory(test_nav::SyntheticNavMap::Name);

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

		/// The first point east of the page centre at which the visibility tile changes.
		Vector3 TileBorderEastOfCentre() const
		{
			const Vector3 centre = test_nav::PageCenter(32, 32);
			const TileIndex2D centreTile = TileOf(centre);

			Vector3 probe = centre;
			while (TileOf(probe) == centreTile)
			{
				probe.x += 0.05f;
			}

			return probe;
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

		bool IsFiledUnderSnapshotTile(GameObjectS& object) const
		{
			const VisibilityTile* tile = world->GetGrid().GetTile(TileOf(object.GetMovementInfo().position));
			return tile && tile->GetGameObjects().contains(&object);
		}

		/// Walks the creature across the tile border and waits until its interpolated position is in
		/// the next tile while its movement snapshot still lies in the tile it started in.
		bool WalkUntilPositionAndSnapshotDisagree(GameCreatureS& creature, const Vector3& border)
		{
			if (!creature.GetMover().MoveTo(border + Vector3(10.0f, 0.0f, 0.0f), 4.0f, 0.0f))
			{
				return false;
			}

			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
			while (std::chrono::steady_clock::now() < deadline)
			{
				if (TileOf(creature.GetPosition()) != TileOf(creature.GetMovementInfo().position))
				{
					return true;
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}

			return false;
		}

		~TileSnapshotFixture()
		{
			io.stop();
		}
	};
}

TEST_CASE_METHOD(TileSnapshotFixture, "A movement info change mid-walk keeps a creature in the tile of its snapshot", "[world][visibility]")
{
	const Vector3 border = TileBorderEastOfCentre();
	auto creature = Spawn(border - Vector3(0.3f, 0.0f, 0.0f));
	REQUIRE(IsFiledUnderSnapshotTile(*creature));

	REQUIRE(WalkUntilPositionAndSnapshotDisagree(*creature, border));

	// What SetMovementMode does when a walking creature starts to run: apply a copy of the
	// snapshot with other flags, while the mover is still under way.
	MovementInfo info = creature->GetMovementInfo();
	creature->ApplyMovementInfo(info);

	CHECK(CountTilesHolding(*creature) == 1);
	CHECK(IsFiledUnderSnapshotTile(*creature));

	// The next periodic relocation moves it on from where the grid has it.
	creature->Relocate(creature->GetPosition(), creature->GetFacing());

	CHECK(CountTilesHolding(*creature) == 1);
	CHECK(IsFiledUnderSnapshotTile(*creature));
}

TEST_CASE_METHOD(TileSnapshotFixture, "Value updates go only to watchers whose client knows the object", "[world][visibility]")
{
	const Vector3 position = test_nav::PageCenter(32, 32);
	auto creature = Spawn(position);
	auto watcherUnit = Spawn(position + Vector3(2.0f, 0.0f, 0.0f));

	CountingSubscriber watcher(*watcherUnit);
	VisibilityTile& tile = world->GetGrid().RequireTile(TileOf(position));
	tile.GetWatchers().add(&watcher);

	SECTION("A watcher that was never sent the creature gets no update for it")
	{
		creature->Set<uint32>(object_fields::Health, 50);
		world->FlushObjectUpdate(creature->GetGuid());

		CHECK(watcher.updates == 0);
	}

	SECTION("A watcher that knows the creature gets its update")
	{
		watcher.knownGuid = creature->GetGuid();

		creature->Set<uint32>(object_fields::Health, 50);
		world->FlushObjectUpdate(creature->GetGuid());

		CHECK(watcher.updates == 1);
	}

	SECTION("A watcher always gets updates for its own unit")
	{
		watcherUnit->Set<uint32>(object_fields::Health, 50);
		world->FlushObjectUpdate(watcherUnit->GetGuid());

		CHECK(watcher.updates == 1);
	}

	tile.GetWatchers().remove(&watcher);
}
