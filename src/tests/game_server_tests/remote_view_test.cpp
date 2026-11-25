// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// A RemoteView gives a tile subscriber a second point of view away from its own unit: the camera of
// a cutscene. These tests pin down what the subscriber's client is told while the view travels:
// what comes into sight of the camera is spawned, what leaves both views is despawned, nothing is
// told twice where the views overlap, and the subscriber's own unit never leaves its client.

#include "catch.hpp"

#include "../nav_mesh_tests/synthetic_nav_map.h"

#include "game_server/condition_mgr.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/trigger_handler.h"
#include "game_server/world/each_tile_in_sight.h"
#include "game_server/world/remote_view.h"
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

#include <memory>
#include <set>

using namespace mmo;

namespace
{
	struct NullTriggerHandler final : ITriggerHandler
	{
		void ExecuteTrigger(const proto::TriggerEntry&, TriggerContext, uint32, bool) override {}
	};

	/// Stands in for a player's client: remembers which objects it was sent and counts every
	/// spawn, despawn and broadcast packet, so a test can tell "told once" from "told twice".
	struct RecordingClient final : TileSubscriber
	{
		explicit RecordingClient(GameUnitS& unit)
			: unit(unit)
		{
			// A client always knows its own character
			known.insert(unit.GetGuid());
		}

		GameUnitS& GetGameUnit() const override { return unit; }
		void NotifyObjectsUpdated(const std::vector<GameObjectS*>&) override {}

		void NotifyObjectsSpawned(const std::vector<GameObjectS*>& objects, game::spawn_reason::Type) override
		{
			for (const GameObjectS* object : objects)
			{
				if (!known.insert(object->GetGuid()).second)
				{
					++doubleSpawns;
				}
				++spawns;
			}
		}

		void NotifyObjectsDespawned(const std::vector<GameObjectS*>& objects, game::despawn_reason::Type) override
		{
			for (const GameObjectS* object : objects)
			{
				if (known.erase(object->GetGuid()) == 0)
				{
					++unknownDespawns;
				}
				if (object->GetGuid() == unit.GetGuid())
				{
					++ownDespawns;
				}
				++despawns;
			}
		}

		GameTime ClientToServerTime(const GameTime clientTimestamp) const override { return clientTimestamp; }
		GameTime ServerToClientTime(const GameTime serverTimestamp) const override { return serverTimestamp; }
		bool HasReceivedTimeSyncResponse() const override { return true; }
		void SendPacket(game::Protocol::OutgoingPacket&, const std::vector<char>&, bool) override { ++packets; }
		bool IsObjectKnown(const uint64 guid) const override { return known.contains(guid); }

		bool Knows(const GameObjectS& object) const { return known.contains(object.GetGuid()); }

		GameUnitS& unit;
		std::set<uint64> known;
		size_t spawns = 0;
		size_t despawns = 0;
		size_t doubleSpawns = 0;
		size_t unknownDespawns = 0;
		size_t ownDespawns = 0;
		size_t packets = 0;
	};

	struct RemoteViewFixture
	{
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

		/// Where the client's own unit stands.
		// Off the page centre, which lies exactly on a tile border
		const Vector3 home = test_nav::PageCenter(32, 32) + Vector3(10.0f, 0.0f, 10.0f);

		RemoteViewFixture()
		{
			auto* factionTemplate = project.factionTemplates.add(1);
			factionTemplate->set_name("Test");
			factionTemplate->set_flags(0);
			factionTemplate->set_faction(1);

			unitEntry = project.units.add(1);
			unitEntry->set_name("Bystander");
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
			map->set_name("Remote View Test");
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

		/// A position the given number of visibility tiles away from home along the x axis.
		Vector3 TilesFromHome(const int tiles) const
		{
			const Vector3 position = home + Vector3(static_cast<float>(tiles) * 33.34f, 0.0f, 0.0f);
			REQUIRE(std::abs(TileOf(position).x() - TileOf(home).x()) == std::abs(tiles));
			REQUIRE(TileOf(position).y() == TileOf(home).y());
			return position;
		}

		/// Subscribes the client at the tile of its unit and sends it what is in sight, as a player's
		/// connection does when its character enters the world.
		void Subscribe(RecordingClient& client)
		{
			VisibilityTile& tile = world->GetGrid().RequireTile(TileOf(client.unit.GetPosition()));
			tile.GetWatchers().add(&client);

			for (VisibilityTile* inSight : GetTilesInSight(world->GetGrid(), tile.GetPosition()))
			{
				std::vector<GameObjectS*> objects;
				for (GameObjectS* object : inSight->GetGameObjects())
				{
					if (!client.IsObjectKnown(object->GetGuid()))
					{
						objects.push_back(object);
					}
				}

				if (!objects.empty())
				{
					client.NotifyObjectsSpawned(objects, game::spawn_reason::CameIntoSight);
				}
			}
		}

		void Unsubscribe(RecordingClient& client)
		{
			world->GetGrid().RequireTile(TileOf(client.unit.GetPosition())).GetWatchers().optionalRemove(&client);
		}
	};
}

TEST_CASE_METHOD(RemoteViewFixture, "A remote view shows its owner what is around the camera", "[world][visibility][cutscene]")
{
	auto ownUnit = Spawn(home);
	auto neighbour = Spawn(home + Vector3(3.0f, 0.0f, 0.0f));
	auto distant = Spawn(TilesFromHome(10));

	RecordingClient client(*ownUnit);
	Subscribe(client);

	REQUIRE(client.Knows(*neighbour));
	REQUIRE_FALSE(client.Knows(*distant));

	RemoteView view(client, world->GetGrid());
	view.MoveTo(TileOf(distant->GetPosition()));

	// The camera sees the distant creature, and the surroundings of the character stay as they were
	CHECK(view.IsAttached());
	CHECK(client.Knows(*distant));
	CHECK(client.Knows(*neighbour));
	CHECK(client.despawns == 0);

	SECTION("Taking the view away despawns only what the camera alone saw")
	{
		view.Detach(true);

		CHECK_FALSE(view.IsAttached());
		CHECK_FALSE(client.Knows(*distant));
		CHECK(client.Knows(*neighbour));
		CHECK(client.Knows(*ownUnit));
		CHECK(client.ownDespawns == 0);
	}

	SECTION("A view that is dropped because its owner leaves tells the owner nothing")
	{
		const size_t despawnsBefore = client.despawns;
		view.Detach(false);

		CHECK(client.despawns == despawnsBefore);
		CHECK(client.Knows(*distant));
	}

	CHECK(client.doubleSpawns == 0);
	CHECK(client.unknownDespawns == 0);

	view.Detach(false);
	Unsubscribe(client);
}

TEST_CASE_METHOD(RemoteViewFixture, "A travelling remote view spawns what it reaches and despawns what it leaves", "[world][visibility][cutscene]")
{
	auto ownUnit = Spawn(home);
	auto first = Spawn(TilesFromHome(10));
	auto second = Spawn(TilesFromHome(20));

	RecordingClient client(*ownUnit);
	Subscribe(client);

	RemoteView view(client, world->GetGrid());

	view.MoveTo(TileOf(first->GetPosition()));
	CHECK(client.Knows(*first));
	CHECK_FALSE(client.Knows(*second));

	view.MoveTo(TileOf(second->GetPosition()));
	CHECK_FALSE(client.Knows(*first));
	CHECK(client.Knows(*second));

	// Moving within the same tile is free
	const size_t spawnsBefore = client.spawns;
	view.MoveTo(TileOf(second->GetPosition()));
	CHECK(client.spawns == spawnsBefore);

	// Back over the character: the camera adds nothing to what the character sees itself
	view.MoveTo(TileOf(home));
	CHECK_FALSE(client.Knows(*second));
	CHECK(client.Knows(*ownUnit));

	CHECK(client.ownDespawns == 0);
	CHECK(client.doubleSpawns == 0);
	CHECK(client.unknownDespawns == 0);

	view.Detach(false);
	Unsubscribe(client);
}

TEST_CASE_METHOD(RemoteViewFixture, "Overlapping views tell the client everything exactly once", "[world][visibility][cutscene]")
{
	auto ownUnit = Spawn(home);

	RecordingClient client(*ownUnit);
	Subscribe(client);

	// Three tiles away: the tiles one and two tiles from home are seen by both views
	RemoteView view(client, world->GetGrid());
	view.MoveTo(TileOf(TilesFromHome(3)));

	SECTION("Broadcasts from a tile both views see reach the owner through its own subscription only")
	{
		CHECK_FALSE(view.AcceptsBroadcastFrom(TileOf(TilesFromHome(2))));
		CHECK(view.AcceptsBroadcastFrom(TileOf(TilesFromHome(4))));

		size_t recipients = 0;
		ForEachSubscriberInSight(world->GetGrid(), TileOf(TilesFromHome(2)), [&recipients](TileSubscriber&) { ++recipients; });
		CHECK(recipients == 1);

		recipients = 0;
		ForEachSubscriberInSight(world->GetGrid(), TileOf(TilesFromHome(4)), [&recipients](TileSubscriber&) { ++recipients; });
		CHECK(recipients == 1);
	}

	SECTION("A creature that appears where both views see it is spawned once")
	{
		auto shared = Spawn(TilesFromHome(2));

		CHECK(client.Knows(*shared));
		CHECK(client.doubleSpawns == 0);
	}

	SECTION("A creature that appears where only the camera sees it is spawned")
	{
		auto cameraOnly = Spawn(TilesFromHome(5));

		CHECK(client.Knows(*cameraOnly));
		CHECK(client.doubleSpawns == 0);

		// Leaving the world again despawns it once
		world->RemoveGameObject(*cameraOnly);
		CHECK_FALSE(client.Knows(*cameraOnly));
		CHECK(client.unknownDespawns == 0);
	}

	SECTION("A creature walking out of the camera's sight but into the character's stays with the client")
	{
		auto walker = Spawn(TilesFromHome(5));
		REQUIRE(client.Knows(*walker));

		walker->Relocate(TilesFromHome(1), walker->GetFacing());

		CHECK(client.Knows(*walker));
		CHECK(client.despawns == 0);
	}

	SECTION("A creature walking out of sight of both views is despawned once")
	{
		auto walker = Spawn(TilesFromHome(5));
		REQUIRE(client.Knows(*walker));

		walker->Relocate(TilesFromHome(9), walker->GetFacing());

		CHECK_FALSE(client.Knows(*walker));
		CHECK(client.despawns == 1);
		CHECK(client.unknownDespawns == 0);
	}

	view.Detach(false);
	Unsubscribe(client);
}
