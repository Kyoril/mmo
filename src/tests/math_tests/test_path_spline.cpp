// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "math/path_spline.h"

#include <cmath>

using namespace mmo;

namespace
{
	PathSpline MakeSquare(bool reversed = false)
	{
		PathSpline spline;
		spline.closed = true;
		spline.interpolation = SplineInterpolation::Linear;
		if (!reversed)
		{
			spline.points = { { 0.0f, 0.0f, 0.0f }, { 10.0f, 0.0f, 0.0f }, { 10.0f, 10.0f, 0.0f }, { 0.0f, 10.0f, 0.0f } };
		}
		else
		{
			spline.points = { { 10.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 10.0f, 0.0f }, { 10.0f, 10.0f, 0.0f } };
		}
		return spline;
	}

	PathSpline MakeOpenL(float w0 = 0.0f, float w1 = 0.0f, float w2 = 0.0f)
	{
		PathSpline spline;
		spline.closed = false;
		spline.points = { { 0.0f, 0.0f, w0 }, { 10.0f, 0.0f, w1 }, { 10.0f, 10.0f, w2 } };
		return spline;
	}
}

TEST_CASE("Linear closed square 10x10 at origin", "[path_spline]")
{
	const SampledPath path(MakeSquare(), 1.0f);

	CHECK(path.IsClosed());
	CHECK(path.GetLength() == Approx(40.0f).margin(1e-4));
	CHECK(path.Contains(5.0f, 5.0f));
	CHECK_FALSE(path.Contains(15.0f, 5.0f));
	CHECK(path.SignedDistance(5.0f, 5.0f) == Approx(-5.0f).margin(1e-4));
	CHECK(path.SignedDistance(15.0f, 5.0f) == Approx(5.0f).margin(1e-4));

	float minX, minZ, maxX, maxZ;
	path.GetBounds(minX, minZ, maxX, maxZ);
	CHECK(minX == Approx(0.0f));
	CHECK(minZ == Approx(0.0f));
	CHECK(maxX == Approx(10.0f));
	CHECK(maxZ == Approx(10.0f));
}

TEST_CASE("Linear open path", "[path_spline]")
{
	const SampledPath path(MakeOpenL(), 1.0f);

	CHECK_FALSE(path.IsClosed());
	CHECK(path.GetLength() == Approx(20.0f).margin(1e-4));
	CHECK(path.SignedDistance(5.0f, 3.0f) == Approx(3.0f).margin(1e-4));
	CHECK_FALSE(path.Contains(5.0f, 3.0f));
}

TEST_CASE("Catmull-Rom passes through control points", "[path_spline]")
{
	PathSpline spline;
	spline.closed = true;
	spline.interpolation = SplineInterpolation::CatmullRom;
	spline.points = { { 20.0f, 0.0f, 0.0f }, { 0.0f, 20.0f, 0.0f }, { -20.0f, 0.0f, 0.0f }, { 0.0f, -20.0f, 0.0f } };

	const SampledPath path(spline, 1.0f);
	for (const SplinePoint& p : spline.points)
	{
		bool found = false;
		for (const SplineSample& s : path.GetSamples())
		{
			if (std::fabs(s.x - p.x) < 1e-3f && std::fabs(s.z - p.z) < 1e-3f)
			{
				found = true;
				break;
			}
		}
		CHECK(found);
	}
}

TEST_CASE("Catmull-Rom circle area", "[path_spline]")
{
	PathSpline spline;
	spline.closed = true;
	spline.interpolation = SplineInterpolation::CatmullRom;
	for (int i = 0; i < 16; ++i)
	{
		const float a = static_cast<float>(i) * 6.28318530718f / 16.0f;
		spline.points.push_back({ 40.0f * std::cos(a), 40.0f * std::sin(a), 0.0f });
	}

	const SampledPath path(spline, 1.0f);
	CHECK(path.Contains(0.0f, 0.0f));
	CHECK(std::fabs(path.SignedDistance(0.0f, 0.0f) + 40.0f) < 0.5f);
}

TEST_CASE("Outward normal", "[path_spline]")
{
	for (const bool reversed : { false, true })
	{
		const SampledPath path(MakeSquare(reversed), 1.0f);
		float nx = 0.0f, nz = 0.0f;
		path.NormalAtDistance(5.0f, nx, nz);
		CHECK(nx == Approx(0.0f).margin(1e-4));
		CHECK(nz == Approx(-1.0f).margin(1e-4));
	}
}

TEST_CASE("Width interpolates linearly between points", "[path_spline]")
{
	const SampledPath path(MakeOpenL(2.0f, 6.0f, 6.0f), 1.0f);
	const SplineSample s = path.SampleAtDistance(5.0f);
	CHECK(s.width == Approx(4.0f).margin(1e-4));
	CHECK(s.x == Approx(5.0f).margin(1e-4));
	CHECK(s.z == Approx(0.0f).margin(1e-4));
	CHECK(path.SegmentAtDistance(5.0f) == 0);
	CHECK(path.SegmentAtDistance(15.0f) == 1);
}

TEST_CASE("Self intersection", "[path_spline]")
{
	PathSpline bowTie;
	bowTie.closed = true;
	bowTie.points = { { 0.0f, 0.0f, 0.0f }, { 10.0f, 10.0f, 0.0f }, { 10.0f, 0.0f, 0.0f }, { 0.0f, 10.0f, 0.0f } };

	CHECK(SampledPath(bowTie, 1.0f).IsSelfIntersecting());
	CHECK_FALSE(SampledPath(MakeSquare(), 1.0f).IsSelfIntersecting());
}

TEST_CASE("SampleAtDistance wraps for closed paths", "[path_spline]")
{
	const SampledPath path(MakeSquare(), 1.0f);
	const SplineSample a = path.SampleAtDistance(45.0f);
	const SplineSample b = path.SampleAtDistance(5.0f);
	CHECK(a.x == Approx(b.x).margin(1e-4));
	CHECK(a.z == Approx(b.z).margin(1e-4));
}
