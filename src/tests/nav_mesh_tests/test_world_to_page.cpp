// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "nav_mesh/map.h"
#include "terrain/constants.h"

using namespace mmo;

TEST_CASE("WorldToPage places the world origin at the start of page 32", "[nav_mesh]")
{
	constexpr float pageSize = static_cast<float>(terrain::constants::PageSize);

	CHECK(nav::WorldToPage(0.f) == 32);
	CHECK(nav::WorldToPage(pageSize * 0.5f) == 32);
	CHECK(nav::WorldToPage(-0.5f) == 31);
	CHECK(nav::WorldToPage(pageSize * 1.5f) == 33);
}

TEST_CASE("WorldToPage clamps coordinates outside the page grid", "[nav_mesh]")
{
	constexpr int32 lastPage = static_cast<int32>(terrain::constants::MaxPages) - 1;

	CHECK(nav::WorldToPage(-1.e9f) == 0);
	CHECK(nav::WorldToPage(1.e9f) == lastPage);
}
