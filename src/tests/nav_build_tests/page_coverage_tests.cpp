// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "nav_build/page_coverage.h"

#include <limits>
#include <vector>

using namespace mmo;

namespace
{
	uint32 CountPages(const NavPageGrid& pages)
	{
		uint32 count = 0;
		for (const auto& column : pages)
		{
			for (const bool hasPage : column)
			{
				count += hasPage ? 1 : 0;
			}
		}

		return count;
	}
}

TEST_CASE("Page 32 starts at the world origin", "[nav_build]")
{
	const double pageSize = terrain::constants::PageSize;

	CHECK(PageIndexForCoordinate(0.0) == 32);
	CHECK(PageIndexForCoordinate(pageSize * 0.5) == 32);
	CHECK(PageIndexForCoordinate(-0.01) == 31);
	CHECK(PageIndexForCoordinate(pageSize + 0.01) == 33);
}

TEST_CASE("Page indices clamp to the page grid", "[nav_build]")
{
	const double pageSize = terrain::constants::PageSize;

	CHECK(PageIndexForCoordinate(-pageSize * 40.0) == 0);
	CHECK(PageIndexForCoordinate(pageSize * 40.0) == 63);
}

TEST_CASE("A world model inside one page marks only that page", "[nav_build]")
{
	NavPageGrid pages{};
	const std::vector<AABB> bounds{ AABB(Vector3(-50.f, -20.f, 10.f), Vector3(-10.f, 30.f, 60.f)) };

	CHECK(MarkPagesCoveredByBounds(bounds, pages) == 1);
	CHECK(pages[31][32]);
	CHECK(CountPages(pages) == 1);
}

TEST_CASE("Geometry across page borders marks every page its footprint overlaps", "[nav_build]")
{
	NavPageGrid pages{};
	const std::vector<AABB> bounds{
		AABB(Vector3(-20.f, 0.f, -20.f), Vector3(20.f, 5.f, 20.f)),
		AABB(Vector3(-10.f, 0.f, -10.f), Vector3(10.f, 5.f, 10.f)),
	};

	// Both boxes straddle the origin; the second covers the same four pages and adds none.
	CHECK(MarkPagesCoveredByBounds(bounds, pages) == 4);
	CHECK(pages[31][31]);
	CHECK(pages[31][32]);
	CHECK(pages[32][31]);
	CHECK(pages[32][32]);
	CHECK(CountPages(pages) == 4);
}

TEST_CASE("Inverted bounds mark no page and existing pages are kept", "[nav_build]")
{
	NavPageGrid pages{};
	pages[5][7] = true;

	// Bounds whose minimum lies above their maximum describe nothing.
	const std::vector<AABB> bounds{ AABB(Vector3(10.f, 10.f, 10.f), Vector3(-10.f, -10.f, -10.f)) };

	CHECK(MarkPagesCoveredByBounds(bounds, pages) == 0);
	CHECK(pages[5][7]);
	CHECK(CountPages(pages) == 1);
}

TEST_CASE("Bounds with a NaN or infinite coordinate mark no page", "[nav_build]")
{
	NavPageGrid pages{};
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float infinity = std::numeric_limits<float>::infinity();

	const std::vector<AABB> bounds{
		AABB(Vector3(nan, 0.f, 0.f), Vector3(10.f, 5.f, 10.f)),
		AABB(Vector3(0.f, 0.f, 0.f), Vector3(10.f, 5.f, infinity)),
	};

	CHECK(MarkPagesCoveredByBounds(bounds, pages) == 0);
	CHECK(CountPages(pages) == 0);
}
