// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/graph/controls.h"
#include "pcg/graph/evaluator.h"
#include "pcg/graph/node_registry.h"
#include "pcg/graph/rectangle_shape.h"

#include <cmath>
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
