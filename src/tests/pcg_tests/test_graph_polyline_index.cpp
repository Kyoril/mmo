// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/graph/polyline_index.h"

#include <cmath>
#include <vector>

using namespace mmo;
using namespace mmo::pcg::graph;

namespace
{
	std::vector<SplineSample> Samples(const std::vector<std::pair<float, float>>& points)
	{
		std::vector<SplineSample> samples;
		for (const auto& [x, z] : points)
		{
			SplineSample s;
			s.x = x;
			s.z = z;
			samples.push_back(s);
		}
		return samples;
	}

	/// Distance from (x, z) to segment a-b.
	double SegmentDistance(const SplineSample& a, const SplineSample& b, const double x, const double z)
	{
		const double dx = static_cast<double>(b.x) - a.x;
		const double dz = static_cast<double>(b.z) - a.z;
		const double len2 = dx * dx + dz * dz;
		const double t = len2 > 0.0 ? std::min(1.0, std::max(0.0, ((x - a.x) * dx + (z - a.z) * dz) / len2)) : 0.0;
		return std::hypot(x - (a.x + dx * t), z - (a.z + dz * t));
	}
}

TEST_CASE("PolylineIndex finds the nearest segment and the point on it", "[graph_polyline_index]")
{
	PolylineIndex index;
	index.Build(Samples({ { 0.0f, 0.0f }, { 10.0f, 0.0f }, { 20.0f, 0.0f } }), false, 5.0f);

	NearestHit hit;
	REQUIRE(index.Nearest(5.0f, 3.0f, hit));
	CHECK(hit.distance == Approx(3.0f));
	CHECK(hit.a == 0);
	CHECK(hit.b == 1);
	CHECK(hit.t == Approx(0.5f));

	REQUIRE(index.Nearest(17.5f, -4.0f, hit));
	CHECK(hit.distance == Approx(4.0f));
	CHECK(hit.a == 1);
	CHECK(hit.b == 2);
	CHECK(hit.t == Approx(0.75f));

	// Past the open end: radial to the end sample.
	REQUIRE(index.Nearest(23.0f, 4.0f, hit));
	CHECK(hit.distance == Approx(5.0f));
	CHECK(hit.a == 1);
	CHECK(hit.b == 2);
	CHECK(hit.t == 1.0f);

	// Beyond the reach, and far outside the grid.
	CHECK_FALSE(index.Nearest(5.0f, 5.5f, hit));
	CHECK_FALSE(index.Nearest(500.0f, 0.0f, hit));
	CHECK_FALSE(index.IsClosed());
	CHECK(index.GetSamples().size() == 3);
}

TEST_CASE("PolylineIndex breaks ties to the lowest segment index", "[graph_polyline_index]")
{
	// (5, 5) is 5 m from segment 0 ((0, 0)-(10, 0)) and from segment 1 ((10, 0)-(10, 10)).
	PolylineIndex index;
	index.Build(Samples({ { 0.0f, 0.0f }, { 10.0f, 0.0f }, { 10.0f, 10.0f } }), false, 8.0f);
	NearestHit hit;
	REQUIRE(index.Nearest(5.0f, 5.0f, hit));
	CHECK(hit.distance == Approx(5.0f));
	CHECK(hit.a == 0);
	CHECK(hit.b == 1);

	// The shared corner sample: both segments are at distance 1 again, segment 0 wins.
	REQUIRE(index.Nearest(11.0f, 0.0f, hit));
	CHECK(hit.a == 0);
	CHECK(hit.b == 1);
	CHECK(hit.t == 1.0f);
}

TEST_CASE("PolylineIndex closes a loop of at least 3 samples", "[graph_polyline_index]")
{
	const std::vector<SplineSample> square = Samples({ { 0.0f, 0.0f }, { 10.0f, 0.0f }, { 10.0f, 10.0f }, { 0.0f, 10.0f } });

	PolylineIndex open;
	open.Build(square, false, 6.0f);
	NearestHit hit;
	REQUIRE(open.Nearest(-1.0f, 5.0f, hit));
	CHECK(hit.distance == Approx(std::sqrt(26.0f)));

	PolylineIndex closed;
	closed.Build(square, true, 6.0f);
	CHECK(closed.IsClosed());
	REQUIRE(closed.Nearest(-1.0f, 5.0f, hit));
	CHECK(hit.distance == Approx(1.0f));
	CHECK(hit.a == 3);
	CHECK(hit.b == 0);
	CHECK(hit.t == Approx(0.5f));

	// Two samples: closed adds no second segment.
	PolylineIndex pair;
	pair.Build(Samples({ { 0.0f, 0.0f }, { 10.0f, 0.0f } }), true, 6.0f);
	REQUIRE(pair.Nearest(5.0f, 2.0f, hit));
	CHECK(hit.a == 0);
	CHECK(hit.b == 1);
}

TEST_CASE("PolylineIndex handles a single sample and an empty polyline", "[graph_polyline_index]")
{
	PolylineIndex single;
	single.Build(Samples({ { 3.0f, 4.0f } }), false, 10.0f);
	NearestHit hit;
	REQUIRE(single.Nearest(0.0f, 0.0f, hit));
	CHECK(hit.distance == Approx(5.0f));
	CHECK(hit.a == 0);
	CHECK(hit.b == 0);

	PolylineIndex empty;
	empty.Build({}, false, 10.0f);
	CHECK_FALSE(empty.Nearest(0.0f, 0.0f, hit));
}

TEST_CASE("PolylineIndex matches a brute-force search", "[graph_polyline_index]")
{
	// A winding closed polyline, queried on a grid: the grid-bucketed result equals the minimum over all segments.
	std::vector<std::pair<float, float>> points;
	for (int i = 0; i < 40; ++i)
	{
		const float angle = static_cast<float>(i) * 0.157f;
		const float radius = 60.0f + 15.0f * std::sin(angle * 5.0f);
		points.emplace_back(radius * std::cos(angle), radius * std::sin(angle));
	}
	const std::vector<SplineSample> samples = Samples(points);
	constexpr float reach = 12.0f;
	PolylineIndex index;
	index.Build(samples, true, reach);

	size_t hits = 0;
	for (float z = -90.0f; z <= 90.0f; z += 3.7f)
	{
		for (float x = -90.0f; x <= 90.0f; x += 3.3f)
		{
			double best = 1e30;
			for (size_t i = 0; i < samples.size(); ++i)
			{
				best = std::min(best, SegmentDistance(samples[i], samples[(i + 1) % samples.size()], x, z));
			}
			NearestHit hit;
			const bool found = index.Nearest(x, z, hit);
			REQUIRE(found == (best <= reach));
			if (found)
			{
				++hits;
				CHECK(hit.distance == Approx(best).margin(1e-4));
			}
		}
	}
	CHECK(hits > 100);
}
