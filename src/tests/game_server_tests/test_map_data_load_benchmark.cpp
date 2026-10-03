// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "assets/asset_registry.h"
#include "game_server/world/server_collision_map.h"
#include "game_server/world/server_water_map.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

using namespace mmo;

// Hidden: loads the server-side collision and water data of a real map and reports how long
// each took. Point MMO_NAV_BENCH_DIR at the nav data folder (data/editor/nav) and
// MMO_WORLD_BENCH_DIR at the client data folder (data/client), optionally MMO_NAV_BENCH_MAP at
// the map name (default "Development"), then run `game_server_tests "[.map_data_benchmark]"`.
TEST_CASE("Server map data load benchmark", "[.map_data_benchmark]")
{
	const char* navDir = std::getenv("MMO_NAV_BENCH_DIR");
	const char* worldDir = std::getenv("MMO_WORLD_BENCH_DIR");
	if (!navDir || !worldDir)
	{
		WARN("MMO_NAV_BENCH_DIR / MMO_WORLD_BENCH_DIR not set, skipping");
		return;
	}

	const char* mapNameEnv = std::getenv("MMO_NAV_BENCH_MAP");
	const std::string mapName = mapNameEnv ? mapNameEnv : "Development";

	AssetRegistry::Initialize(navDir, {});
	AssetRegistry::AddArchivePackage(worldDir);

	using ms = std::chrono::milliseconds;

	const auto t0 = std::chrono::steady_clock::now();
	ServerCollisionMap collision(mapName);
	const auto t1 = std::chrono::steady_clock::now();
	ServerWaterMap water(mapName);
	const auto t2 = std::chrono::steady_clock::now();

	std::cout << "collision map '" << mapName << "': " << std::chrono::duration_cast<ms>(t1 - t0).count() << " ms (loaded: " << collision.IsLoaded() << ")" << std::endl;
	std::cout << "water map '" << mapName << "': " << std::chrono::duration_cast<ms>(t2 - t1).count() << " ms (loaded: " << water.IsLoaded() << ")" << std::endl;

	AssetRegistry::Destroy();
	CHECK(collision.IsLoaded());
}
