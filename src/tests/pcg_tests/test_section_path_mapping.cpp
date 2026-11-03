// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/section_path.h"

#include <cmath>

using namespace mmo;
using namespace mmo::pcg;

namespace
{
	PathSpline Straight(const std::vector<float>& zs, const float y0 = 0.0f, const float y1 = 0.0f)
	{
		PathSpline spline;
		spline.interpolation = SplineInterpolation::Linear;
		for (size_t i = 0; i < zs.size(); ++i)
		{
			SplinePoint p;
			p.x = 0.0f;
			p.z = zs[i];
			p.y = y0 + (y1 - y0) * static_cast<float>(i) / static_cast<float>(zs.size() - 1);
			spline.points.push_back(p);
		}
		return spline;
	}

	/// Builds the path once with the contract step and lays the sections along it.
	SectionLayout Lay(const PathSpline& spline, const std::vector<uint64>& ids, const float length)
	{
		const SampledPath path(spline, kSectionPathSampleStep);
		return LayOutSections(spline, path, ids, length);
	}

	std::vector<uint64> Ids(const size_t n, const uint64 first = 100)
	{
		std::vector<uint64> ids;
		for (size_t i = 0; i < n; ++i)
		{
			ids.push_back(first + i);
		}
		return ids;
	}

	PathSpline FourPoints(const float p2x, const float p2z)
	{
		PathSpline spline;
		spline.interpolation = SplineInterpolation::Linear;
		SplinePoint a; a.x = 0.0f; a.z = 0.0f;
		SplinePoint b; b.x = 0.0f; b.z = 30.0f;
		SplinePoint c; c.x = p2x; c.z = p2z;
		SplinePoint d; d.x = 30.0f; d.z = 60.0f;
		SplinePoint e; e.x = 30.0f; e.z = 90.0f;
		spline.points = { a, b, c, d, e };
		return spline;
	}
}

TEST_CASE("A straight spline gets evenly stretched copies", "[section_path_map]")
{
	const SectionLayout layout = Lay(Straight({ 0.0f, 100.0f }), Ids(2), 20.0f);
	REQUIRE(layout.copies.size() == 5);
	for (size_t i = 0; i < 5; ++i)
	{
		CHECK(layout.copies[i].stretchedLength == Approx(20.0f));
		CHECK(layout.copies[i].startDistance == Approx(20.0f * static_cast<float>(i)));
	}
}

TEST_CASE("Every span is fitted on its own", "[section_path_map]")
{
	const SectionLayout layout = Lay(Straight({ 0.0f, 45.0f, 100.0f }), Ids(3), 20.0f);
	REQUIRE(layout.copies.size() == 5);
	CHECK(layout.copies[0].span == 0);
	CHECK(layout.copies[1].stretchedLength == Approx(22.5f));
	CHECK(layout.copies[2].span == 1);
	CHECK(layout.copies[2].copy == 0);
	CHECK(layout.copies[2].startDistance == Approx(45.0f));
	CHECK(layout.copies[4].stretchedLength == Approx(55.0f / 3.0f));
}

TEST_CASE("A span shorter than a section still gets one copy", "[section_path_map]")
{
	const SectionLayout layout = Lay(Straight({ 0.0f, 7.0f }), Ids(2), 20.0f);
	REQUIRE(layout.copies.size() == 1);
	CHECK(layout.copies[0].stretchedLength == Approx(7.0f));
}

TEST_CASE("Frames follow the spline", "[section_path_map]")
{
	const SampledPath flat(Straight({ 0.0f, 100.0f }), kSectionPathSampleStep);
	const SectionFrame f = FrameAt(flat, 50.0f);
	CHECK(f.forwardX == Approx(0.0f));
	CHECK(f.forwardZ == Approx(1.0f));
	CHECK(f.rightX == Approx(1.0f));
	CHECK(f.rightZ == Approx(0.0f));
	CHECK(f.position.z == Approx(50.0f));

	const SampledPath rising(Straight({ 0.0f, 100.0f }, 0.0f, 10.0f), kSectionPathSampleStep);
	const SectionFrame r = FrameAt(rising, 50.0f);
	CHECK(r.position.y == Approx(5.0f));
	CHECK(r.forwardZ == Approx(1.0f));
}

TEST_CASE("The minimum bend radius is reported", "[section_path_map]")
{
	// Quarter circle of radius 30 around (30, 0), from (0, 0) heading +Z.
	PathSpline spline;
	spline.interpolation = SplineInterpolation::Linear;
	constexpr int steps = 24;
	for (int i = 0; i <= steps; ++i)
	{
		const float a = 1.5707963f * static_cast<float>(i) / static_cast<float>(steps);
		SplinePoint p;
		p.x = 30.0f - 30.0f * std::cos(a);
		p.z = 30.0f * std::sin(a);
		spline.points.push_back(p);
	}

	const SectionLayout layout = Lay(spline, Ids(spline.points.size()), 20.0f);
	CHECK(layout.minimumBendRadius == Approx(30.0f).margin(2.0f));
}

TEST_CASE("Points project onto the path", "[section_path_map]")
{
	const SampledPath path(Straight({ 0.0f, 100.0f }), kSectionPathSampleStep);
	float distance = 0.0f;
	float across = 0.0f;
	REQUIRE(ProjectToPath(path, 3.0f, 40.0f, 10.0f, distance, across));
	CHECK(distance == Approx(40.0f));
	CHECK(across == Approx(3.0f));

	REQUIRE(ProjectToPath(path, -2.0f, 10.0f, 10.0f, distance, across));
	CHECK(across == Approx(-2.0f));

	CHECK_FALSE(ProjectToPath(path, 20.0f, 40.0f, 10.0f, distance, across));
}

TEST_CASE("Copies are found by arc distance", "[section_path_map]")
{
	const SectionLayout layout = Lay(Straight({ 0.0f, 45.0f, 100.0f }), Ids(3), 20.0f);
	float localZ = 0.0f;
	const SectionCopy* copy = FindCopy(layout, 11.25f, 20.0f, localZ);
	REQUIRE(copy != nullptr);
	CHECK(copy->span == 0);
	CHECK(copy->copy == 0);
	CHECK(localZ == Approx(10.0f));

	copy = FindCopy(layout, 100.0f, 20.0f, localZ);
	REQUIRE(copy != nullptr);
	CHECK(localZ == Approx(20.0f));
	CHECK(FindCopy(layout, 101.0f, 20.0f, localZ) == nullptr);
}

TEST_CASE("Moving a point only touches the adjacent spans", "[section_path_map]")
{
	// Points 1..3 at z 30, 60/45, 60; moving point 2 keeps the summed length of spans 1 and 2.
	const SectionLayout before = Lay(FourPoints(30.0f, 30.0f), Ids(5), 20.0f);
	const SectionLayout after = Lay(FourPoints(0.0f, 60.0f), Ids(5), 20.0f);
	REQUIRE(before.copies.size() == after.copies.size());

	for (size_t i = 0; i < before.copies.size(); ++i)
	{
		const SectionCopy& a = before.copies[i];
		const SectionCopy& b = after.copies[i];
		CHECK(a.spanKey == b.spanKey);
		if (a.span == 0 || a.span == 3)
		{
			CHECK(a.startDistance == Approx(b.startDistance));
			CHECK(a.stretchedLength == Approx(b.stretchedLength));
		}
	}
}

TEST_CASE("Inserting a point keeps the other span keys", "[section_path_map]")
{
	const SectionLayout before = Lay(Straight({ 0.0f, 30.0f, 60.0f, 90.0f }), Ids(4), 20.0f);

	std::vector<uint64> ids = { 100, 101, 999, 102, 103 };
	const SectionLayout after = Lay(Straight({ 0.0f, 30.0f, 45.0f, 60.0f, 90.0f }), ids, 20.0f);

	auto keyOfSpan = [](const SectionLayout& layout, const uint32 span)
	{
		for (const SectionCopy& c : layout.copies)
		{
			if (c.span == span)
			{
				return c.spanKey;
			}
		}
		return uint64(0);
	};

	CHECK(keyOfSpan(before, 0) == keyOfSpan(after, 0));
	CHECK(keyOfSpan(before, 2) == keyOfSpan(after, 3));
	CHECK(keyOfSpan(before, 1) != keyOfSpan(after, 1));
}

TEST_CASE("Copy lookup has no gaps at boundaries or at the path end", "[section_path_map]")
{
	const PathSpline spline = Straight({ 0.0f, 45.0f, 100.0f });
	const SampledPath path(spline, kSectionPathSampleStep);
	const SectionLayout layout = LayOutSections(spline, path, Ids(3), 20.0f);

	float localZ = 0.0f;
	for (const SectionCopy& expected : layout.copies)
	{
		const SectionCopy* found = FindCopy(layout, expected.startDistance, 20.0f, localZ);
		REQUIRE(found != nullptr);
		CHECK(found->span == expected.span);
		CHECK(found->copy == expected.copy);
		CHECK(localZ == Approx(0.0f).margin(1e-3f));
	}

	const SectionCopy* last = FindCopy(layout, path.GetLength(), 20.0f, localZ);
	REQUIRE(last != nullptr);
	CHECK(last == &layout.copies.back());
	CHECK(localZ == Approx(20.0f).margin(1e-2f));
}
