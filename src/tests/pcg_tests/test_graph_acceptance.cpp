// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Acceptance scenarios for the PCG graph core: whole graphs loaded from .hpcgg text through graph_io, evaluated
// against placed controls and a terrain, checked against the rules a level designer expects.

#include "catch.hpp"

#include "pcg/graph/controls.h"
#include "pcg/graph/evaluator.h"
#include "pcg/graph/graph_io.h"
#include "pcg/graph/node_registry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <vector>

using namespace mmo;
using namespace mmo::pcg::graph;

namespace
{
	/// Flat terrain (height 0) with a steep band x in [bandMinX, bandMaxX] rising at 45 degrees; beyond the
	/// band the terrain is flat again at the band's top height.
	class SteepBandTerrain final : public ITerrainSampler
	{
	public:
		bool Height(const float x, float, float& h) const override
		{
			h = std::clamp(x, bandMinX, bandMaxX) - bandMinX;
			return true;
		}

		Vector3 Normal(const float x, float) const override
		{
			if (InBand(x))
			{
				Vector3 n(-1.0f, 1.0f, 0.0f);
				n.Normalize();
				return n;
			}
			return Vector3(0.0f, 1.0f, 0.0f);
		}

		float SplatWeight(const String&, float, float) const override
		{
			return 0.0f;
		}

		bool WaterLevel(float, float, float&) const override
		{
			return false;
		}

		uint64 Revision() const override
		{
			return 1;
		}

		bool InBand(const float x) const
		{
			return x >= bandMinX && x <= bandMaxX;
		}

		float bandMinX = 260.0f;
		float bandMaxX = 290.0f;
	};

	/// Owns control types and instances so the resolved controls' pointers stay valid.
	struct ControlWorld
	{
		std::vector<std::unique_ptr<ControlType>> types;
		std::vector<std::unique_ptr<ControlInstance>> instances;
		std::vector<ResolvedControl> resolved;

		const ControlType* AddType(const String& name, const ControlShape shape, const std::set<String>& tags, const String& property = String(), const float propertyDefault = 0.0f)
		{
			auto t = std::make_unique<ControlType>();
			t->name = name;
			t->shape = shape;
			t->defaultTags = tags;
			if (!property.empty())
			{
				PropertyGroup group;
				group.name = "Main";
				PropertyDef def;
				def.name = property;
				def.type = PropertyType::Float;
				def.defaultValue = propertyDefault;
				group.properties.push_back(def);
				t->groups.push_back(group);
			}
			types.push_back(std::move(t));
			return types.back().get();
		}

		void Add(const ControlType* type, const uint64 id, const std::vector<SplinePoint>& points, const bool closed, const AttributeSet& overrides = {})
		{
			auto inst = std::make_unique<ControlInstance>();
			inst->id = id;
			inst->typePath = type->name;
			inst->spline.closed = closed;
			inst->spline.points = points;
			for (size_t i = 0; i < points.size(); ++i)
			{
				inst->pointIds.push_back(i + 1);
			}
			inst->nextPointId = points.size() + 1;
			inst->overrides = overrides;
			instances.push_back(std::move(inst));

			ResolvedControl rc;
			std::vector<String> warnings;
			REQUIRE(ResolveControl(*type, *instances.back(), rc, warnings));
			CHECK(warnings.empty());
			resolved.push_back(rc);
		}
	};

	std::vector<SplinePoint> Rect(const float x0, const float z0, const float x1, const float z1)
	{
		return { { x0, z0 }, { x1, z0 }, { x1, z1 }, { x0, z1 } };
	}

	SplinePoint At(const float x, const float y, const float z)
	{
		SplinePoint p;
		p.x = x;
		p.y = y;
		p.z = z;
		return p;
	}

	/// Parses graph text, checks that it writes back and re-parses to the same text, and returns the graph.
	Graph LoadGraph(const String& json)
	{
		Graph graph;
		String error;
		if (!ParseGraph(json, graph, error))
		{
			FAIL("graph does not parse: " << error);
		}
		const String written = WriteGraph(graph);
		Graph reparsed;
		REQUIRE(ParseGraph(written, reparsed, error));
		CHECK(WriteGraph(reparsed) == written);
		REQUIRE(ValidateGraph(reparsed, NodeRegistry::Default()).empty());
		return reparsed;
	}

	String FloatSetting(const String& name, const float value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.6g", static_cast<double>(value));
		return "\"" + name + "\": { \"type\": \"float\", \"value\": " + buffer + " }";
	}

	/// The forest graph: Get Controls(forest) -> Sample Area -> Difference(Get Controls(exclusion)) -> Self Prune(6 m)
	/// -> Filter by Terrain(slope <= 30) -> Transform Points. Node 6 is the output.
	String ForestGraphJson(const float cellSize)
	{
		return String(R"({
  "version": 1,
  "name": "Forest",
  "nodes": [
    { "id": 1, "type": "GetControls", "settings": { "tags": { "type": "string", "value": "forest" } } },
    { "id": 2, "type": "SampleArea", "settings": {
        )") + FloatSetting("cellSize", cellSize) + R"(,
        "densityAttribute": { "type": "string", "value": "density" } } },
    { "id": 3, "type": "GetControls", "settings": { "tags": { "type": "string", "value": "exclusion" } } },
    { "id": 4, "type": "Difference", "settings": { "distance": { "type": "float", "value": 0 } } },
    { "id": 5, "type": "SelfPrune", "settings": { "minSpacing": { "type": "float", "value": 6 } } },
    { "id": 7, "type": "FilterByTerrain", "settings": { "maxSlope": { "type": "float", "value": 30 } } },
    { "id": 6, "type": "TransformPoints", "settings": {
        "yawMin": { "type": "float", "value": 0 },
        "yawMax": { "type": "float", "value": 360 },
        "scaleMin": { "type": "vector3", "value": [0.8, 0.8, 0.8] },
        "scaleMax": { "type": "vector3", "value": [1.2, 1.2, 1.2] },
        "orientation": { "type": "string", "value": "upright" } } }
  ],
  "links": [
    { "fromNode": 1, "fromPin": "Out", "toNode": 2, "toPin": "In" },
    { "fromNode": 2, "fromPin": "Out", "toNode": 4, "toPin": "In" },
    { "fromNode": 3, "fromPin": "Out", "toNode": 4, "toPin": "Subtract" },
    { "fromNode": 4, "fromPin": "Out", "toNode": 5, "toPin": "In" },
    { "fromNode": 5, "fromPin": "Out", "toNode": 7, "toPin": "In" },
    { "fromNode": 7, "fromPin": "Out", "toNode": 6, "toPin": "In" }
  ]
})";
	}

	/// The waterfall rule: Get Controls(river) -> Sample Spline(endsOnly) -> Distance To(Get Controls(water)) ->
	/// Filter by Attribute(distanceToWater <= 15) -> Copy Attributes from Control(water, 15 m) ->
	/// Attribute Math(drop = position.y - level) -> Filter by Attribute(drop >= 3). Node 8 is the output.
	const char* const WaterfallGraphJson = R"({
  "version": 1,
  "name": "Waterfalls",
  "nodes": [
    { "id": 1, "type": "GetControls", "settings": { "tags": { "type": "string", "value": "river" } } },
    { "id": 2, "type": "SampleSpline", "settings": { "endsOnly": { "type": "bool", "value": true } } },
    { "id": 3, "type": "GetControls", "settings": { "tags": { "type": "string", "value": "water" } } },
    { "id": 4, "type": "DistanceTo", "settings": { "attributeName": { "type": "string", "value": "distanceToWater" } } },
    { "id": 5, "type": "FilterByAttribute", "settings": {
        "attribute": { "type": "string", "value": "distanceToWater" },
        "op": { "type": "string", "value": "<=" },
        "value": { "type": "float", "value": 15 } } },
    { "id": 6, "type": "CopyAttributesFromControl", "settings": {
        "attributes": { "type": "string", "value": "level" },
        "distance": { "type": "float", "value": 15 } } },
    { "id": 7, "type": "AttributeMath", "settings": {
        "target": { "type": "string", "value": "drop" },
        "input": { "type": "string", "value": "position.y" },
        "op": { "type": "string", "value": "sub" },
        "operand": { "type": "string", "value": "level" } } },
    { "id": 8, "type": "FilterByAttribute", "settings": {
        "attribute": { "type": "string", "value": "drop" },
        "op": { "type": "string", "value": ">=" },
        "value": { "type": "float", "value": 3 } } }
  ],
  "links": [
    { "fromNode": 1, "fromPin": "Out", "toNode": 2, "toPin": "In" },
    { "fromNode": 2, "fromPin": "Out", "toNode": 4, "toPin": "In" },
    { "fromNode": 3, "fromPin": "Out", "toNode": 4, "toPin": "Target" },
    { "fromNode": 4, "fromPin": "Out", "toNode": 5, "toPin": "In" },
    { "fromNode": 5, "fromPin": "Out", "toNode": 6, "toPin": "In" },
    { "fromNode": 3, "fromPin": "Out", "toNode": 6, "toPin": "Controls" },
    { "fromNode": 6, "fromPin": "Out", "toNode": 7, "toPin": "In" },
    { "fromNode": 7, "fromPin": "Out", "toNode": 8, "toPin": "In" }
  ]
})";

	constexpr float ForestSpacing = 6.0f;
	constexpr float RoadClearance = 6.0f;
	constexpr float RoadZ = 360.0f;

	/// Forest controls: dense forest (0.6) x[0,400], sparse forest (0.3) x[500,900], both z[0,400]; an exclusion
	/// rectangle; a road along z = RoadZ with clearance 6 that is tagged exclusion as well.
	struct ForestScene
	{
		ControlWorld world;
		float exclusion[4] = {};

		explicit ForestScene(const float exclusionX0 = 40.0f, const float scale = 1.0f)
		{
			exclusion[0] = exclusionX0 * scale;
			exclusion[1] = 40.0f * scale;
			exclusion[2] = (exclusionX0 + 80.0f) * scale;
			exclusion[3] = 120.0f * scale;

			const ControlType* forest = world.AddType("ForestArea", ControlShape::ClosedLoop, { "forest" }, "density", 0.5f);
			const ControlType* excl = world.AddType("Exclusion", ControlShape::ClosedLoop, { "exclusion" });
			const ControlType* road = world.AddType("Road", ControlShape::OpenSpline, { "road", "exclusion" }, "clearance", 0.0f);
			world.Add(forest, 1, Rect(0, 0, 400 * scale, 400 * scale), true, { { "density", 0.6f } });
			world.Add(forest, 2, Rect(500 * scale, 0, 900 * scale, 400 * scale), true, { { "density", 0.3f } });
			world.Add(excl, 3, Rect(exclusion[0], exclusion[1], exclusion[2], exclusion[3]), true);
			world.Add(road, 4, { { -50 * scale, RoadZ * scale }, { 950 * scale, RoadZ * scale } }, false, { { "clearance", RoadClearance } });
		}

		EvalContext Context(const ITerrainSampler& terrain) const
		{
			EvalContext ctx;
			ctx.instanceSeed = 42;
			ctx.controls = &world.resolved;
			ctx.controlsRevision = ControlsHash(world.resolved);
			ctx.terrain = &terrain;
			return ctx;
		}

		/// Planar distance from (x, z) to the exclusion rectangle (0 inside).
		float ExclusionDistance(const float x, const float z) const
		{
			const float dx = std::max({ exclusion[0] - x, 0.0f, x - exclusion[2] });
			const float dz = std::max({ exclusion[1] - z, 0.0f, z - exclusion[3] });
			return std::hypot(dx, dz);
		}
	};

	std::vector<Point> Evaluated(const Graph& graph, const EvalContext& ctx, const uint32 outputNode, GraphCache* cache = nullptr, uint64* hash = nullptr)
	{
		const EvalResult result = Evaluate(graph, NodeRegistry::Default(), ctx, cache);
		REQUIRE(result.ok);
		std::vector<Point> points;
		for (const auto& [id, node] : result.nodes)
		{
			INFO("node " << id << ": " << node.output.error);
			REQUIRE(node.output.error.empty());
		}
		const NodeResult& out = result.nodes.at(outputNode);
		if (hash != nullptr)
		{
			*hash = out.hash;
		}
		for (const DataItem& item : out.output.pins.at("Out"))
		{
			points.insert(points.end(), item.points.points.begin(), item.points.points.end());
		}
		return points;
	}

	size_t CountIn(const std::vector<Point>& points, const float x0, const float z0, const float x1, const float z1)
	{
		return static_cast<size_t>(std::count_if(points.begin(), points.end(), [&](const Point& p)
		{
			return p.position.x >= x0 && p.position.x <= x1 && p.position.z >= z0 && p.position.z <= z1;
		}));
	}

	bool SameResult(const Point& a, const Point& b)
	{
		return a.seed == b.seed && a.position == b.position && a.scale == b.scale &&
			a.rotation.w == b.rotation.w && a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y && a.rotation.z == b.rotation.z;
	}
}

TEST_CASE("Forest graph: exclusions, road clearance, slope, spacing, density, determinism and locality", "[graph_acceptance]")
{
	// 10 m cells keep the candidates sparse enough that the 6 m prune does not saturate the dense forest (with
	// 8 m cells it removes 20% of the dense forest's points but only 5% of the sparse one's, ratio 1.7).
	const Graph graph = LoadGraph(ForestGraphJson(10.0f));
	const SteepBandTerrain terrain;
	const ForestScene scene;
	const EvalContext ctx = scene.Context(terrain);

	uint64 hashA = 0;
	const std::vector<Point> points = Evaluated(graph, ctx, 6, nullptr, &hashA);
	REQUIRE(points.size() > 500);

	SECTION("no point inside the exclusion, near the road or on the steep band")
	{
		for (const Point& p : points)
		{
			INFO("point at " << p.position.x << ", " << p.position.z);
			CHECK(scene.ExclusionDistance(p.position.x, p.position.z) > 0.0f);
			CHECK(std::fabs(p.position.z - RoadZ) > RoadClearance);
			CHECK_FALSE(terrain.InBand(p.position.x));
			CHECK(p.position.y == Approx(std::clamp(p.position.x, terrain.bandMinX, terrain.bandMaxX) - terrain.bandMinX));
		}
		// The rules are not vacuous: there are points on both sides of the road and of the band.
		CHECK(CountIn(points, 0, RoadZ, 400, 400) > 0);
		CHECK(CountIn(points, 290, 0, 400, 400) > 0);
	}

	SECTION("minimum spacing holds")
	{
		for (size_t i = 0; i < points.size(); ++i)
		{
			for (size_t j = i + 1; j < points.size(); ++j)
			{
				const float d = std::hypot(points[i].position.x - points[j].position.x, points[i].position.z - points[j].position.z);
				if (d < ForestSpacing)
				{
					FAIL("points " << i << " and " << j << " are " << d << " m apart");
				}
			}
		}
	}

	SECTION("the denser forest has about twice the points per area")
	{
		// Equal windows clear of the exclusion, the road and the band. The accepted band is empirical: it depends
		// on the samplers' seed hashing, so re-check it whenever that hash (or the prune ranking) changes.
		const size_t dense = CountIn(points, 0, 140, 240, 340);
		const size_t sparse = CountIn(points, 500, 140, 740, 340);
		INFO("dense " << dense << ", sparse " << sparse);
		REQUIRE(sparse > 50);
		const double ratio = static_cast<double>(dense) / static_cast<double>(sparse);
		CHECK(ratio > 1.7);
		CHECK(ratio < 2.3);
	}

	SECTION("two runs are identical")
	{
		uint64 hashB = 0;
		const std::vector<Point> again = Evaluated(graph, ctx, 6, nullptr, &hashB);
		CHECK(hashA == hashB);
		REQUIRE(again.size() == points.size());
		for (size_t i = 0; i < points.size(); ++i)
		{
			CHECK(SameResult(points[i], again[i]));
		}
	}

	SECTION("moving the exclusion changes only points near it")
	{
		constexpr float moveDistance = 20.0f;
		const ForestScene moved(40.0f + moveDistance);
		const EvalContext movedCtx = moved.Context(terrain);
		REQUIRE(movedCtx.controlsRevision != ctx.controlsRevision);
		const std::vector<Point> after = Evaluated(graph, movedCtx, 6);

		// Self Prune's locality radius (3 * minSpacing, point_prune.h) plus the move distance.
		const float radius = 3.0f * ForestSpacing + moveDistance;
		const auto farFromBoth = [&](const Point& p)
		{
			return scene.ExclusionDistance(p.position.x, p.position.z) > radius && moved.ExclusionDistance(p.position.x, p.position.z) > radius;
		};

		std::map<uint64, const Point*> beforeBySeed;
		std::map<uint64, const Point*> afterBySeed;
		for (const Point& p : points)
		{
			beforeBySeed[p.seed] = &p;
		}
		for (const Point& p : after)
		{
			afterBySeed[p.seed] = &p;
		}

		size_t compared = 0;
		for (const auto& [from, to] : { std::make_pair(&beforeBySeed, &afterBySeed), std::make_pair(&afterBySeed, &beforeBySeed) })
		{
			for (const auto& [seed, p] : *from)
			{
				if (!farFromBoth(*p))
				{
					continue;
				}
				++compared;
				INFO("point at " << p->position.x << ", " << p->position.z);
				const auto it = to->find(seed);
				REQUIRE(it != to->end());
				CHECK(SameResult(*p, *it->second));
			}
		}
		CHECK(compared > 500);

		// The move did change something: nothing inside the new rectangle, and the freed strip got points.
		for (const Point& p : after)
		{
			CHECK(moved.ExclusionDistance(p.position.x, p.position.z) > 0.0f);
		}
		CHECK(CountIn(after, 40, 40, 40 + moveDistance - 0.01f, 120) > 0);
	}
}

TEST_CASE("Waterfall graph: only a river end near and well above the lake yields a waterfall point", "[graph_acceptance]")
{
	const Graph graph = LoadGraph(WaterfallGraphJson);

	ControlWorld world;
	const ControlType* river = world.AddType("River", ControlShape::OpenSpline, { "river" });
	const ControlType* lake = world.AddType("Lake", ControlShape::ClosedLoop, { "water" }, "level", 0.0f);

	// Lake x/z [0,100] at level 5.
	world.Add(lake, 1, Rect(0, 0, 100, 100), true, { { "level", 5.0f } });
	// River 1 ends 10 m east of the lake, 8 m above its level; its other end is 200 m away.
	world.Add(river, 2, { At(110, 13, 50), At(300, 20, 50) }, false);
	// River 2 ends 12 m north of the lake but only 1 m above its level; its other end is far away.
	world.Add(river, 3, { At(50, 6, 112), At(50, 10, 300) }, false);

	EvalContext ctx;
	ctx.controls = &world.resolved;
	ctx.controlsRevision = ControlsHash(world.resolved);

	const EvalResult result = Evaluate(graph, NodeRegistry::Default(), ctx, nullptr);
	REQUIRE(result.ok);

	// Four river ends, two within 15 m of the lake, one with enough drop.
	const auto countAt = [&](const uint32 node)
	{
		size_t n = 0;
		for (const DataItem& item : result.nodes.at(node).output.pins.at("Out"))
		{
			n += item.points.points.size();
		}
		return n;
	};
	CHECK(countAt(2) == 4);
	CHECK(countAt(5) == 2);

	const Collection& out = result.nodes.at(8).output.pins.at("Out");
	REQUIRE(out.size() == 1);
	CHECK(out[0].sourceId == 2);
	REQUIRE(out[0].points.points.size() == 1);
	const Point& waterfall = out[0].points.points[0];
	CHECK(waterfall.position.x == Approx(110.0f));
	CHECK(waterfall.position.y == Approx(13.0f));
	CHECK(waterfall.position.z == Approx(50.0f));
	CHECK(std::get<float>(*out[0].points.GetAttribute(0, "distanceToWater")) == Approx(10.0f));
	CHECK(std::get<float>(*out[0].points.GetAttribute(0, "level")) == Approx(5.0f));
	CHECK(std::get<float>(*out[0].points.GetAttribute(0, "drop")) == Approx(8.0f));
	for (const auto& [id, node] : result.nodes)
	{
		INFO("node " << id);
		CHECK(node.output.error.empty());
		CHECK(node.output.warnings.empty());
	}
}

TEST_CASE("Forest graph performance over 1.6 square kilometres with 4 m cells", "[graph_acceptance][.perf]")
{
	// The forest scene scaled so that the two forests cover 1.6 km^2 together (two 894 m squares); the exclusion
	// and the road scale along, the steep band stays.
	const float scale = std::sqrt(1.6e6f / (2.0f * 400.0f * 400.0f));
	const Graph graph = LoadGraph(ForestGraphJson(4.0f));
	const SteepBandTerrain terrain;
	const ForestScene scene(40.0f, scale);
	const EvalContext ctx = scene.Context(terrain);

	GraphCache cache;
	const auto t0 = std::chrono::steady_clock::now();
	const std::vector<Point> points = Evaluated(graph, ctx, 6, &cache);
	const auto t1 = std::chrono::steady_clock::now();
	const std::vector<Point> cached = Evaluated(graph, ctx, 6, &cache);
	const auto t2 = std::chrono::steady_clock::now();

	const double first = std::chrono::duration<double, std::milli>(t1 - t0).count();
	const double again = std::chrono::duration<double, std::milli>(t2 - t1).count();
	std::printf("[pcg perf] forest 1.6 km^2, 4 m cells: %zu points, first evaluation %.1f ms, cached re-evaluation %.3f ms\n", points.size(), first, again);
	CHECK(cached.size() == points.size());
	CHECK(points.size() > 10000);
}
