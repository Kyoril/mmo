// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/graph/debug_geometry.h"

#include <cmath>
#include <initializer_list>
#include <utility>

using namespace mmo;
using namespace mmo::pcg::graph;

namespace
{
	DataItem MakePoints(const size_t count)
	{
		DataItem item;
		item.kind = ItemKind::Points;
		item.points.points.resize(count);
		for (size_t i = 0; i < count; ++i)
		{
			item.points.points[i].position = Vector3(static_cast<float>(i % 1000), 0.0f, static_cast<float>(i / 1000));
		}
		return item;
	}

	bool SamePosition(const Vector3& a, const Vector3& b)
	{
		return a.x == b.x && a.y == b.y && a.z == b.z;
	}
}

TEST_CASE("BuildDebugGeometry draws one point as a 12-line box", "[graph_debug]")
{
	Collection items { MakePoints(1) };
	items[0].points.points[0].position = Vector3(10.0f, 2.0f, -4.0f);
	items[0].points.points[0].scale = Vector3(2.0f, 2.0f, 2.0f);

	const DebugGeometry geometry = BuildDebugGeometry(items, 0xFF00FF00);
	REQUIRE(geometry.lines.size() == 12);
	CHECK(geometry.pointsTotal == 1);
	CHECK(geometry.pointsDrawn == 1);

	// Unit bounds scaled by 2 around (10, 2, -4): every corner coordinate is centre +/- 1.
	for (const DebugLine& line : geometry.lines)
	{
		for (const Vector3& v : { line.a, line.b })
		{
			CHECK(std::abs(std::abs(v.x - 10.0f) - 1.0f) < 1e-4f);
			CHECK(std::abs(std::abs(v.y - 2.0f) - 1.0f) < 1e-4f);
			CHECK(std::abs(std::abs(v.z + 4.0f) - 1.0f) < 1e-4f);
		}
		CHECK((line.argb & 0xFF000000u) == 0xFF000000u);
	}
}

TEST_CASE("BuildDebugGeometry switches to ticks beyond the box limit", "[graph_debug]")
{
	// Split over two items: the limits count points across the whole collection.
	const Collection items { MakePoints(12'000), MakePoints(18'000) };
	const DebugGeometry geometry = BuildDebugGeometry(items, 0xFFFFFFFF, 20'000, 200'000);
	CHECK(geometry.lines.size() == 20'000u * 12u + 10'000u);
	CHECK(geometry.pointsTotal == 30'000);
	CHECK(geometry.pointsDrawn == 30'000);
}

TEST_CASE("BuildDebugGeometry subsamples deterministically beyond the point limit", "[graph_debug]")
{
	const Collection items { MakePoints(500'000) };
	const DebugGeometry a = BuildDebugGeometry(items, 0xFFFF0000, 20'000, 200'000);
	CHECK(a.pointsTotal == 500'000);
	CHECK(a.pointsDrawn <= 200'000);
	CHECK(a.pointsDrawn > 0);
	CHECK(a.lines.size() == 20'000u * 12u + (a.pointsDrawn - 20'000u));

	const DebugGeometry b = BuildDebugGeometry(items, 0xFFFF0000, 20'000, 200'000);
	REQUIRE(b.lines.size() == a.lines.size());
	CHECK(b.pointsDrawn == a.pointsDrawn);
	bool same = true;
	for (size_t i = 0; i < a.lines.size() && same; ++i)
	{
		same = SamePosition(a.lines[i].a, b.lines[i].a) && SamePosition(a.lines[i].b, b.lines[i].b) && a.lines[i].argb == b.lines[i].argb;
	}
	CHECK(same);
}

TEST_CASE("BuildDebugGeometry draws a closed spline as a closed polyline", "[graph_debug]")
{
	DataItem item;
	item.kind = ItemKind::Area;
	item.spline.closed = true;
	item.spline.interpolation = SplineInterpolation::Linear;
	for (const auto& xz : { std::pair<float, float>(0, 0), { 10, 0 }, { 10, 10 }, { 0, 10 } })
	{
		SplinePoint p;
		p.x = xz.first;
		p.z = xz.second;
		item.spline.points.push_back(p);
	}

	const DebugGeometry geometry = BuildDebugGeometry(Collection { item }, 0xFF0000FF);
	REQUIRE(geometry.lines.size() >= 4);
	CHECK(geometry.pointsTotal == 0);
	for (size_t i = 1; i < geometry.lines.size(); ++i)
	{
		CHECK(SamePosition(geometry.lines[i - 1].b, geometry.lines[i].a));
	}
	CHECK(SamePosition(geometry.lines.back().b, geometry.lines.front().a));
}

TEST_CASE("BuildDebugGeometry draws an open spline as an open polyline", "[graph_debug]")
{
	DataItem item;
	item.kind = ItemKind::Spline;
	for (const float x : { 0.0f, 5.0f, 10.0f })
	{
		SplinePoint p;
		p.x = x;
		item.spline.points.push_back(p);
	}

	const DebugGeometry geometry = BuildDebugGeometry(Collection { item }, 0xFF0000FF);
	REQUIRE(!geometry.lines.empty());
	CHECK(!SamePosition(geometry.lines.back().b, geometry.lines.front().a));
}

TEST_CASE("BuildDebugGeometry gives no lines for an empty collection", "[graph_debug]")
{
	const DebugGeometry geometry = BuildDebugGeometry(Collection(), 0xFFFFFFFF);
	CHECK(geometry.lines.empty());
	CHECK(geometry.pointsTotal == 0);
	CHECK(geometry.pointsDrawn == 0);
}

TEST_CASE("BuildDebugGeometry scales brightness by density", "[graph_debug]")
{
	Collection items { MakePoints(2) };
	items[0].points.points[0].density = 1.0f;
	items[0].points.points[1].density = 0.0f;

	const DebugGeometry geometry = BuildDebugGeometry(items, 0xFFC08040);
	REQUIRE(geometry.lines.size() == 24);
	const uint32 bright = geometry.lines[0].argb;
	const uint32 dim = geometry.lines[12].argb;
	CHECK(bright == 0xFFC08040);
	CHECK((dim & 0xFF000000u) == 0xFF000000u);
	CHECK(((dim >> 16) & 0xFF) < 0xC0);
	CHECK(((dim >> 8) & 0xFF) < 0x80);
	CHECK((dim & 0xFF) < 0x40);
}

TEST_CASE("BuildDebugGeometry caps lines at the line limit", "[graph_debug]")
{
	// Ten 100 x 100 areas: thousands of polyline segments each at the sampling step.
	Collection items;
	for (int a = 0; a < 10; ++a)
	{
		DataItem item;
		item.kind = ItemKind::Area;
		item.spline.closed = true;
		item.spline.interpolation = SplineInterpolation::Linear;
		const float x0 = static_cast<float>(a) * 200.0f;
		for (const std::pair<float, float>& xz : std::initializer_list<std::pair<float, float>> { { x0, 0.0f }, { x0 + 100.0f, 0.0f }, { x0 + 100.0f, 100.0f }, { x0, 100.0f } })
		{
			SplinePoint p;
			p.x = xz.first;
			p.z = xz.second;
			item.spline.points.push_back(p);
		}
		items.push_back(item);
	}
	// A point after the areas: no budget left for it.
	items.push_back(MakePoints(1));

	const DebugGeometry unlimited = BuildDebugGeometry(items, 0xFFFFFFFF, 20'000, 200'000, ~size_t(0));
	REQUIRE(unlimited.linesTotal > 10'000);
	CHECK(unlimited.linesDrawn == unlimited.linesTotal);
	CHECK(unlimited.pointsDrawn == 1);

	const DebugGeometry capped = BuildDebugGeometry(items, 0xFFFFFFFF, 20'000, 200'000, 10'000);
	CHECK(capped.lines.size() == 10'000);
	CHECK(capped.linesDrawn == 10'000);
	CHECK(capped.linesTotal == unlimited.linesTotal);
	CHECK(capped.pointsTotal == 1);
	CHECK(capped.pointsDrawn == 0);

	// The capped lines are the first lines of the unlimited drawing.
	bool prefix = true;
	for (size_t i = 0; i < capped.lines.size() && prefix; ++i)
	{
		prefix = SamePosition(capped.lines[i].a, unlimited.lines[i].a) && SamePosition(capped.lines[i].b, unlimited.lines[i].b);
	}
	CHECK(prefix);
}

TEST_CASE("BuildDebugGeometry draws a point only if its whole box fits", "[graph_debug]")
{
	const Collection items { MakePoints(3) };
	const DebugGeometry geometry = BuildDebugGeometry(items, 0xFFFFFFFF, 20'000, 200'000, 30);
	CHECK(geometry.lines.size() == 24);
	CHECK(geometry.pointsDrawn == 2);
	CHECK(geometry.boxesDrawn == 2);
	CHECK(geometry.linesTotal == 36);
	CHECK(geometry.linesDrawn == 24);
}
