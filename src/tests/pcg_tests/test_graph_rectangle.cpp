// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/graph/controls.h"
#include "pcg/graph/evaluator.h"
#include "pcg/graph/node_registry.h"
#include "pcg/graph/rectangle_shape.h"

#include <cmath>
#include <limits>
#include <memory>

using namespace mmo;
using namespace mmo::pcg::graph;

namespace
{
	/// A control type of the given shape with no properties.
	ControlType MakeType(const ControlShape shape)
	{
		ControlType type;
		type.name = "Field";
		type.shape = shape;
		return type;
	}

	/// Evaluates GetControls over one control of the given type and instance and returns the single resulting item.
	DataItem GetItem(const ControlType& type, const ControlInstance& inst)
	{
		ResolvedControl rc;
		std::vector<String> warnings;
		REQUIRE(ResolveControl(type, inst, rc, warnings));
		std::vector<ResolvedControl> controls{ rc };

		Graph g;
		NodeInstance n;
		n.id = 1;
		n.type = "GetControls";
		g.nodes.push_back(n);
		g.nextNodeId = 2;
		EvalContext ctx;
		ctx.controls = &controls;
		const EvalResult result = Evaluate(g, NodeRegistry::Default(), ctx, nullptr);
		REQUIRE(result.ok);
		const Collection& out = result.nodes.at(1).output.pins.at("Out");
		REQUIRE(out.size() == 1);
		return out[0];
	}

	ControlInstance MakeInstance(const Vector3& position, const Vector3& halfExtents)
	{
		ControlInstance inst;
		inst.id = 7;
		inst.position = position;
		inst.halfExtents = halfExtents;
		return inst;
	}
}

TEST_CASE("rectangle type round trips", "[graph_rectangle]")
{
	const ControlType type = MakeType(ControlShape::Rectangle);
	const String text = WriteControlType(type);
	CHECK(text.find("\"shape\": \"rectangle\"") != String::npos);

	ControlType loaded;
	String error;
	REQUIRE(ParseControlType(text, loaded, error));
	CHECK(loaded.shape == ControlShape::Rectangle);

	// No spline points are needed.
	const ControlInstance inst = MakeInstance(Vector3(0, 0, 0), Vector3(2, 1, 2));
	ResolvedControl rc;
	std::vector<String> warnings;
	CHECK(ResolveControl(loaded, inst, rc, warnings));
}

TEST_CASE("GetControls emits a rectangle as a closed linear area", "[graph_rectangle]")
{
	const ControlType type = MakeType(ControlShape::Rectangle);
	const ControlInstance inst = MakeInstance(Vector3(10, 3, 20), Vector3(2, 9, 4));
	const DataItem item = GetItem(type, inst);

	CHECK(item.kind == ItemKind::Area);
	CHECK(item.spline.closed);
	CHECK(item.spline.interpolation == SplineInterpolation::Linear);
	REQUIRE(item.spline.points.size() == 4);

	const float expected[4][2] = { { 8, 16 }, { 12, 16 }, { 12, 24 }, { 8, 24 } };
	for (size_t i = 0; i < 4; ++i)
	{
		CHECK(item.spline.points[i].x == Approx(expected[i][0]));
		CHECK(item.spline.points[i].z == Approx(expected[i][1]));
		CHECK(item.spline.points[i].y == Approx(3.0f));
	}
	CHECK(item.pointIds == std::vector<uint64>({ 1, 2, 3, 4 }));
	CHECK(item.attributes.count("volumeHeight") == 0);
}

TEST_CASE("rectangle point ids and order survive resize and rotation", "[graph_rectangle]")
{
	const ControlType type = MakeType(ControlShape::Rectangle);
	ControlInstance inst = MakeInstance(Vector3(10, 3, 20), Vector3(6, 9, 1));
	const float yaw = 1.5707963f;
	inst.rotation = Quaternion(Radian(yaw), Vector3::UnitY);
	const DataItem item = GetItem(type, inst);

	CHECK(item.pointIds == std::vector<uint64>({ 1, 2, 3, 4 }));
	REQUIRE(item.spline.points.size() == 4);

	// Corner 0 is position + R(yaw) * (-6, -1), with R mapping local x to (cos, -sin) and local z to (sin, cos).
	const float c = std::cos(yaw);
	const float s = std::sin(yaw);
	const float lx = -6.0f;
	const float lz = -1.0f;
	CHECK(item.spline.points[0].x == Approx(10.0f + c * lx + s * lz).margin(1e-4));
	CHECK(item.spline.points[0].z == Approx(20.0f - s * lx + c * lz).margin(1e-4));

	const std::array<SplinePoint, 4> corners = RectangleCorners(inst);
	for (size_t i = 0; i < 4; ++i)
	{
		CHECK(corners[i].x == Approx(item.spline.points[i].x));
		CHECK(corners[i].z == Approx(item.spline.points[i].z));
	}
}

TEST_CASE("a volume is unchanged", "[graph_rectangle]")
{
	const ControlType type = MakeType(ControlShape::Volume);
	const ControlInstance inst = MakeInstance(Vector3(10, 3, 20), Vector3(2, 1.5f, 4));
	const DataItem item = GetItem(type, inst);

	CHECK(item.kind == ItemKind::Area);
	CHECK(item.spline.closed);
	CHECK(item.pointIds.empty());
	REQUIRE(item.attributes.count("volumeHeight") == 1);
	CHECK(std::get<float>(item.attributes.at("volumeHeight")) == Approx(3.0f));
	REQUIRE(item.spline.points.size() == 4);
	for (const SplinePoint& p : item.spline.points)
	{
		CHECK(p.y == Approx(1.5f));
	}
	CHECK(item.spline.points[0].x == Approx(8));
	CHECK(item.spline.points[0].z == Approx(16));
}

namespace
{
	/// Checks that two corners of a rectangle are at the same place on x/z.
	void CheckSameCorner(const SplinePoint& a, const SplinePoint& b)
	{
		CHECK(a.x == Approx(b.x).margin(1e-4));
		CHECK(a.z == Approx(b.z).margin(1e-4));
	}
}

TEST_CASE("dragging a side keeps the opposite side", "[graph_rectangle]")
{
	ControlInstance inst = MakeInstance(Vector3(0, 0, 0), Vector3(10, 2, 10));
	DragRectangleHandle(inst, RectangleHandle::SidePosX, 16.0f, 3.0f);

	CHECK(inst.position.x == Approx(3.0f));
	CHECK(inst.halfExtents.x == Approx(13.0f));
	CHECK(inst.position.z == Approx(0.0f));
	CHECK(inst.halfExtents.z == Approx(10.0f));
	CHECK(inst.halfExtents.y == Approx(2.0f));

	// The -x side is still at x == -10.
	const Vector3 negSide = RectangleHandlePosition(inst, RectangleHandle::SideNegX);
	CHECK(negSide.x == Approx(-10.0f));
	CHECK(negSide.z == Approx(0.0f));

	// The dragged side follows the cursor along x only.
	const Vector3 posSide = RectangleHandlePosition(inst, RectangleHandle::SidePosX);
	CHECK(posSide.x == Approx(16.0f));
	CHECK(posSide.z == Approx(0.0f));

	// The other sides keep their place: -z and +z.
	ControlInstance zInst = MakeInstance(Vector3(0, 0, 0), Vector3(10, 2, 10));
	DragRectangleHandle(zInst, RectangleHandle::SideNegZ, 5.0f, -20.0f);
	CHECK(zInst.position.x == Approx(0.0f));
	CHECK(zInst.halfExtents.x == Approx(10.0f));
	CHECK(zInst.position.z == Approx(-5.0f));
	CHECK(zInst.halfExtents.z == Approx(15.0f));
	CHECK(RectangleHandlePosition(zInst, RectangleHandle::SidePosZ).z == Approx(10.0f));
}

TEST_CASE("dragging a corner keeps the opposite corner", "[graph_rectangle]")
{
	ControlInstance inst = MakeInstance(Vector3(10, 4, 20), Vector3(5, 1, 3));
	const std::array<SplinePoint, 4> before = RectangleCorners(inst);

	// Corner (+x,+z) is index 2; its opposite (-x,-z) is index 0 at (5, 17).
	DragRectangleHandle(inst, RectangleHandle::CornerPosPos, 25.0f, 31.0f);
	CHECK(inst.halfExtents.x == Approx(10.0f));
	CHECK(inst.halfExtents.z == Approx(7.0f));
	CHECK(inst.position.x == Approx(15.0f));
	CHECK(inst.position.z == Approx(24.0f));
	CHECK(inst.position.y == Approx(4.0f));

	const std::array<SplinePoint, 4> after = RectangleCorners(inst);
	CheckSameCorner(after[0], before[0]);
	const Vector3 corner = RectangleHandlePosition(inst, RectangleHandle::CornerPosPos);
	CHECK(corner.x == Approx(25.0f));
	CHECK(corner.z == Approx(31.0f));
	CHECK(corner.y == Approx(4.0f));

	// Corner (-x,+z) keeps (+x,-z), index 1.
	ControlInstance other = MakeInstance(Vector3(10, 4, 20), Vector3(5, 1, 3));
	DragRectangleHandle(other, RectangleHandle::CornerNegPos, 0.0f, 30.0f);
	CheckSameCorner(RectangleCorners(other)[1], before[1]);
	const Vector3 otherCorner = RectangleHandlePosition(other, RectangleHandle::CornerNegPos);
	CHECK(otherCorner.x == Approx(0.0f).margin(1e-4));
	CHECK(otherCorner.z == Approx(30.0f));
}

TEST_CASE("dragging works in the rectangle's rotated frame", "[graph_rectangle]")
{
	for (const float degrees : { 90.0f, 37.0f })
	{
		const float yaw = degrees * 3.14159265f / 180.0f;
		ControlInstance inst = MakeInstance(Vector3(10, 0, 20), Vector3(6, 1, 4));
		inst.rotation = Quaternion(Radian(yaw), Vector3::UnitY);
		const std::array<SplinePoint, 4> before = RectangleCorners(inst);

		// Local x in world is (cos, -sin), local z is (sin, cos).
		const float c = std::cos(yaw);
		const float s = std::sin(yaw);

		// A cursor 3 m beyond the +x side along local x, and 2 m off along local z: only the +x side moves.
		const float lx = 9.0f;
		const float lz = 2.0f;
		const float wx = 10.0f + c * lx + s * lz;
		const float wz = 20.0f - s * lx + c * lz;
		DragRectangleHandle(inst, RectangleHandle::SidePosX, wx, wz);
		CHECK(inst.halfExtents.x == Approx(7.5f));
		CHECK(inst.halfExtents.z == Approx(4.0f));

		// The -x side's corners stay.
		const std::array<SplinePoint, 4> after = RectangleCorners(inst);
		CheckSameCorner(after[0], before[0]);
		CheckSameCorner(after[3], before[3]);

		// The dragged side's midpoint sits on the cursor's projection onto the local x axis.
		const Vector3 side = RectangleHandlePosition(inst, RectangleHandle::SidePosX);
		CHECK(side.x == Approx(10.0f + c * lx).margin(1e-4));
		CHECK(side.z == Approx(20.0f - s * lx).margin(1e-4));

		// A corner drag in the same rotated frame keeps the opposite corner.
		ControlInstance cornerInst = MakeInstance(Vector3(10, 0, 20), Vector3(6, 1, 4));
		cornerInst.rotation = Quaternion(Radian(yaw), Vector3::UnitY);
		const float cx = -8.0f;
		const float cz = 7.0f;
		DragRectangleHandle(cornerInst, RectangleHandle::CornerNegPos, 10.0f + c * cx + s * cz, 20.0f - s * cx + c * cz);
		CHECK(cornerInst.halfExtents.x == Approx(7.0f));
		CHECK(cornerInst.halfExtents.z == Approx(5.5f));
		CheckSameCorner(RectangleCorners(cornerInst)[1], before[1]);
	}
}

TEST_CASE("a side dragged past its opposite clamps at one metre", "[graph_rectangle]")
{
	ControlInstance inst = MakeInstance(Vector3(0, 0, 0), Vector3(10, 2, 10));
	DragRectangleHandle(inst, RectangleHandle::SidePosX, -50.0f, 0.0f);

	CHECK(inst.halfExtents.x == MinRectangleHalfExtent);
	CHECK(std::isfinite(inst.position.x));
	CHECK(std::isfinite(inst.position.z));
	CHECK(inst.halfExtents.z == Approx(10.0f));
	CHECK(RectangleHandlePosition(inst, RectangleHandle::SideNegX).x == Approx(-10.0f));

	// A corner dragged across both opposite sides clamps both axes and keeps the opposite corner.
	ControlInstance corner = MakeInstance(Vector3(0, 0, 0), Vector3(10, 2, 10));
	DragRectangleHandle(corner, RectangleHandle::CornerNegNeg, 40.0f, 40.0f);
	CHECK(corner.halfExtents.x == MinRectangleHalfExtent);
	CHECK(corner.halfExtents.z == MinRectangleHalfExtent);
	const Vector3 kept = RectangleHandlePosition(corner, RectangleHandle::CornerPosPos);
	CHECK(kept.x == Approx(10.0f));
	CHECK(kept.z == Approx(10.0f));
}

TEST_CASE("non-finite cursor positions are ignored", "[graph_rectangle]")
{
	const ControlInstance original = MakeInstance(Vector3(1, 2, 3), Vector3(10, 2, 10));
	const float bad[] = { std::nanf(""), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };
	for (const float value : bad)
	{
		ControlInstance inst = original;
		DragRectangleHandle(inst, RectangleHandle::SidePosX, value, 0.0f);
		DragRectangleHandle(inst, RectangleHandle::CornerNegPos, 0.0f, value);
		CHECK(inst.position.x == original.position.x);
		CHECK(inst.position.y == original.position.y);
		CHECK(inst.position.z == original.position.z);
		CHECK(inst.halfExtents.x == original.halfExtents.x);
		CHECK(inst.halfExtents.z == original.halfExtents.z);
	}
}
