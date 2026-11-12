// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "fake_terrain_sampler.h"
#include "graph_node_test_util.h"

#include "pcg/graph/evaluator.h"
#include "pcg/graph/node_registry.h"

#include <algorithm>
#include <cmath>

using namespace mmo;
using namespace mmo::pcg::graph;

namespace
{
	DataItem Tagged(const std::set<String>& tags, const uint64 sourceId)
	{
		DataItem item;
		item.kind = ItemKind::Params;
		item.tags = tags;
		item.sourceId = sourceId;
		return item;
	}

	std::vector<uint64> SourceIds(const Collection& c)
	{
		std::vector<uint64> ids;
		for (const DataItem& item : c)
		{
			ids.push_back(item.sourceId);
		}
		return ids;
	}

	/// Distance from a point to a segment on the x/z plane.
	float SegmentDistance(const float px, const float pz, const float ax, const float az, const float bx, const float bz)
	{
		const float dx = bx - ax;
		const float dz = bz - az;
		const float len2 = dx * dx + dz * dz;
		const float t = len2 > 0 ? std::clamp(((px - ax) * dx + (pz - az) * dz) / len2, 0.0f, 1.0f) : 0.0f;
		return std::hypot(ax + t * dx - px, az + t * dz - pz);
	}

	/// Pseudo-random points in [x0, x0 + size] x [z0, z0 + size].
	DataItem RandomPoints(const size_t count, const float x0, const float z0, const float size, const uint64 seed)
	{
		DataItem item;
		item.kind = ItemKind::Points;
		for (size_t i = 0; i < count; ++i)
		{
			Point p;
			p.position = Vector3(x0 + pcg::HashToUnit(pcg::Hash(seed, static_cast<int64>(i), 1)) * size, 0.0f, z0 + pcg::HashToUnit(pcg::Hash(seed, static_cast<int64>(i), 2)) * size);
			p.seed = pcg::Hash(seed, static_cast<int64>(i), 3);
			item.points.points.push_back(p);
		}
		return item;
	}

	bool SamePoint(const Point& a, const Point& b)
	{
		return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z && a.seed == b.seed;
	}
}

TEST_CASE("Filter and spatial node types are registered", "[graph_nodes_filter]")
{
	for (const char* type : { "FilterByTag", "FilterByAttribute", "FilterByTerrain", "DensityFilter", "SelfPrune", "Difference", "Intersection", "DistanceTo" })
	{
		INFO(type);
		const NodeTypeDef* def = NodeRegistry::Default().Find(type);
		REQUIRE(def != nullptr);
		REQUIRE_FALSE(def->outputs.empty());
		CHECK(def->outputs[0].name == "Out");
		REQUIRE_FALSE(def->inputs.empty());
		CHECK(def->inputs[0].name == "In");
		CHECK(def->inputs[0].required);
	}
	CHECK(NodeRegistry::Default().Find("FilterByTerrain")->contextDeps == context_dep::Terrain);
	for (const char* type : { "FilterByTag", "FilterByAttribute", "DensityFilter", "SelfPrune", "Difference", "Intersection", "DistanceTo" })
	{
		CHECK(NodeRegistry::Default().Find(type)->contextDeps == context_dep::None);
	}
	CHECK(NodeRegistry::Default().Find("FilterByTag")->outputs.size() == 2);
	CHECK(NodeRegistry::Default().Find("FilterByTag")->outputs[1].name == "Rejected");
}

TEST_CASE("Every filter node outputs empty collections for empty inputs", "[graph_nodes_filter]")
{
	FakeTerrainSampler terrain;
	EvalContext ctx;
	ctx.terrain = &terrain;
	for (const char* type : { "FilterByTag", "FilterByAttribute", "FilterByTerrain", "DensityFilter", "SelfPrune", "Difference", "Intersection", "DistanceTo" })
	{
		INFO(type);
		// No link at all, then a linked but empty pin.
		const NodeOutput none = ExecNode(type, {}, {}, ctx);
		CHECK(none.error.empty());
		const NodeOutput empty = ExecNode(type, {}, { { "In", {} }, { "Subtract", {} }, { "Areas", {} }, { "Target", {} } }, ctx);
		CHECK(empty.error.empty());
		for (const NodeOutput* out : { &none, &empty })
		{
			for (const auto& [pin, items] : out->pins)
			{
				CHECK(items.empty());
			}
		}
	}

	SECTION("an empty points item passes without NaN or crash")
	{
		DataItem emptyPoints;
		emptyPoints.kind = ItemKind::Points;
		const NodeOutput out = ExecNode("DistanceTo", {}, { { "In", { emptyPoints } }, { "Target", { MakeSquare(0, 0, 10) } } }, ctx);
		CHECK(CountPoints(out.pins.at("Out")) == 0);
	}
}

TEST_CASE("Filter by Tag routes items by all-of, any-of and exclude rules", "[graph_nodes_filter]")
{
	const Collection items = { Tagged({ "forest" }, 1), Tagged({ "forest", "dense" }, 2), Tagged({ "road" }, 3), Tagged({ "road", "forest", "old" }, 4) };
	const EvalContext ctx;

	SECTION("all of")
	{
		const NodeOutput out = ExecNode("FilterByTag", { { "tags", String("forest, old") } }, { { "In", items } }, ctx);
		CHECK(SourceIds(out.pins.at("Out")) == std::vector<uint64>{ 4 });
		CHECK(SourceIds(out.pins.at("Rejected")) == std::vector<uint64>{ 1, 2, 3 });
	}
	SECTION("any of")
	{
		const NodeOutput out = ExecNode("FilterByTag", { { "tags", String("dense,road") }, { "matchAny", true } }, { { "In", items } }, ctx);
		CHECK(SourceIds(out.pins.at("Out")) == std::vector<uint64>{ 2, 3, 4 });
		CHECK(SourceIds(out.pins.at("Rejected")) == std::vector<uint64>{ 1 });
	}
	SECTION("exclude wins")
	{
		const NodeOutput out = ExecNode("FilterByTag", { { "tags", String("forest") }, { "excludeTags", String("dense, old") } }, { { "In", items } }, ctx);
		CHECK(SourceIds(out.pins.at("Out")) == std::vector<uint64>{ 1 });
		CHECK(SourceIds(out.pins.at("Rejected")) == std::vector<uint64>{ 2, 3, 4 });
	}
	SECTION("no rules pass everything in order")
	{
		const NodeOutput out = ExecNode("FilterByTag", {}, { { "In", items } }, ctx);
		CHECK(SourceIds(out.pins.at("Out")) == std::vector<uint64>{ 1, 2, 3, 4 });
		CHECK(out.pins.at("Rejected").empty());
	}
}

TEST_CASE("Filter by Attribute compares point columns, item attributes and point properties", "[graph_nodes_filter]")
{
	const EvalContext ctx;
	DataItem pts = MakePoints({ { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 }, { 4, 0 } });
	pts.sourceId = 9;
	pts.tags = { "t" };
	for (size_t i = 0; i < 5; ++i)
	{
		pts.points.SetAttribute(i, "h", static_cast<float>(i + 1));
		pts.points.SetAttribute(i, "n", static_cast<int64>(i));
		pts.points.SetAttribute(i, "biome", String(i < 2 ? "desert" : "forest"));
		pts.points.points[i].density = 0.2f * static_cast<float>(i);
	}

	SECTION("each comparison operator on a Float column")
	{
		const std::vector<std::pair<String, size_t>> cases = { { "<", 2 }, { "<=", 3 }, { "==", 1 }, { "!=", 4 }, { ">=", 3 }, { ">", 2 } };
		for (const auto& [op, kept] : cases)
		{
			INFO(op);
			const NodeOutput out = ExecNode("FilterByAttribute", { { "attribute", String("h") }, { "op", op }, { "value", 3.0f } }, { { "In", { pts } } }, ctx);
			CHECK(out.warnings.empty());
			CHECK(CountPoints(out.pins.at("Out")) == kept);
			CHECK(CountPoints(out.pins.at("Rejected")) == 5 - kept);
		}
	}
	SECTION("in range is inclusive, keeps order, columns and item identity")
	{
		const NodeOutput out = ExecNode("FilterByAttribute", { { "attribute", String("h") }, { "op", String("inRange") }, { "value", 2.0f }, { "valueMax", 4.0f } }, { { "In", { pts } } }, ctx);
		const Collection& kept = out.pins.at("Out");
		REQUIRE(kept.size() == 1);
		CHECK(kept[0].sourceId == 9);
		CHECK(kept[0].tags == std::set<String>{ "t" });
		REQUIRE(kept[0].points.points.size() == 3);
		CHECK(FloatAttr(kept[0], 0, "h") == 2.0f);
		CHECK(FloatAttr(kept[0], 2, "h") == 4.0f);
		const Collection& rejected = out.pins.at("Rejected");
		REQUIRE(rejected.size() == 1);
		CHECK(rejected[0].points.points.size() == 2);
		CHECK(FloatAttr(rejected[0], 0, "h") == 1.0f);
		CHECK(FloatAttr(rejected[0], 1, "h") == 5.0f);
	}
	SECTION("Int columns compare numerically")
	{
		const NodeOutput out = ExecNode("FilterByAttribute", { { "attribute", String("n") }, { "op", String(">=") }, { "value", 3.0f } }, { { "In", { pts } } }, ctx);
		CHECK(CountPoints(out.pins.at("Out")) == 2);
	}
	SECTION("the reserved name density reads the point property")
	{
		const NodeOutput out = ExecNode("FilterByAttribute", { { "attribute", String("density") }, { "op", String(">") }, { "value", 0.5f } }, { { "In", { pts } } }, ctx);
		CHECK(CountPoints(out.pins.at("Out")) == 2);
	}
	SECTION("strings support == and !=")
	{
		const NodeOutput eq = ExecNode("FilterByAttribute", { { "attribute", String("biome") }, { "op", String("==") }, { "stringValue", String("desert") } }, { { "In", { pts } } }, ctx);
		CHECK(CountPoints(eq.pins.at("Out")) == 2);
		CHECK(eq.warnings.empty());
		const NodeOutput ne = ExecNode("FilterByAttribute", { { "attribute", String("biome") }, { "op", String("!=") }, { "stringValue", String("desert") } }, { { "In", { pts } } }, ctx);
		CHECK(CountPoints(ne.pins.at("Out")) == 3);
	}
	SECTION("ordering a string attribute rejects with one warning")
	{
		const NodeOutput out = ExecNode("FilterByAttribute", { { "attribute", String("biome") }, { "op", String("<") }, { "value", 1.0f } }, { { "In", { pts } } }, ctx);
		CHECK(CountPoints(out.pins.at("Out")) == 0);
		CHECK(CountPoints(out.pins.at("Rejected")) == 5);
		CHECK(out.warnings.size() == 1);
	}
	SECTION("a missing attribute rejects with exactly one warning")
	{
		DataItem other = pts;
		other.sourceId = 10;
		const NodeOutput out = ExecNode("FilterByAttribute", { { "attribute", String("missing") }, { "op", String("<") }, { "value", 1.0f } }, { { "In", { pts, other } } }, ctx);
		CHECK(out.pins.at("Out").empty());
		CHECK(SourceIds(out.pins.at("Rejected")) == std::vector<uint64>{ 9, 10 });
		CHECK(CountPoints(out.pins.at("Rejected")) == 10);
		CHECK(out.warnings.size() == 1);
	}
	SECTION("item attributes route whole items")
	{
		DataItem lake = MakeSquare(0, 0, 10);
		lake.attributes["level"] = 5.0f;
		lake.sourceId = 1;
		DataItem pond = MakeSquare(20, 0, 10);
		pond.attributes["level"] = static_cast<int64>(1);
		pond.sourceId = 2;
		const NodeOutput out = ExecNode("FilterByAttribute", { { "attribute", String("level") }, { "op", String(">") }, { "value", 2.0f } }, { { "In", { lake, pond } } }, ctx);
		CHECK(SourceIds(out.pins.at("Out")) == std::vector<uint64>{ 1 });
		CHECK(SourceIds(out.pins.at("Rejected")) == std::vector<uint64>{ 2 });
	}
	SECTION("points fall back to the item attribute; scope item routes the whole points item")
	{
		DataItem withLevel = pts;
		withLevel.attributes["level"] = 7.0f;
		const NodeOutput perPoint = ExecNode("FilterByAttribute", { { "attribute", String("level") }, { "op", String(">") }, { "value", 2.0f } }, { { "In", { withLevel } } }, ctx);
		CHECK(CountPoints(perPoint.pins.at("Out")) == 5);

		withLevel.attributes["h"] = 100.0f;
		const NodeOutput itemScope = ExecNode("FilterByAttribute", { { "attribute", String("h") }, { "op", String(">") }, { "value", 50.0f }, { "scope", String("item") } }, { { "In", { withLevel } } }, ctx);
		CHECK(CountPoints(itemScope.pins.at("Out")) == 5);
		const NodeOutput pointScope = ExecNode("FilterByAttribute", { { "attribute", String("h") }, { "op", String(">") }, { "value", 50.0f } }, { { "In", { withLevel } } }, ctx);
		CHECK(CountPoints(pointScope.pins.at("Out")) == 0);
	}
}

TEST_CASE("Filter by Terrain: slope, height, splat weight and water", "[graph_nodes_filter]")
{
	FakeTerrainSampler terrain;
	terrain.base = 0.0f;
	EvalContext ctx;
	ctx.terrain = &terrain;
	const DataItem line = MakePoints({ { 0, 0 }, { 10, 0 }, { 20, 0 }, { 30, 0 }, { 40, 0 }, { 50, 0 }, { 60, 0 } });

	SECTION("slope range in degrees")
	{
		terrain.slopeX = 1.0f; // 45 degrees
		const NodeOutput steep = ExecNode("FilterByTerrain", { { "maxSlope", 30.0f } }, { { "In", { line } } }, ctx);
		CHECK(CountPoints(steep.pins.at("Out")) == 0);
		CHECK(CountPoints(steep.pins.at("Rejected")) == 7);
		const NodeOutput ok = ExecNode("FilterByTerrain", { { "minSlope", 40.0f }, { "maxSlope", 50.0f } }, { { "In", { line } } }, ctx);
		CHECK(CountPoints(ok.pins.at("Out")) == 7);
		terrain.slopeX = 0.1f; // ~5.7 degrees
		const NodeOutput flat = ExecNode("FilterByTerrain", { { "maxSlope", 30.0f } }, { { "In", { line } } }, ctx);
		CHECK(CountPoints(flat.pins.at("Out")) == 7);
	}
	SECTION("height range uses the terrain height, not the point y")
	{
		terrain.slopeX = 0.1f; // heights 0, 1, ..., 6
		const NodeOutput out = ExecNode("FilterByTerrain", { { "minHeight", 2.0f }, { "maxHeight", 4.0f } }, { { "In", { line } } }, ctx);
		const auto kept = AllPoints(out.pins.at("Out"));
		REQUIRE(kept.size() == 3);
		CHECK(kept[0].position.x == 20.0f);
		CHECK(kept[2].position.x == 40.0f);
	}
	SECTION("splat weight")
	{
		terrain.splat["sand"] = 0.7f;
		terrain.splat["grass"] = 0.3f;
		CHECK(CountPoints(ExecNode("FilterByTerrain", { { "splatKey", String("sand") }, { "minSplatWeight", 0.5f } }, { { "In", { line } } }, ctx).pins.at("Out")) == 7);
		CHECK(CountPoints(ExecNode("FilterByTerrain", { { "splatKey", String("grass") }, { "minSplatWeight", 0.5f } }, { { "In", { line } } }, ctx).pins.at("Out")) == 0);
	}
	SECTION("under water excluded only when asked")
	{
		terrain.base = 10.0f;
		terrain.hasWater = true;
		terrain.waterLevel = 15.0f;
		CHECK(CountPoints(ExecNode("FilterByTerrain", { { "excludeUnderWater", true } }, { { "In", { line } } }, ctx).pins.at("Out")) == 0);
		CHECK(CountPoints(ExecNode("FilterByTerrain", {}, { { "In", { line } } }, ctx).pins.at("Out")) == 7);
		terrain.waterLevel = 5.0f;
		CHECK(CountPoints(ExecNode("FilterByTerrain", { { "excludeUnderWater", true } }, { { "In", { line } } }, ctx).pins.at("Out")) == 7);
	}
	SECTION("points without terrain are rejected with a warning")
	{
		terrain.maxX = 25.0f;
		const NodeOutput out = ExecNode("FilterByTerrain", {}, { { "In", { line } } }, ctx);
		CHECK(CountPoints(out.pins.at("Out")) == 3);
		CHECK(CountPoints(out.pins.at("Rejected")) == 4);
		CHECK(out.warnings.size() == 1);
	}
	SECTION("no terrain at all rejects everything with a warning")
	{
		const EvalContext noTerrain;
		const NodeOutput out = ExecNode("FilterByTerrain", {}, { { "In", { line } } }, noTerrain);
		CHECK(CountPoints(out.pins.at("Out")) == 0);
		CHECK(CountPoints(out.pins.at("Rejected")) == 7);
		CHECK(out.warnings.size() == 1);
	}
}

TEST_CASE("Density Filter keeps densities in [min, max]", "[graph_nodes_filter]")
{
	DataItem pts = MakePoints({ { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } });
	pts.points.points[0].density = 0.1f;
	pts.points.points[1].density = 0.3f;
	pts.points.points[2].density = 0.8f;
	pts.points.points[3].density = 0.9f;
	const NodeOutput out = ExecNode("DensityFilter", { { "minDensity", 0.3f }, { "maxDensity", 0.8f } }, { { "In", { pts } } }, EvalContext());
	const auto kept = AllPoints(out.pins.at("Out"));
	REQUIRE(kept.size() == 2);
	CHECK(kept[0].density == 0.3f);
	CHECK(kept[1].density == 0.8f);
	CHECK(CountPoints(out.pins.at("Rejected")) == 2);
}

TEST_CASE("Self Prune: spacing, priority, determinism, locality and cross-item duplicates", "[graph_nodes_filter]")
{
	const EvalContext ctx;
	const float spacing = 2.0f;

	SECTION("no two kept points closer than minSpacing; deterministic")
	{
		const DataItem pts = RandomPoints(3000, 0, 0, 60, 5);
		const NodeOutput a = ExecNode("SelfPrune", { { "minSpacing", spacing } }, { { "In", { pts } } }, ctx);
		const NodeOutput b = ExecNode("SelfPrune", { { "minSpacing", spacing } }, { { "In", { pts } } }, ctx);
		CHECK(ContentHash(a.pins.at("Out")) == ContentHash(b.pins.at("Out")));
		const auto kept = AllPoints(a.pins.at("Out"));
		CHECK(kept.size() > 300);
		CHECK(kept.size() + CountPoints(a.pins.at("Rejected")) == 3000);
		for (size_t i = 0; i < kept.size(); ++i)
		{
			for (size_t j = i + 1; j < kept.size(); ++j)
			{
				const float d = std::hypot(kept[i].position.x - kept[j].position.x, kept[i].position.z - kept[j].position.z);
				if (d < spacing * 0.999f)
				{
					FAIL("points " << i << " and " << j << " are " << d << " apart");
				}
			}
		}
	}
	SECTION("higher density wins, then the lower seed")
	{
		DataItem pair = MakePoints({ { 0, 0 }, { 1, 0 } });
		pair.points.points[0].seed = 5;
		pair.points.points[1].seed = 9;
		pair.points.points[0].density = 0.4f;
		pair.points.points[1].density = 0.6f;
		auto kept = AllPoints(ExecNode("SelfPrune", { { "minSpacing", spacing } }, { { "In", { pair } } }, ctx).pins.at("Out"));
		REQUIRE(kept.size() == 1);
		CHECK(kept[0].seed == 9);

		pair.points.points[1].density = 0.4f;
		kept = AllPoints(ExecNode("SelfPrune", { { "minSpacing", spacing } }, { { "In", { pair } } }, ctx).pins.at("Out"));
		REQUIRE(kept.size() == 1);
		CHECK(kept[0].seed == 5);

		// priority "seed" ignores density.
		pair.points.points[1].density = 0.9f;
		kept = AllPoints(ExecNode("SelfPrune", { { "minSpacing", spacing }, { "priority", String("seed") } }, { { "In", { pair } } }, ctx).pins.at("Out"));
		REQUIRE(kept.size() == 1);
		CHECK(kept[0].seed == 5);
	}
	SECTION("locality: changing points in one corner leaves far points unchanged")
	{
		const DataItem base = RandomPoints(3000, 0, 0, 60, 11);
		DataItem changed = base;
		for (Point& p : changed.points.points)
		{
			if (p.position.x < 10.0f && p.position.z < 10.0f)
			{
				p.position.x += 0.7f;
				p.seed ^= 0x1234;
			}
		}
		const auto a = AllPoints(ExecNode("SelfPrune", { { "minSpacing", spacing } }, { { "In", { base } } }, ctx).pins.at("Out"));
		const auto b = AllPoints(ExecNode("SelfPrune", { { "minSpacing", spacing } }, { { "In", { changed } } }, ctx).pins.at("Out"));
		// The fate of a point depends on points within 3 * minSpacing (one round); the change region grows by 0.7.
		const float margin = 10.7f + 3.0f * spacing + 0.1f;
		auto farAway = [&](const Point& p) { return p.position.x > margin || p.position.z > margin; };
		std::vector<Point> farA, farB;
		std::copy_if(a.begin(), a.end(), std::back_inserter(farA), farAway);
		std::copy_if(b.begin(), b.end(), std::back_inserter(farB), farAway);
		REQUIRE(farA.size() == farB.size());
		CHECK(farA.size() > 200);
		for (size_t i = 0; i < farA.size(); ++i)
		{
			CHECK(SamePoint(farA[i], farB[i]));
		}
	}
	SECTION("prunes across items: a duplicated item keeps only the first copy")
	{
		DataItem first = RandomPoints(200, 0, 0, 20, 3);
		first.sourceId = 1;
		DataItem second = first;
		second.sourceId = 2;
		const NodeOutput out = ExecNode("SelfPrune", { { "minSpacing", spacing } }, { { "In", { first, second } } }, ctx);
		const Collection& kept = out.pins.at("Out");
		REQUIRE(kept.size() == 1);
		CHECK(kept[0].sourceId == 1);
	}
	SECTION("minSpacing 0 keeps everything")
	{
		const DataItem pts = RandomPoints(100, 0, 0, 5, 2);
		CHECK(CountPoints(ExecNode("SelfPrune", { { "minSpacing", 0.0f } }, { { "In", { pts } } }, ctx).pins.at("Out")) == 100);
	}
}

TEST_CASE("Difference removes points inside areas and near splines with clearance", "[graph_nodes_filter]")
{
	const EvalContext ctx;
	DataItem grid = MakeGrid(0, 0, 40, 40, 1.0f);
	grid.sourceId = 3;
	const size_t total = grid.points.points.size();

	DataItem exclusion = MakeSquare(10.5f, 10.5f, 5.0f);
	DataItem road = MakeSplineItem({ { 0, 30 }, { 40, 30 } }, false);
	road.attributes["clearance"] = 6.0f;

	SECTION("areas remove points inside")
	{
		const NodeOutput out = ExecNode("Difference", {}, { { "In", { grid } }, { "Subtract", { exclusion } } }, ctx);
		const auto kept = AllPoints(out.pins.at("Out"));
		CHECK(kept.size() == total - 25);
		CHECK(CountPoints(out.pins.at("Rejected")) == 25);
		for (const Point& p : kept)
		{
			CHECK_FALSE((p.position.x > 10.5f && p.position.x < 15.5f && p.position.z > 10.5f && p.position.z < 15.5f));
		}
		REQUIRE(out.pins.at("Out").size() == 1);
		CHECK(out.pins.at("Out")[0].sourceId == 3);
	}
	SECTION("a road spline with clearance removes points within distance + clearance")
	{
		const NodeOutput out = ExecNode("Difference", {}, { { "In", { grid } }, { "Subtract", { exclusion, road } } }, ctx);
		const auto kept = AllPoints(out.pins.at("Out"));
		for (const Point& p : kept)
		{
			CHECK(SegmentDistance(p.position.x, p.position.z, 0, 30, 40, 30) > 6.0f);
		}
		// Rows z = 24 ... 36 (13 rows of 41) plus the exclusion's 25 points.
		CHECK(kept.size() == total - 13 * 41 - 25);

		const NodeOutput wider = ExecNode("Difference", { { "distance", 2.0f } }, { { "In", { grid } }, { "Subtract", { road } } }, ctx);
		for (const Point& p : AllPoints(wider.pins.at("Out")))
		{
			CHECK(SegmentDistance(p.position.x, p.position.z, 0, 30, 40, 30) > 8.0f);
		}
		CHECK(CountPoints(wider.pins.at("Out")) == total - 17 * 41);
	}
	SECTION("without clearance only distance counts; distance 0 removes nothing near the spline")
	{
		DataItem path = road;
		path.attributes.clear();
		CHECK(CountPoints(ExecNode("Difference", {}, { { "In", { grid } }, { "Subtract", { path } } }, ctx).pins.at("Out")) == total);
		CHECK(CountPoints(ExecNode("Difference", { { "distance", 1.5f } }, { { "In", { grid } }, { "Subtract", { path } } }, ctx).pins.at("Out")) == total - 3 * 41);
	}
	SECTION("a non-numeric clearance warns and counts as 0")
	{
		DataItem bad = road;
		bad.attributes["clearance"] = String("wide");
		const NodeOutput out = ExecNode("Difference", { { "distance", 1.5f } }, { { "In", { grid } }, { "Subtract", { bad } } }, ctx);
		CHECK(CountPoints(out.pins.at("Out")) == total - 3 * 41);
		CHECK(out.warnings.size() == 1);
	}
	SECTION("no subtract input passes everything")
	{
		CHECK(CountPoints(ExecNode("Difference", {}, { { "In", { grid } } }, ctx).pins.at("Out")) == total);
	}
}

TEST_CASE("Intersection keeps points inside areas", "[graph_nodes_filter]")
{
	const EvalContext ctx;
	const DataItem grid = MakeGrid(0, 0, 20, 20, 1.0f);
	const NodeOutput out = ExecNode("Intersection", {}, { { "In", { grid } }, { "Areas", { MakeSquare(2.5f, 2.5f, 3.0f), MakeSquare(10.5f, 10.5f, 2.0f) } } }, ctx);
	const auto kept = AllPoints(out.pins.at("Out"));
	CHECK(kept.size() == 9 + 4);
	CHECK(CountPoints(out.pins.at("Rejected")) == 21 * 21 - 13);

	const NodeOutput none = ExecNode("Intersection", {}, { { "In", { grid } }, { "Areas", {} } }, ctx);
	CHECK(CountPoints(none.pins.at("Out")) == 0);
	CHECK(CountPoints(none.pins.at("Rejected")) == 21 * 21);
}

TEST_CASE("Distance To writes the signed distance to splines and area edges", "[graph_nodes_filter]")
{
	const EvalContext ctx;
	DataItem pts = MakePoints({ { 5, 5 }, { 15, 5 }, { 5, 8 }, { 30, 3 }, { 500, 500 } });
	pts.sourceId = 4;
	const DataItem lake = MakeSquare(0, 0, 10);
	const DataItem river = MakeSplineItem({ { 30, 0 }, { 30, 50 } }, false);

	const NodeOutput out = ExecNode("DistanceTo", { { "attributeName", String("distanceToWater") }, { "maxDistance", 100.0f } }, { { "In", { pts } }, { "Target", { lake, river } } }, ctx);
	const Collection& c = out.pins.at("Out");
	REQUIRE(c.size() == 1);
	CHECK(c[0].sourceId == 4);
	REQUIRE(c[0].points.points.size() == 5);
	CHECK(FloatAttr(c[0], 0, "distanceToWater") == Approx(-5.0f).margin(1e-3));
	CHECK(FloatAttr(c[0], 1, "distanceToWater") == Approx(5.0f).margin(1e-3));
	CHECK(FloatAttr(c[0], 2, "distanceToWater") == Approx(-2.0f).margin(1e-3));
	CHECK(FloatAttr(c[0], 3, "distanceToWater") == Approx(0.0f).margin(1e-3));
	CHECK(FloatAttr(c[0], 4, "distanceToWater") == 100.0f);

	SECTION("default attribute name and no targets")
	{
		const NodeOutput none = ExecNode("DistanceTo", {}, { { "In", { pts } } }, ctx);
		const Collection& n = none.pins.at("Out");
		REQUIRE(n.size() == 1);
		CHECK(FloatAttr(n[0], 0, "distanceTo") == 1000.0f);
		CHECK(n[0].points.columns.count("distance") == 0);
	}
	SECTION("an existing column of another type is replaced with one warning")
	{
		DataItem typed = pts;
		for (size_t i = 0; i < typed.points.points.size(); ++i)
		{
			typed.points.SetAttribute(i, "distanceToWater", static_cast<int64>(3));
		}
		const NodeOutput replaced = ExecNode("DistanceTo", { { "attributeName", String("distanceToWater") }, { "maxDistance", 100.0f } }, { { "In", { typed } }, { "Target", { lake, river } } }, ctx);
		CHECK(replaced.warnings.size() == 1);
		const DataItem& item = replaced.pins.at("Out").at(0);
		CHECK(item.points.columns.at("distanceToWater").first == AttributeType::Float);
		CHECK(FloatAttr(item, 0, "distanceToWater") == Approx(-5.0f).margin(1e-3));
	}
	SECTION("overlapping areas: the deepest containing area wins")
	{
		const DataItem left = MakeSquare(0, 0, 10);
		const DataItem right = MakeSquare(5, 0, 10);
		const DataItem probes = MakePoints({ { 7, 5 }, { 12, 5 }, { 20, 5 } });
		const NodeOutput overlap = ExecNode("DistanceTo", {}, { { "In", { probes } }, { "Target", { left, right } } }, ctx);
		const DataItem& item = overlap.pins.at("Out").at(0);
		CHECK(FloatAttr(item, 0, "distanceTo") == Approx(-3.0f).margin(1e-3));
		CHECK(FloatAttr(item, 1, "distanceTo") == Approx(-3.0f).margin(1e-3));
		CHECK(FloatAttr(item, 2, "distanceTo") == Approx(5.0f).margin(1e-3));
	}
	SECTION("deterministic")
	{
		const NodeOutput again = ExecNode("DistanceTo", { { "attributeName", String("distanceToWater") }, { "maxDistance", 100.0f } }, { { "In", { pts } }, { "Target", { lake, river } } }, ctx);
		CHECK(ContentHash(again.pins.at("Out")) == ContentHash(c));
	}
}

TEST_CASE("Area inputs whose spline is not closed are skipped with a warning", "[graph_nodes_filter]")
{
	const EvalContext ctx;
	const DataItem grid = MakeGrid(0, 0, 20, 20, 1.0f);
	DataItem open = MakeSquare(2.5f, 2.5f, 5.0f);
	open.spline.closed = false;
	for (const char* type : { "Difference", "Intersection", "DistanceTo" })
	{
		INFO(type);
		const NodeOutput out = ExecNode(type, {}, { { "In", { grid } }, { "Subtract", { open } }, { "Areas", { open } }, { "Target", { open } } }, ctx);
		CHECK(out.error.empty());
		REQUIRE(out.warnings.size() == 1);
		CHECK(out.warnings[0].find("not closed") != String::npos);
	}
	CHECK(CountPoints(ExecNode("Difference", {}, { { "In", { grid } }, { "Subtract", { open } } }, ctx).pins.at("Out")) == 21 * 21);
}

TEST_CASE("Unknown enum setting values are node errors and validation issues", "[graph_nodes_filter]")
{
	const EvalContext ctx;
	const Collection points{ MakePoints({ { 0.0f, 0.0f }, { 1.0f, 0.0f } }) };
	CHECK_FALSE(ExecNode("FilterByAttribute", { { "attribute", String("density") }, { "scope", String("items") } }, { { "In", points } }, ctx).error.empty());
	CHECK_FALSE(ExecNode("SelfPrune", { { "priority", String("random") } }, { { "In", points } }, ctx).error.empty());
	CHECK(ExecNode("SelfPrune", { { "priority", String("seed") } }, { { "In", points } }, ctx).error.empty());

	// Region -> Sample Area -> Self Prune, so the only possible issues are the settings.
	Graph g;
	for (const auto& [id, type] : { std::pair<uint32, const char*>{ 1, "GetRegion" }, { 2, "SampleArea" }, { 3, "SelfPrune" } })
	{
		NodeInstance node;
		node.id = id;
		node.type = type;
		g.nodes.push_back(node);
	}
	g.links.push_back(Link{ 1, "Out", 2, "In" });
	g.links.push_back(Link{ 2, "Out", 3, "In" });
	g.nextNodeId = 4;
	REQUIRE(ValidateGraph(g, NodeRegistry::Default()).empty());

	g.nodes[2].settings["priority"] = String("random");
	std::vector<GraphIssue> issues = ValidateGraph(g, NodeRegistry::Default());
	REQUIRE(issues.size() == 1);
	CHECK(issues[0].node == 3);
	CHECK(issues[0].message.find("'random'") != String::npos);

	// Through a binding the parameter's default is checked, and an override outside the list fails the node.
	g.nodes[2].settings["priority"] = String("$prio");
	g.parameters.push_back(ParameterDef{ "prio", String("random"), "" });
	issues = ValidateGraph(g, NodeRegistry::Default());
	REQUIRE(issues.size() == 1);
	CHECK(issues[0].message.find("'prio'") != String::npos);

	g.parameters[0].defaultValue = String("seed");
	CHECK(ValidateGraph(g, NodeRegistry::Default()).empty());
	EvalContext overridden;
	overridden.parameters["prio"] = String("random");
	const EvalResult result = Evaluate(g, NodeRegistry::Default(), overridden, nullptr);
	REQUIRE(result.ok);
	CHECK(result.nodes.at(3).output.error.find("'random'") != String::npos);
}
