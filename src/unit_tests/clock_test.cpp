// Copyright (C) 2022, Robin Klimonow. All rights reserved.

#include "catch.hpp"

#include "base/clock.h"
#include <thread>
#include <chrono>

using namespace mmo;


TEST_CASE("GetAsyncTimeMsReturnsValidValue", "[clock_test]")
{
	const auto start = GetAsyncTimeMs();
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	const auto end = GetAsyncTimeMs();

	const auto diff = end - start;
	CHECK(diff >= 9);
	CHECK(diff <= 9);
}