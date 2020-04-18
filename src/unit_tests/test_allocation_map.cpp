// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "catch.hpp"

#include "base/typedefs.h"
#include "hpak_v1_0/allocation_map.h"

using namespace mmo;


namespace
{
	void CheckedReserve(
		hpak::AllocationMap &map,
		uint32 offset,
		uint32 size)
	{
		REQUIRE(map.reserve(offset, size));
		REQUIRE(!map.reserve(offset, size));
	}
}

TEST_CASE("AllocationMapSimple", "[allocation_map]")
{
	hpak::AllocationMap map;

	const size_t blockSize = 10;

	CheckedReserve(map, 0, blockSize);
	CheckedReserve(map, blockSize, blockSize);

	for (size_t i = 0; i < (2 * blockSize); ++i)
	{
		REQUIRE(!map.reserve(i, 1));
	}

	REQUIRE(map.allocate(1) == (2 * blockSize));
}

TEST_CASE("AllocationMapHole", "[allocation_map]")
{
	hpak::AllocationMap map;

	const size_t blockSize = 10;

	CheckedReserve(map, 0, blockSize);
	CheckedReserve(map, 2 * blockSize, blockSize);

	for (size_t i = 0; i < blockSize; ++i)
	{
		REQUIRE(!map.reserve(i, 1));
	}

	for (size_t i = 2 * blockSize; i < (3 * blockSize); ++i)
	{
		REQUIRE(!map.reserve(i, 1));
	}

	CheckedReserve(map, blockSize, blockSize);

	for (size_t i = 0; i < 10; ++i)
	{
		REQUIRE(map.allocate(1) == ((3 * blockSize) + i));
	}
}

TEST_CASE("AllocationReverseReserve", "[allocation_map]")
{
	hpak::AllocationMap map;

	const size_t blockSize = 10;
	const size_t blockCount = 10;

	for (size_t i = (blockSize * blockCount); i >= blockSize; i -= blockSize)
	{
		CheckedReserve(map, i - blockSize, blockSize);
	}

	REQUIRE(map.allocate(1) == (blockSize * blockCount));
}