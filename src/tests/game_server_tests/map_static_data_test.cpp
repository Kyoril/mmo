// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/world/map_static_data.h"

#include <memory>

using namespace mmo;

namespace
{
	// No files exist for this directory: the nav map is invalid, the collision map empty.
	constexpr const char* MissingMap = "Worlds/__static_data_test__/__static_data_test__";
}

TEST_CASE("MapStaticDataRegistry shares one MapStaticData per directory", "[map_static_data]")
{
	MapStaticDataRegistry registry;

	const auto a = registry.Acquire(MissingMap);
	const auto b = registry.Acquire(MissingMap);
	REQUIRE(a);
	CHECK(a == b);
	CHECK(registry.GetLiveCount() == 1);

	const auto other = registry.Acquire("Worlds/__other__/__other__");
	CHECK(other != a);
	CHECK(registry.GetLiveCount() == 2);
}

TEST_CASE("MapStaticDataRegistry frees data once the last user is gone", "[map_static_data]")
{
	MapStaticDataRegistry registry;

	std::weak_ptr<MapStaticData> weak;
	{
		const auto a = registry.Acquire(MissingMap);
		weak = a;
		CHECK(registry.GetLiveCount() == 1);
	}

	CHECK(weak.expired());
	CHECK(registry.GetLiveCount() == 0);
}

TEST_CASE("MapStaticDataRegistry reloads after the last instance is gone", "[map_static_data]")
{
	MapStaticDataRegistry registry;

	{
		const auto first = registry.Acquire(MissingMap);
		REQUIRE(first);
	}

	const auto second = registry.Acquire(MissingMap);
	REQUIRE(second);
	CHECK(second->GetDirectory() == MissingMap);
	CHECK(registry.GetLiveCount() == 1);

	// Update on data without nav pages must be a harmless no-op.
	registry.Update();
}
