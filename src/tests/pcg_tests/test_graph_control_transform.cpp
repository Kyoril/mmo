// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/graph/control_transform.h"
#include "pcg/graph/rectangle_shape.h"

#include "math/quaternion.h"
#include "math/radian.h"

#include <cmath>
#include <limits>
#include <set>

using namespace mmo;
using namespace mmo::pcg::graph;

namespace
{
	constexpr float HalfTurnRadians = 3.14159265358979f;

	/// A closed square loop (0,0)(10,0)(10,10)(0,10) with heights 1..4, widths 2 and point ids 11..14.
	ControlInstance MakeSquareLoop(const SplineInterpolation interpolation)
	{
		ControlInstance inst;
		inst.id = 0x42;
		inst.spline.closed = true;
		inst.spline.interpolation = interpolation;
		const float coords[4][2] = { { 0, 0 }, { 10, 0 }, { 10, 10 }, { 0, 10 } };
		for (int i = 0; i < 4; ++i)
		{
			SplinePoint p;
			p.x = coords[i][0];
			p.z = coords[i][1];
			p.y = static_cast<float>(i + 1);
			p.width = 2.0f;
			inst.spline.points.push_back(p);
			inst.pointIds.push_back(static_cast<uint64>(11 + i));
		}
		inst.nextPointId = 15;
		return inst;
	}

	/// Whether two instances have exactly the same spline points.
	bool SamePoints(const ControlInstance& a, const ControlInstance& b)
	{
		if (a.spline.points.size() != b.spline.points.size())
		{
			return false;
		}

		for (size_t i = 0; i < a.spline.points.size(); ++i)
		{
			const SplinePoint& p = a.spline.points[i];
			const SplinePoint& q = b.spline.points[i];
			if (p.x != q.x || p.z != q.z || p.y != q.y || p.width != q.width || p.inX != q.inX || p.inZ != q.inZ
				|| p.outX != q.outX || p.outZ != q.outZ || p.inY != q.inY || p.outY != q.outY || p.autoTangents != q.autoTangents)
			{
				return false;
			}
		}
		return true;
	}
}

TEST_CASE("translating a loop moves every point and keeps ids", "[graph_control_transform]")
{
	ControlInstance inst = MakeSquareLoop(SplineInterpolation::CatmullRom);
	const ControlInstance before = inst;

	TranslateControl(inst, Vector3(3.0f, 2.0f, -4.0f));

	REQUIRE(inst.spline.points.size() == 4);
	for (size_t i = 0; i < 4; ++i)
	{
		CHECK(inst.spline.points[i].x == Approx(before.spline.points[i].x + 3.0f));
		CHECK(inst.spline.points[i].z == Approx(before.spline.points[i].z - 4.0f));
		CHECK(inst.spline.points[i].y == Approx(before.spline.points[i].y + 2.0f));
		CHECK(inst.spline.points[i].width == before.spline.points[i].width);
	}
	CHECK(inst.pointIds == before.pointIds);
	CHECK(inst.nextPointId == before.nextPointId);

	const Vector3 pivot = ControlPivot(inst);
	CHECK(pivot.x == Approx(8.0f));
	CHECK(pivot.z == Approx(1.0f));
	CHECK(pivot.y == Approx(4.5f));
}

TEST_CASE("rotating a spline turns points and tangent handles about the centroid", "[graph_control_transform]")
{
	ControlInstance inst = MakeSquareLoop(SplineInterpolation::CatmullRom);
	inst.spline.points[1].autoTangents = false;
	inst.spline.points[1].outX = 2.0f;
	inst.spline.points[1].outZ = 0.0f;
	inst.spline.points[1].inX = -2.0f;
	inst.spline.points[1].inZ = 0.0f;
	const ControlInstance before = inst;

	RotateControl(inst, HalfTurnRadians * 0.5f);

	const Vector3 pivot = ControlPivot(inst);
	CHECK(pivot.x == Approx(5.0f));
	CHECK(pivot.z == Approx(5.0f));

	// The engine's yaw (SelectedControlTransform::FromYaw = Quaternion(yaw, +Y)) turns +X towards -Z.
	const Quaternion yaw(Radian(HalfTurnRadians * 0.5f), Vector3::UnitY);
	for (size_t i = 0; i < 4; ++i)
	{
		const SplinePoint& p = before.spline.points[i];
		const Vector3 expected = yaw * Vector3(p.x - 5.0f, 0.0f, p.z - 5.0f);
		CHECK(inst.spline.points[i].x == Approx(5.0f + expected.x).margin(1.0e-4));
		CHECK(inst.spline.points[i].z == Approx(5.0f + expected.z).margin(1.0e-4));
		CHECK(inst.spline.points[i].y == p.y);
		CHECK(inst.spline.points[i].width == p.width);
	}

	// Point (10,0) is offset (5,-5) from the pivot and lands on (0,0); its out handle (2,0) turns to (0,-2).
	const SplinePoint& turned = inst.spline.points[1];
	CHECK(turned.x == Approx(0.0f).margin(1.0e-4));
	CHECK(turned.z == Approx(0.0f).margin(1.0e-4));
	CHECK(turned.outX == Approx(0.0f).margin(1.0e-4));
	CHECK(turned.outZ == Approx(-2.0f).margin(1.0e-4));
	CHECK(turned.inX == Approx(0.0f).margin(1.0e-4));
	CHECK(turned.inZ == Approx(2.0f).margin(1.0e-4));
	CHECK_FALSE(turned.autoTangents);
	CHECK(inst.pointIds == before.pointIds);
	CHECK(inst.nextPointId == before.nextPointId);
}

TEST_CASE("scaling scales offsets from the centroid and the tangents", "[graph_control_transform]")
{
	ControlInstance inst = MakeSquareLoop(SplineInterpolation::CatmullRom);
	inst.spline.points[0].autoTangents = false;
	inst.spline.points[0].outX = 1.0f;
	inst.spline.points[0].outZ = 3.0f;
	inst.spline.points[0].outY = 0.5f;
	const ControlInstance before = inst;

	ScaleControl(inst, 2.0f, 1.0f);

	const Vector3 pivot = ControlPivot(inst);
	CHECK(pivot.x == Approx(5.0f));
	CHECK(pivot.z == Approx(5.0f));
	for (size_t i = 0; i < 4; ++i)
	{
		const SplinePoint& p = before.spline.points[i];
		CHECK(inst.spline.points[i].x == Approx(5.0f + (p.x - 5.0f) * 2.0f));
		CHECK(inst.spline.points[i].z == Approx(p.z));
		CHECK(inst.spline.points[i].y == p.y);
		CHECK(inst.spline.points[i].width == p.width);
	}
	CHECK(inst.spline.points[0].outX == Approx(2.0f));
	CHECK(inst.spline.points[0].outZ == Approx(3.0f));
	CHECK(inst.spline.points[0].outY == 0.5f);
	CHECK(inst.pointIds == before.pointIds);
	CHECK(inst.nextPointId == before.nextPointId);
}

TEST_CASE("scale factors are clamped and non-finite ones ignored", "[graph_control_transform]")
{
	SECTION("zero and negative factors clamp to the minimum")
	{
		ControlInstance inst = MakeSquareLoop(SplineInterpolation::Linear);
		const ControlInstance before = inst;

		ScaleControl(inst, 0.0f, -3.0f);

		std::set<std::pair<float, float>> distinct;
		for (size_t i = 0; i < 4; ++i)
		{
			const SplinePoint& p = inst.spline.points[i];
			const SplinePoint& q = before.spline.points[i];
			distinct.emplace(p.x, p.z);

			// Same side of the pivot as before: the offsets keep their signs.
			CHECK((p.x - 5.0f) * (q.x - 5.0f) > 0.0f);
			CHECK((p.z - 5.0f) * (q.z - 5.0f) > 0.0f);
			CHECK(p.x - 5.0f == Approx((q.x - 5.0f) * 0.01f));
		}
		CHECK(distinct.size() == 4);
	}

	SECTION("a non-finite factor counts as one")
	{
		ControlInstance inst = MakeSquareLoop(SplineInterpolation::Linear);
		const ControlInstance before = inst;

		ScaleControl(inst, std::numeric_limits<float>::quiet_NaN(), 1.0f);
		CHECK(SamePoints(inst, before));

		ScaleControl(inst, 1.0f, std::numeric_limits<float>::infinity());
		CHECK(SamePoints(inst, before));
	}

	SECTION("huge factors clamp to the maximum")
	{
		ControlInstance inst = MakeSquareLoop(SplineInterpolation::Linear);
		ScaleControl(inst, 1000.0f, 1.0f);
		CHECK(inst.spline.points[1].x - 5.0f == Approx(500.0f));
	}

	SECTION("transform half extents stay at the minimum half extent")
	{
		ControlInstance rectangle;
		rectangle.halfExtents = Vector3(10.0f, 3.0f, 10.0f);
		ScaleControl(rectangle, 0.01f, 0.01f);
		CHECK(rectangle.halfExtents.x == MinRectangleHalfExtent);
		CHECK(rectangle.halfExtents.z == MinRectangleHalfExtent);
		CHECK(rectangle.halfExtents.y == 3.0f);
	}

	SECTION("a point control's zero half extents stay zero")
	{
		ControlInstance point;
		point.halfExtents = Vector3(0.0f, 0.0f, 0.0f);
		ScaleControl(point, 0.01f, 50.0f);
		CHECK(point.halfExtents.x == 0.0f);
		CHECK(point.halfExtents.y == 0.0f);
		CHECK(point.halfExtents.z == 0.0f);
	}
}

TEST_CASE("a linear spline keeps its heights when translated in y", "[graph_control_transform]")
{
	ControlInstance inst = MakeSquareLoop(SplineInterpolation::Linear);
	inst.spline.closed = false;
	const ControlInstance before = inst;

	TranslateControl(inst, Vector3(1.0f, 7.0f, 2.0f));

	for (size_t i = 0; i < 4; ++i)
	{
		CHECK(inst.spline.points[i].x == Approx(before.spline.points[i].x + 1.0f));
		CHECK(inst.spline.points[i].z == Approx(before.spline.points[i].z + 2.0f));
		CHECK(inst.spline.points[i].y == before.spline.points[i].y);
	}
}

TEST_CASE("transform shapes route to position, rotation and half extents", "[graph_control_transform]")
{
	ControlInstance inst;
	inst.id = 7;
	inst.position = Vector3(10.0f, 2.0f, -3.0f);
	inst.rotation = Quaternion(Radian(0.25f), Vector3::UnitY);
	inst.halfExtents = Vector3(4.0f, 5.0f, 6.0f);

	const Vector3 pivot = ControlPivot(inst);
	CHECK(pivot.x == Approx(10.0f));
	CHECK(pivot.y == Approx(2.0f));
	CHECK(pivot.z == Approx(-3.0f));

	TranslateControl(inst, Vector3(1.0f, 1.0f, 1.0f));
	CHECK(inst.position.x == Approx(11.0f));
	CHECK(inst.position.y == Approx(3.0f));
	CHECK(inst.position.z == Approx(-2.0f));

	RotateControl(inst, 0.5f);
	const Vector3 rotatedX = inst.rotation * Vector3::UnitX;
	CHECK(std::atan2(-rotatedX.z, rotatedX.x) == Approx(0.75f));
	CHECK(inst.position.x == Approx(11.0f));
	CHECK(inst.position.z == Approx(-2.0f));

	ScaleControl(inst, 2.0f, 0.5f);
	CHECK(inst.halfExtents.x == Approx(8.0f));
	CHECK(inst.halfExtents.y == Approx(5.0f));
	CHECK(inst.halfExtents.z == Approx(3.0f));
	CHECK(inst.spline.points.empty());
	CHECK(inst.pointIds.empty());
}
