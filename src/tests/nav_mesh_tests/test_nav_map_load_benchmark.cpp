// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "assets/asset_registry.h"
#include "nav_mesh/map.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace mmo;

// Hidden: loads a real nav map from disk and reports how long it took. Point
// MMO_NAV_BENCH_DIR at a nav data folder (e.g. data/editor/nav) and optionally
// MMO_NAV_BENCH_MAP at the map name (default "Development"), then run
// `nav_mesh_tests "[.nav_benchmark]"`.
TEST_CASE("Nav map full load benchmark", "[.nav_benchmark]")
{
	const char* dir = std::getenv("MMO_NAV_BENCH_DIR");
	if (!dir)
	{
		WARN("MMO_NAV_BENCH_DIR not set, skipping");
		return;
	}

	const char* mapNameEnv = std::getenv("MMO_NAV_BENCH_MAP");
	const std::string mapName = mapNameEnv ? mapNameEnv : "Development";

	AssetRegistry::Initialize(dir, {});

	const auto t0 = std::chrono::steady_clock::now();
	nav::Map map(mapName);
	const auto t1 = std::chrono::steady_clock::now();
	const int32 pages = map.LoadAllPages();
	const auto t2 = std::chrono::steady_clock::now();

	using ms = std::chrono::milliseconds;
	std::cout << "nav map '" << mapName << "': init " << std::chrono::duration_cast<ms>(t1 - t0).count()
		<< " ms, " << pages << " pages loaded in " << std::chrono::duration_cast<ms>(t2 - t1).count() << " ms" << std::endl;

	map.UnloadAllPages();
	const auto t3 = std::chrono::steady_clock::now();
	std::cout << "unload: " << std::chrono::duration_cast<ms>(t3 - t2).count() << " ms" << std::endl;

	// Same load split into its two phases: reading (thread-safe) and installing into Detour.
	std::vector<nav::PageData> pageData;
	for (int32 y = 0; y < 64; ++y)
	{
		for (int32 x = 0; x < 64; ++x)
		{
			if (map.HasPage(x, y))
			{
				nav::Map::ReadPage(mapName, x, y, pageData.emplace_back());
			}
		}
	}
	const auto t4 = std::chrono::steady_clock::now();
	for (auto& page : pageData)
	{
		map.AddPage(std::move(page));
	}
	const auto t5 = std::chrono::steady_clock::now();
	std::cout << "read: " << std::chrono::duration_cast<ms>(t4 - t3).count() << " ms, add: "
		<< std::chrono::duration_cast<ms>(t5 - t4).count() << " ms" << std::endl;

	AssetRegistry::Destroy();
	CHECK(pages > 0);
}
