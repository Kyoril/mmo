// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "math/path_spline.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

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

TEST_CASE("Winding and normal are stable at large world coordinates", "[path_spline]")
{
	for (const bool reversed : { false, true })
	{
		PathSpline spline = MakeSquare(reversed);
		for (SplinePoint& p : spline.points)
		{
			p.x += 20000.0f;
			p.z += 20000.0f;
		}

		const SampledPath path(spline, 1.0f);
		float nx = 0.0f, nz = 0.0f;
		path.NormalAtDistance(5.0f, nx, nz);
		CHECK(nx == Approx(0.0f).margin(1e-4));
		CHECK(nz == Approx(-1.0f).margin(1e-4));
		CHECK(path.SignedDistance(20005.0f, 20005.0f) == Approx(-5.0f).margin(1e-3));
	}
}

TEST_CASE("Winding of a 2x2 square far from the origin", "[path_spline]")
{
	// The enclosed area (4) is tiny against the products of the absolute coordinates (4e8), so a
	// shoelace sum in float loses the sign of the winding here.
	for (const bool reversed : { false, true })
	{
		PathSpline spline;
		spline.closed = true;
		spline.interpolation = SplineInterpolation::Linear;
		spline.points = reversed
			? std::vector<SplinePoint>{ { 20002.0f, 20000.0f, 0.0f }, { 20000.0f, 20000.0f, 0.0f }, { 20000.0f, 20002.0f, 0.0f }, { 20002.0f, 20002.0f, 0.0f } }
			: std::vector<SplinePoint>{ { 20000.0f, 20000.0f, 0.0f }, { 20002.0f, 20000.0f, 0.0f }, { 20002.0f, 20002.0f, 0.0f }, { 20000.0f, 20002.0f, 0.0f } };

		const SampledPath path(spline, 0.5f);

		// Distance 1 lies on the edge z = 20000 in both windings; outward is -Z.
		float nx = 0.0f, nz = 0.0f;
		path.NormalAtDistance(1.0f, nx, nz);
		CHECK(nx == Approx(0.0f).margin(1e-4));
		CHECK(nz == Approx(-1.0f).margin(1e-4));

		CHECK(path.SignedDistance(20001.0f, 20001.0f) == Approx(-1.0f).margin(1e-3));
		CHECK(path.SignedDistance(20001.0f, 19998.0f) == Approx(2.0f).margin(1e-3));
		CHECK(path.Contains(20001.0f, 20001.0f));
		CHECK_FALSE(path.Contains(20001.0f, 19998.0f));
	}
}

namespace
{
	/// Uniform Catmull-Rom sampling as the path spline did it before tangents existed: the reference
	/// an all-auto spline must still match.
	std::vector<SplineSample> SampleUniformCatmullRom(const PathSpline& spline, const float maxStep)
	{
		const auto& pts = spline.points;
		const int n = static_cast<int>(pts.size());
		const bool closed = spline.closed && n >= 3;
		const int segmentCount = closed ? n : n - 1;
		auto fetch = [&](int i) -> const SplinePoint&
		{
			if (closed)
			{
				return pts[((i % n) + n) % n];
			}
			return pts[std::clamp(i, 0, n - 1)];
		};
		auto cr = [](float p0, float p1, float p2, float p3, float t)
		{
			const float t2 = t * t;
			const float t3 = t2 * t;
			return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
		};

		std::vector<SplineSample> samples;
		for (int s = 0; s < segmentCount; ++s)
		{
			const SplinePoint& p0 = fetch(s - 1);
			const SplinePoint& p1 = fetch(s);
			const SplinePoint& p2 = fetch(s + 1);
			const SplinePoint& p3 = fetch(s + 2);
			const float chord = std::sqrt((p2.x - p1.x) * (p2.x - p1.x) + (p2.z - p1.z) * (p2.z - p1.z));
			const int steps = std::max(1, static_cast<int>(std::ceil(chord / maxStep)));
			for (int k = 0; k < steps; ++k)
			{
				const float t = static_cast<float>(k) / static_cast<float>(steps);
				samples.push_back({ cr(p0.x, p1.x, p2.x, p3.x, t), cr(p0.z, p1.z, p2.z, p3.z, t), 0.0f, 0.0f });
			}
		}
		if (!closed)
		{
			samples.push_back({ pts.back().x, pts.back().z, 0.0f, 0.0f });
		}
		return samples;
	}

	PathSpline MakeCircle(int count, float radius)
	{
		PathSpline spline;
		spline.closed = true;
		spline.interpolation = SplineInterpolation::CatmullRom;
		for (int i = 0; i < count; ++i)
		{
			const float a = static_cast<float>(i) * 6.28318530718f / static_cast<float>(count);
			spline.points.push_back({ radius * std::cos(a), radius * std::sin(a), 0.0f });
		}
		return spline;
	}

	PathSpline MakeOpenCurve()
	{
		PathSpline spline;
		spline.closed = false;
		spline.interpolation = SplineInterpolation::CatmullRom;
		spline.points = { { 0.0f, 0.0f, 0.0f }, { 20.0f, 5.0f, 0.0f }, { 35.0f, 30.0f, 0.0f }, { 10.0f, 45.0f, 0.0f } };
		return spline;
	}

	PathSpline MakeDiamond()
	{
		PathSpline spline;
		spline.closed = true;
		spline.interpolation = SplineInterpolation::CatmullRom;
		spline.points = { { 20.0f, 0.0f, 0.0f }, { 0.0f, 20.0f, 0.0f }, { -20.0f, 0.0f, 0.0f }, { 0.0f, -20.0f, 0.0f } };
		return spline;
	}

	/// Largest distance between corresponding samples; infinity if the counts differ.
	float MaxSampleDeviation(const std::vector<SplineSample>& a, const std::vector<SplineSample>& b)
	{
		if (a.size() != b.size())
		{
			return std::numeric_limits<float>::infinity();
		}
		float worst = 0.0f;
		for (size_t i = 0; i < a.size(); ++i)
		{
			worst = std::max(worst, std::hypot(a[i].x - b[i].x, a[i].z - b[i].z));
		}
		return worst;
	}

	float AngleBetweenDegrees(float ax, float az, float bx, float bz)
	{
		const float dot = (ax * bx + az * bz) / (std::hypot(ax, az) * std::hypot(bx, bz));
		return std::acos(std::clamp(dot, -1.0f, 1.0f)) * 57.2957795f;
	}

	/// Checks that changing a tangent changed samples only on one control segment.
	void CheckOnlySegmentChanged(const PathSpline& before, const PathSpline& after, uint32 changedSegment)
	{
		const SampledPath a(before, 1.0f);
		const SampledPath b(after, 1.0f);
		REQUIRE(a.GetSamples().size() == b.GetSamples().size());

		float changedMax = 0.0f;
		for (size_t i = 0; i < a.GetSamples().size(); ++i)
		{
			const SplineSample& sa = a.GetSamples()[i];
			const SplineSample& sb = b.GetSamples()[i];
			const float deviation = std::hypot(sa.x - sb.x, sa.z - sb.z);
			const uint32 segment = a.SegmentAtDistance(sa.distance);
			if (segment == changedSegment)
			{
				changedMax = std::max(changedMax, deviation);
			}
			else
			{
				INFO("sample " << i << " on segment " << segment);
				CHECK(deviation < 1e-5f);
			}
		}
		CHECK(changedMax > 0.1f);
	}
}

TEST_CASE("All-auto curved splines sample like uniform Catmull-Rom", "[path_spline]")
{
	for (const PathSpline& spline : { MakeCircle(16, 40.0f), MakeOpenCurve(), MakeDiamond() })
	{
		const SampledPath path(spline, 1.0f);
		CHECK(MaxSampleDeviation(path.GetSamples(), SampleUniformCatmullRom(spline, 1.0f)) < 1e-3f);
	}
}

TEST_CASE("Effective tangents of auto points are the Catmull-Rom ones", "[path_spline]")
{
	const PathSpline closed = MakeDiamond();
	float inX, inZ, outX, outZ;
	GetEffectiveTangents(closed, 0, inX, inZ, outX, outZ);
	// (p1 - p3) / 6 = ((0,20) - (0,-20)) / 6
	CHECK(outX == Approx(0.0f).margin(1e-5));
	CHECK(outZ == Approx(40.0f / 6.0f).margin(1e-5));
	CHECK(inX == Approx(-outX).margin(1e-5));
	CHECK(inZ == Approx(-outZ).margin(1e-5));

	// Open ends duplicate the end point.
	const PathSpline open = MakeOpenCurve();
	GetEffectiveTangents(open, 0, inX, inZ, outX, outZ);
	CHECK(outX == Approx(20.0f / 6.0f).margin(1e-5));
	CHECK(outZ == Approx(5.0f / 6.0f).margin(1e-5));
	GetEffectiveTangents(open, 3, inX, inZ, outX, outZ);
	CHECK(inX == Approx(25.0f / 6.0f).margin(1e-5));
	CHECK(inZ == Approx(-15.0f / 6.0f).margin(1e-5));

	// Manual points report their stored values.
	PathSpline manual = MakeDiamond();
	manual.points[2].autoTangents = false;
	manual.points[2].inX = 1.0f;
	manual.points[2].inZ = 2.0f;
	manual.points[2].outX = 3.0f;
	manual.points[2].outZ = 4.0f;
	GetEffectiveTangents(manual, 2, inX, inZ, outX, outZ);
	CHECK(inX == 1.0f);
	CHECK(inZ == 2.0f);
	CHECK(outX == 3.0f);
	CHECK(outZ == 4.0f);
}

TEST_CASE("Making tangents manual does not move the curve", "[path_spline]")
{
	for (const PathSpline& original : { MakeCircle(16, 40.0f), MakeOpenCurve(), MakeDiamond() })
	{
		PathSpline spline = original;
		const std::vector<SplineSample> before = SampledPath(spline, 1.0f).GetSamples();
		for (size_t i = 0; i < spline.points.size(); ++i)
		{
			MakeTangentsManual(spline, i);
			CHECK_FALSE(spline.points[i].autoTangents);
			CHECK(MaxSampleDeviation(SampledPath(spline, 1.0f).GetSamples(), before) < 1e-4f);
		}
	}
}

TEST_CASE("Out tangent shapes only its own segment, in tangent only the previous one", "[path_spline]")
{
	const PathSpline base = MakeCircle(8, 40.0f);

	SECTION("out tangent of point 3")
	{
		PathSpline changed = base;
		MakeTangentsManual(changed, 3);
		changed.points[3].outX += 8.0f;
		changed.points[3].outZ -= 5.0f;
		CheckOnlySegmentChanged(base, changed, 3);
	}

	SECTION("in tangent of point 3")
	{
		PathSpline changed = base;
		MakeTangentsManual(changed, 3);
		changed.points[3].inX += 8.0f;
		changed.points[3].inZ -= 5.0f;
		CheckOnlySegmentChanged(base, changed, 2);
	}

	SECTION("in tangent of point 0 wraps to the closing segment")
	{
		PathSpline changed = base;
		MakeTangentsManual(changed, 0);
		changed.points[0].inX += 8.0f;
		changed.points[0].inZ -= 5.0f;
		CheckOnlySegmentChanged(base, changed, 7);
	}
}

TEST_CASE("Independent handles make a corner", "[path_spline]")
{
	PathSpline spline = MakeDiamond();
	// Point 1 at (0, 20): arrive heading +x, leave heading -z (a 90 degree turn).
	SplinePoint& p = spline.points[1];
	p.autoTangents = false;
	p.inX = -6.0f;
	p.inZ = 0.0f;
	p.outX = 0.0f;
	p.outZ = -6.0f;

	const SampledPath path(spline, 0.5f);
	const auto& samples = path.GetSamples();
	size_t at = samples.size();
	for (size_t i = 0; i < samples.size(); ++i)
	{
		if (std::hypot(samples[i].x - p.x, samples[i].z - p.z) < 1e-4f)
		{
			at = i;
			break;
		}
	}
	REQUIRE(at < samples.size());
	REQUIRE(at > 0);
	REQUIRE(at + 1 < samples.size());

	const SplineSample& before = samples[at - 1];
	const SplineSample& after = samples[at + 1];
	const float angle = AngleBetweenDegrees(p.x - before.x, p.z - before.z, after.x - p.x, after.z - p.z);
	CHECK(angle > 30.0f);
}

TEST_CASE("Linear splines ignore tangents", "[path_spline]")
{
	const PathSpline plain = MakeSquare();
	PathSpline withTangents = plain;
	for (SplinePoint& p : withTangents.points)
	{
		p.autoTangents = false;
		p.inX = 3.0f;
		p.inZ = -7.0f;
		p.outX = -2.0f;
		p.outZ = 9.0f;
	}
	CHECK(MaxSampleDeviation(SampledPath(withTangents, 1.0f).GetSamples(), SampledPath(plain, 1.0f).GetSamples()) == 0.0f);
}
