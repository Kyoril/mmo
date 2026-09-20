// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "scene_graph/environment_retarget.h"

#include <optional>

using namespace mmo;

TEST_CASE("A pending snap always snaps", "[environment]")
{
	CHECK(ShouldSnapEnvironment(true, std::nullopt, Vector3(0.0f, 0.0f, 0.0f)));
	CHECK(ShouldSnapEnvironment(true, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f)));
}

TEST_CASE("No last position and not pending does not snap", "[environment]")
{
	CHECK_FALSE(ShouldSnapEnvironment(false, std::nullopt, Vector3(1000.0f, 0.0f, 1000.0f)));
}

TEST_CASE("A move under the teleport distance does not snap", "[environment]")
{
	const Vector3 last(0.0f, 0.0f, 0.0f);
	const Vector3 current(199.0f, 0.0f, 0.0f);
	CHECK_FALSE(ShouldSnapEnvironment(false, last, current));
}

TEST_CASE("A move beyond the teleport distance snaps", "[environment]")
{
	const Vector3 last(0.0f, 0.0f, 0.0f);
	const Vector3 current(201.0f, 0.0f, 0.0f);
	CHECK(ShouldSnapEnvironment(false, last, current));
}

TEST_CASE("A custom teleport distance is honoured", "[environment]")
{
	const Vector3 last(0.0f, 0.0f, 0.0f);
	const Vector3 current(60.0f, 0.0f, 0.0f);

	CHECK_FALSE(ShouldSnapEnvironment(false, last, current, 100.0f));
	CHECK(ShouldSnapEnvironment(false, last, current, 50.0f));
}
