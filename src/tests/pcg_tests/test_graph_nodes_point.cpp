// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "fake_terrain_sampler.h"
#include "graph_node_test_util.h"

#include "pcg/graph/evaluator.h"
#include "pcg/graph/node_registry.h"

#include <cmath>
#include <set>

using namespace mmo;
using namespace mmo::pcg::graph;

namespace
{
	constexpr float Deg = 180.0f / 3.14159265358979f;

	DataItem ManyPoints(const size_t count)
	{
		std::vector<std::pair<float, float>> xz;
		for (size_t i = 0; i < count; ++i)
		{
			xz.emplace_back(static_cast<float>(i % 20) * 3.0f, static_cast<float>(i / 20) * 3.0f);
		}
		return MakePoints(xz);
	}
}

TEST_CASE("Point operation node types are registered", "[graph_nodes_point]")
{
	for (const char* type : { "TransformPoints", "DensityNoise", "AttributeMath", "CopyAttributesFromControl" })
	{
		INFO(type);
		const NodeTypeDef* def = NodeRegistry::Default().Find(type);
		REQUIRE(def != nullptr);
		CHECK(def->category == "Point");
		REQUIRE(def->outputs.size() == 1);
		CHECK(def->outputs[0].name == "Out");
		REQUIRE_FALSE(def->inputs.empty());
		CHECK(def->inputs[0].name == "In");
		CHECK(def->inputs[0].required);
	}
	CHECK(NodeRegistry::Default().Find("TransformPoints")->contextDeps == context_dep::Terrain);
	for (const char* type : { "DensityNoise", "AttributeMath", "CopyAttributesFromControl" })
	{
		CHECK(NodeRegistry::Default().Find(type)->contextDeps == context_dep::None);
	}
}

TEST_CASE("Every point operation node outputs empty collections for empty inputs", "[graph_nodes_point]")
{
	FakeTerrainSampler terrain;
	EvalContext ctx;
	ctx.terrain = &terrain;
	DataItem emptyPoints;
	emptyPoints.kind = ItemKind::Points;
	for (const char* type : { "TransformPoints", "DensityNoise", "AttributeMath", "CopyAttributesFromControl" })
	{
		INFO(type);
		const NodeOutput none = ExecNode(type, {}, {}, ctx);
		CHECK(none.error.empty());
		CHECK(none.pins.at("Out").empty());
		const NodeOutput empty = ExecNode(type, {}, { { "In", {} }, { "Controls", {} } }, ctx);
		CHECK(empty.error.empty());
		CHECK(empty.pins.at("Out").empty());
		const NodeOutput emptyItem = ExecNode(type, {}, { { "In", { emptyPoints } }, { "Controls", { MakeSquare(0, 0, 5) } } }, ctx);
		CHECK(emptyItem.error.empty());
		CHECK(CountPoints(emptyItem.pins.at("Out")) == 0);
	}
}

TEST_CASE("Transform Points: ranges respected, deterministic, seeded per point", "[graph_nodes_point]")
{
	const EvalContext ctx;
	DataItem pts = ManyPoints(300);
	pts.sourceId = 8;
	const AttributeSet settings = {
		{ "offsetMin", Vector3(-1, 0, -2) }, { "offsetMax", Vector3(1, 0.5f, 2) },
		{ "yawMin", -45.0f }, { "yawMax", 90.0f },
		{ "scaleMin", Vector3(0.5f, 0.5f, 0.5f) }, { "scaleMax", Vector3(2, 2, 2) }
	};
	const NodeOutput out = ExecNode("TransformPoints", settings, { { "In", { pts } } }, ctx);
	const Collection& c = out.pins.at("Out");
	REQUIRE(c.size() == 1);
	CHECK(c[0].sourceId == 8);
	REQUIRE(c[0].points.points.size() == 300);
	std::set<float> yaws;
	for (size_t i = 0; i < 300; ++i)
	{
		const Point& before = pts.points.points[i];
		const Point& p = c[0].points.points[i];
		const Vector3 d = p.position - before.position;
		CHECK(d.x >= -1.0f);
		CHECK(d.x <= 1.0f);
		CHECK(d.y >= 0.0f);
		CHECK(d.y <= 0.5f);
		CHECK(d.z >= -2.0f);
		CHECK(d.z <= 2.0f);
		const Vector3 forward = p.rotation * Vector3(0, 0, 1);
		CHECK(forward.y == Approx(0.0f).margin(1e-4));
		const float yaw = std::atan2(forward.x, forward.z) * Deg;
		CHECK(yaw >= -45.01f);
		CHECK(yaw <= 90.01f);
		yaws.insert(std::round(yaw));
		// Uniform scale by default.
		CHECK(p.scale.x >= 0.5f);
		CHECK(p.scale.x <= 2.0f);
		CHECK(p.scale.y == p.scale.x);
		CHECK(p.scale.z == p.scale.x);
		CHECK(p.seed == before.seed);
	}
	CHECK(yaws.size() > 50);

	SECTION("deterministic, and the node seed decorrelates two nodes")
	{
		const NodeOutput again = ExecNode("TransformPoints", settings, { { "In", { pts } } }, ctx);
		CHECK(ContentHash(again.pins.at("Out")) == ContentHash(c));
		const NodeOutput otherNode = ExecNode("TransformPoints", settings, { { "In", { pts } } }, ctx, 999);
		CHECK(ContentHash(otherNode.pins.at("Out")) != ContentHash(c));
	}
	SECTION("a point's result depends only on its own seed")
	{
		DataItem shifted = MakePoints({ { 1000, 1000 } });
		shifted.points.points[0].seed = pts.points.points[0].seed;
		const Point a = ExecNode("TransformPoints", settings, { { "In", { pts } } }, ctx).pins.at("Out")[0].points.points[0];
		const Point b = ExecNode("TransformPoints", settings, { { "In", { shifted } } }, ctx).pins.at("Out")[0].points.points[0];
		CHECK(a.scale.x == b.scale.x);
		CHECK(a.rotation.w == b.rotation.w);
		CHECK(a.rotation.y == b.rotation.y);
	}
	SECTION("non-uniform scale ranges per axis")
	{
		const NodeOutput nonUniform = ExecNode("TransformPoints", { { "uniformScale", false }, { "scaleMin", Vector3(1, 2, 3) }, { "scaleMax", Vector3(1.5f, 2.5f, 3.5f) } }, { { "In", { pts } } }, ctx);
		size_t differ = 0;
		for (const Point& p : AllPoints(nonUniform.pins.at("Out")))
		{
			CHECK(p.scale.x >= 1.0f);
			CHECK(p.scale.x <= 1.5f);
			CHECK(p.scale.y >= 2.0f);
			CHECK(p.scale.y <= 2.5f);
			CHECK(p.scale.z >= 3.0f);
			CHECK(p.scale.z <= 3.5f);
			differ += (p.scale.y - 2.0f) != (p.scale.x - 1.0f) ? 1 : 0;
		}
		CHECK(differ > 250);
	}
	SECTION("pitch and roll ranges tilt the up axis by at most their angles")
	{
		const NodeOutput tilted = ExecNode("TransformPoints", { { "pitchMin", -10.0f }, { "pitchMax", 10.0f }, { "rollMin", -10.0f }, { "rollMax", 10.0f } }, { { "In", { pts } } }, ctx);
		size_t tiltedCount = 0;
		for (const Point& p : AllPoints(tilted.pins.at("Out")))
		{
			const Vector3 up = p.rotation * Vector3(0, 1, 0);
			const float angle = std::acos(std::min(1.0f, up.y)) * Deg;
			CHECK(angle <= 14.2f);
			tiltedCount += angle > 1.0f ? 1 : 0;
		}
		CHECK(tiltedCount > 200);
	}
	SECTION("upright resets the incoming rotation; keep composes with it")
	{
		DataItem rotated = pts;
		for (Point& p : rotated.points.points)
		{
			p.rotation = Quaternion(Radian(1.0f), Vector3::UnitX);
		}
		const Point upright = ExecNode("TransformPoints", { { "orientation", String("upright") } }, { { "In", { rotated } } }, ctx).pins.at("Out")[0].points.points[0];
		CHECK((upright.rotation * Vector3(0, 1, 0)).y == Approx(1.0f));
		const Point kept = ExecNode("TransformPoints", {}, { { "In", { rotated } } }, ctx).pins.at("Out")[0].points.points[0];
		CHECK((kept.rotation * Vector3(0, 1, 0)).y == Approx(std::cos(1.0f)).margin(1e-4));
	}
}

TEST_CASE("Transform Points aligns to the fake terrain normal", "[graph_nodes_point]")
{
	FakeTerrainSampler terrain;
	terrain.slopeX = 0.5f;
	terrain.slopeZ = -0.25f;
	EvalContext ctx;
	ctx.terrain = &terrain;
	const DataItem pts = ManyPoints(40);
	const Vector3 normal = terrain.Normal(0, 0);
	const NodeOutput out = ExecNode("TransformPoints", { { "orientation", String("alignToTerrain") }, { "yawMin", 0.0f }, { "yawMax", 360.0f } }, { { "In", { pts } } }, ctx);
	for (const Point& p : AllPoints(out.pins.at("Out")))
	{
		const Vector3 up = p.rotation * Vector3(0, 1, 0);
		CHECK(up.x == Approx(normal.x).margin(1e-4));
		CHECK(up.y == Approx(normal.y).margin(1e-4));
		CHECK(up.z == Approx(normal.z).margin(1e-4));
	}

	SECTION("without a terrain the points stay upright with a warning")
	{
		const NodeOutput none = ExecNode("TransformPoints", { { "orientation", String("alignToTerrain") } }, { { "In", { pts } } }, EvalContext());
		CHECK(none.warnings.size() == 1);
		CHECK((AllPoints(none.pins.at("Out"))[0].rotation * Vector3(0, 1, 0)).y == Approx(1.0f));
	}
}

TEST_CASE("Density Noise is world-anchored and supports multiply, add and set", "[graph_nodes_point]")
{
	const EvalContext ctx;
	const AttributeSet settings = { { "frequency", 0.1f }, { "octaves", static_cast<int64>(3) }, { "mode", String("set") } };
	DataItem a = MakeGrid(0, 0, 20, 20, 1.0f);
	DataItem b = MakeGrid(10, 10, 30, 30, 1.0f);
	for (Point& p : b.points.points)
	{
		p.seed ^= 0xabcdef;
	}
	const auto pa = AllPoints(ExecNode("DensityNoise", settings, { { "In", { a } } }, ctx).pins.at("Out"));
	const auto pb = AllPoints(ExecNode("DensityNoise", settings, { { "In", { b } } }, ctx).pins.at("Out"));
	size_t matched = 0;
	std::set<int> distinct;
	for (const Point& p : pa)
	{
		CHECK(p.density >= 0.0f);
		CHECK(p.density <= 1.0f);
		distinct.insert(static_cast<int>(p.density * 100.0f));
		for (const Point& q : pb)
		{
			if (p.position.x == q.position.x && p.position.z == q.position.z)
			{
				CHECK(p.density == q.density);
				++matched;
			}
		}
	}
	CHECK(matched == 11 * 11);
	CHECK(distinct.size() > 10);

	SECTION("multiply and add relative to the set values")
	{
		DataItem half = a;
		for (Point& p : half.points.points)
		{
			p.density = 0.5f;
		}
		const auto mul = AllPoints(ExecNode("DensityNoise", { { "frequency", 0.1f }, { "octaves", static_cast<int64>(3) }, { "mode", String("multiply") } }, { { "In", { half } } }, ctx).pins.at("Out"));
		const auto add = AllPoints(ExecNode("DensityNoise", { { "frequency", 0.1f }, { "octaves", static_cast<int64>(3) }, { "mode", String("add") }, { "amount", 0.25f } }, { { "In", { half } } }, ctx).pins.at("Out"));
		REQUIRE(mul.size() == pa.size());
		for (size_t i = 0; i < pa.size(); ++i)
		{
			const float u = pa[i].density;
			CHECK(mul[i].density == Approx(0.5f * u).margin(1e-5));
			CHECK(add[i].density == Approx(std::clamp(0.5f + 0.25f * (2.0f * u - 1.0f), 0.0f, 1.0f)).margin(1e-5));
		}
		const auto none = AllPoints(ExecNode("DensityNoise", { { "amount", 0.0f } }, { { "In", { half } } }, ctx).pins.at("Out"));
		CHECK(none[0].density == 0.5f);
	}
}

TEST_CASE("Attribute Math: operators, remap, attributes as operands, point properties", "[graph_nodes_point]")
{
	const EvalContext ctx;
	DataItem pts = MakePoints({ { 0, 0 }, { 1, 0 }, { 2, 0 } });
	pts.attributes["level"] = 4.0f;
	for (size_t i = 0; i < 3; ++i)
	{
		pts.points.SetAttribute(i, "h", static_cast<float>(i) * 5.0f);
		pts.points.points[i].position.y = 10.0f + static_cast<float>(i);
		pts.points.points[i].scale = Vector3(1, 2, 3);
		pts.points.points[i].density = 0.5f;
	}
	auto run = [&](const AttributeSet& settings)
	{
		const NodeOutput out = ExecNode("AttributeMath", settings, { { "In", { pts } } }, ctx);
		REQUIRE(out.pins.at("Out").size() == 1);
		return out;
	};

	SECTION("remap")
	{
		const NodeOutput out = run({ { "target", String("r") }, { "input", String("h") }, { "op", String("remap") }, { "inMin", 0.0f }, { "inMax", 10.0f }, { "outMin", 100.0f }, { "outMax", 200.0f } });
		const DataItem& item = out.pins.at("Out")[0];
		CHECK(FloatAttr(item, 0, "r") == Approx(100.0f));
		CHECK(FloatAttr(item, 1, "r") == Approx(150.0f));
		CHECK(FloatAttr(item, 2, "r") == Approx(200.0f));
		CHECK(FloatAttr(item, 1, "h") == 5.0f);
	}
	SECTION("a degenerate remap range yields outMin, never NaN")
	{
		const NodeOutput out = run({ { "target", String("r") }, { "input", String("h") }, { "op", String("remap") }, { "inMin", 3.0f }, { "inMax", 3.0f }, { "outMin", 7.0f } });
		CHECK(FloatAttr(out.pins.at("Out")[0], 2, "r") == 7.0f);
	}
	SECTION("drop = position.y - level (item attribute operand)")
	{
		const NodeOutput out = run({ { "target", String("drop") }, { "input", String("position.y") }, { "op", String("sub") }, { "operand", String("level") } });
		CHECK(FloatAttr(out.pins.at("Out")[0], 0, "drop") == Approx(6.0f));
		CHECK(FloatAttr(out.pins.at("Out")[0], 2, "drop") == Approx(8.0f));
	}
	SECTION("constants on the target itself: add, mul, min, max, clamp")
	{
		CHECK(FloatAttr(run({ { "target", String("h") }, { "op", String("add") }, { "value", 1.0f } }).pins.at("Out")[0], 2, "h") == 11.0f);
		CHECK(FloatAttr(run({ { "target", String("h") }, { "op", String("mul") }, { "value", 3.0f } }).pins.at("Out")[0], 1, "h") == 15.0f);
		CHECK(FloatAttr(run({ { "target", String("h") }, { "op", String("min") }, { "value", 3.0f } }).pins.at("Out")[0], 2, "h") == 3.0f);
		CHECK(FloatAttr(run({ { "target", String("h") }, { "op", String("max") }, { "value", 3.0f } }).pins.at("Out")[0], 0, "h") == 3.0f);
		const DataItem clamped = run({ { "target", String("h") }, { "op", String("clamp") }, { "min", 2.0f }, { "max", 6.0f } }).pins.at("Out")[0];
		CHECK(FloatAttr(clamped, 0, "h") == 2.0f);
		CHECK(FloatAttr(clamped, 1, "h") == 5.0f);
		CHECK(FloatAttr(clamped, 2, "h") == 6.0f);
	}
	SECTION("point properties: density, position.y and per-axis scale")
	{
		CHECK(AllPoints(run({ { "target", String("density") }, { "op", String("mul") }, { "value", 0.5f } }).pins.at("Out"))[0].density == 0.25f);
		CHECK(AllPoints(run({ { "target", String("position.y") }, { "op", String("add") }, { "value", 2.0f } }).pins.at("Out"))[1].position.y == 13.0f);
		const Point scaled = AllPoints(run({ { "target", String("scale") }, { "op", String("mul") }, { "value", 2.0f } }).pins.at("Out"))[0];
		CHECK(scaled.scale.x == 2.0f);
		CHECK(scaled.scale.y == 4.0f);
		CHECK(scaled.scale.z == 6.0f);
	}
	SECTION("division by zero gives 0 with one warning")
	{
		const NodeOutput out = run({ { "target", String("h") }, { "op", String("div") }, { "value", 0.0f } });
		CHECK(FloatAttr(out.pins.at("Out")[0], 2, "h") == 0.0f);
		CHECK(out.warnings.size() == 1);
	}
	SECTION("a missing or non-numeric input leaves points unchanged with one warning")
	{
		const NodeOutput missing = run({ { "target", String("density") }, { "input", String("nothing") }, { "op", String("add") }, { "value", 1.0f } });
		CHECK(AllPoints(missing.pins.at("Out"))[0].density == 0.5f);
		CHECK(missing.warnings.size() == 1);
		DataItem withString = pts;
		withString.attributes["label"] = String("x");
		const NodeOutput text = ExecNode("AttributeMath", { { "target", String("h") }, { "op", String("add") }, { "operand", String("label") } }, { { "In", { withString } } }, ctx);
		CHECK(FloatAttr(text.pins.at("Out")[0], 1, "h") == 5.0f);
		CHECK(text.warnings.size() == 1);
	}
}

TEST_CASE("Copy Attributes from Control: containing area first, else nearest spline within distance", "[graph_nodes_point]")
{
	const EvalContext ctx;
	DataItem dense = MakeSquare(0, 0, 10);
	dense.attributes["density"] = 0.6f;
	dense.attributes["biome"] = String("pine");
	DataItem sparse = MakeSquare(5, 0, 10);
	sparse.attributes["density"] = 0.3f;
	sparse.attributes["biome"] = String("birch");
	DataItem road = MakeSplineItem({ { 0, 30 }, { 20, 30 } }, false);
	road.attributes["width"] = 4.0f;
	DataItem path = MakeSplineItem({ { 0, 36 }, { 20, 36 } }, false);
	path.attributes["width"] = 1.0f;

	// inside dense only, overlap, inside sparse only, outside all, near the road, nearer the path, far from both
	const DataItem pts = MakePoints({ { 2, 5 }, { 7, 5 }, { 12, 5 }, { 30, 5 }, { 10, 31 }, { 10, 34 }, { 10, 50 } });

	SECTION("the containing area's density (first area wins in overlaps)")
	{
		const NodeOutput out = ExecNode("CopyAttributesFromControl", { { "attributes", String("density, biome") } }, { { "In", { pts } }, { "Controls", { dense, sparse } } }, ctx);
		const DataItem& item = out.pins.at("Out").at(0);
		CHECK(item.points.points[0].density == 0.6f);
		CHECK(item.points.points[1].density == 0.6f);
		CHECK(item.points.points[2].density == 0.3f);
		CHECK(item.points.points[3].density == 1.0f);
		CHECK(std::get<String>(*item.points.GetAttribute(0, "biome")) == "pine");
		CHECK(std::get<String>(*item.points.GetAttribute(2, "biome")) == "birch");
		CHECK(std::get<String>(*item.points.GetAttribute(3, "biome")).empty());
		CHECK(out.warnings.empty());
	}
	SECTION("the nearest spline within distance")
	{
		const NodeOutput out = ExecNode("CopyAttributesFromControl", { { "attributes", String("width") }, { "distance", 3.0f } }, { { "In", { pts } }, { "Controls", { road, path } } }, ctx);
		const DataItem& item = out.pins.at("Out").at(0);
		CHECK(FloatAttr(item, 4, "width") == 4.0f);
		CHECK(FloatAttr(item, 5, "width") == 1.0f);
		CHECK(FloatAttr(item, 6, "width") == 0.0f);
		CHECK(FloatAttr(item, 0, "width") == 0.0f);
	}
	SECTION("an empty list copies every attribute; distance 0 ignores splines")
	{
		const NodeOutput all = ExecNode("CopyAttributesFromControl", {}, { { "In", { pts } }, { "Controls", { dense, road } } }, ctx);
		const DataItem& item = all.pins.at("Out").at(0);
		CHECK(item.points.points[0].density == 0.6f);
		CHECK(std::get<String>(*item.points.GetAttribute(0, "biome")) == "pine");
		CHECK(FloatAttr(item, 4, "width") == 0.0f);
	}
	SECTION("a column of another type is left alone with a warning")
	{
		DataItem typed = pts;
		for (size_t i = 0; i < typed.points.points.size(); ++i)
		{
			typed.points.SetAttribute(i, "biome", static_cast<int64>(7));
		}
		const NodeOutput out = ExecNode("CopyAttributesFromControl", { { "attributes", String("biome") } }, { { "In", { typed } }, { "Controls", { dense } } }, ctx);
		CHECK(std::get<int64>(*out.pins.at("Out").at(0).points.GetAttribute(0, "biome")) == 7);
		CHECK(out.warnings.size() == 1);
	}
}

TEST_CASE("Copy Attributes from Control: area edges are distance sources", "[graph_nodes_point]")
{
	const EvalContext ctx;
	DataItem lake = MakeSquare(0, 0, 10);
	lake.attributes["level"] = 4.0f;
	DataItem river = MakeSplineItem({ { 13, 0 }, { 13, 10 } }, false);
	river.attributes["level"] = 9.0f;

	SECTION("a point 2 m outside a square area within distance 3 takes the area's attribute")
	{
		const NodeOutput out = ExecNode("CopyAttributesFromControl", { { "attributes", String("level") }, { "distance", 3.0f } }, { { "In", { MakePoints({ { 12, 5 } }) } }, { "Controls", { lake } } }, ctx);
		CHECK(FloatAttr(out.pins.at("Out").at(0), 0, "level") == 4.0f);
		const NodeOutput tooFar = ExecNode("CopyAttributesFromControl", { { "attributes", String("level") }, { "distance", 1.5f } }, { { "In", { MakePoints({ { 12, 5 } }) } }, { "Controls", { lake } } }, ctx);
		CHECK(FloatAttr(tooFar.pins.at("Out").at(0), 0, "level") == 0.0f);
	}
	SECTION("a nearer spline beats a farther area edge")
	{
		// (12.5, 5): lake edge 2.5 m away, river 0.5 m away.
		const NodeOutput out = ExecNode("CopyAttributesFromControl", { { "attributes", String("level") }, { "distance", 3.0f } }, { { "In", { MakePoints({ { 12.5f, 5 } }) } }, { "Controls", { lake, river } } }, ctx);
		CHECK(FloatAttr(out.pins.at("Out").at(0), 0, "level") == 9.0f);
	}
	SECTION("a point inside an area takes the containing area even when a spline is closer")
	{
		DataItem inner = MakeSplineItem({ { 9.5f, 0 }, { 9.5f, 10 } }, false);
		inner.attributes["level"] = 9.0f;
		const NodeOutput out = ExecNode("CopyAttributesFromControl", { { "attributes", String("level") }, { "distance", 3.0f } }, { { "In", { MakePoints({ { 9, 5 } }) } }, { "Controls", { inner, lake } } }, ctx);
		CHECK(FloatAttr(out.pins.at("Out").at(0), 0, "level") == 4.0f);
	}
	SECTION("a reserved name holding a String on the area warns and leaves the property unchanged")
	{
		DataItem labelled = lake;
		labelled.attributes["density"] = String("high");
		const NodeOutput out = ExecNode("CopyAttributesFromControl", { { "attributes", String("density, level") } }, { { "In", { MakePoints({ { 5, 5 } }) } }, { "Controls", { labelled } } }, ctx);
		const DataItem& item = out.pins.at("Out").at(0);
		CHECK(item.points.points[0].density == 1.0f);
		CHECK(FloatAttr(item, 0, "level") == 4.0f);
		REQUIRE(out.warnings.size() == 1);
		CHECK(out.warnings[0].find("density") != String::npos);
	}
}

TEST_CASE("Attribute Math reports a non-Float target column as such", "[graph_nodes_point]")
{
	DataItem pts = MakePoints({ { 0, 0 }, { 1, 0 } });
	for (size_t i = 0; i < 2; ++i)
	{
		pts.points.SetAttribute(i, "label", String("a"));
	}
	const NodeOutput out = ExecNode("AttributeMath", { { "target", String("label") }, { "op", String("add") }, { "value", 1.0f } }, { { "In", { pts } } }, EvalContext());
	CHECK(std::get<String>(*out.pins.at("Out").at(0).points.GetAttribute(1, "label")) == "a");
	REQUIRE(out.warnings.size() == 1);
	CHECK(out.warnings[0].find("target column 'label' is not Float") != String::npos);
}

TEST_CASE("Attribute Math creates its target column even when every point is skipped", "[graph_nodes_point]")
{
	const EvalContext ctx;
	const NodeOutput out = ExecNode("AttributeMath", { { "target", String("height") }, { "input", String("missing") }, { "op", String("add") } }, { { "In", { MakePoints({ { 0.0f, 0.0f }, { 1.0f, 0.0f } }) } } }, ctx);
	REQUIRE(out.error.empty());
	REQUIRE(out.pins.at("Out").size() == 1);
	const PointSet& set = out.pins.at("Out")[0].points;
	const auto column = set.columns.find("height");
	REQUIRE(column != set.columns.end());
	CHECK(column->second.first == AttributeType::Float);
	REQUIRE(column->second.second.size() == 2);
	CHECK(std::get<float>(column->second.second[0]) == 0.0f);
	CHECK(std::get<float>(column->second.second[1]) == 0.0f);
}
