// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "graph_node_test_util.h"

#include "pcg/graph/controls.h"
#include "pcg/graph/evaluator.h"
#include "pcg/graph/node_registry.h"
#include "pcg/graph/terrain_compose.h"
#include "pcg/graph/terrain_op.h"

#include <algorithm>
#include <cmath>

using namespace mmo;
using namespace mmo::pcg;
using namespace mmo::pcg::graph;

namespace
{
	NodeOutput Run(const String& type, const AttributeSet& settings, const DataItem& item, const uint64 seed = 1234)
	{
		std::map<String, Collection> inputs;
		inputs["In"].push_back(item);
		return ExecNode(type, settings, inputs, EvalContext(), seed);
	}

	DataItem Line(const float x0, const float z0, const float x1, const float z1)
	{
		DataItem item = MakeSplineItem({ { x0, z0 }, { x1, z1 } }, false);
		item.sourceId = 42;
		item.pointIds = { 11, 12 };
		item.tags = { "river" };
		return item;
	}

	float Number(const DataItem& item, const char* name)
	{
		const auto it = item.op.params.find(name);
		REQUIRE(it != item.op.params.end());
		REQUIRE(std::holds_alternative<float>(it->second));
		return std::get<float>(it->second);
	}

	PreparedTerrain Compose(const NodeOutput& out, const uint32 order)
	{
		std::vector<TerrainOpInput> ops;
		for (const DataItem& item : out.pins.at("Terrain"))
		{
			TerrainOpInput in;
			in.op = item.op;
			in.op.order = order;
			in.spline = item.spline;
			in.pointIds = item.pointIds;
			in.sourceId = item.sourceId;
			ops.push_back(in);
		}
		PathSpline region;
		region.closed = true;
		for (const auto& p : { std::make_pair(-500.0f, -500.0f), std::make_pair(500.0f, -500.0f), std::make_pair(500.0f, 500.0f), std::make_pair(-500.0f, 500.0f) })
		{
			SplinePoint point;
			point.x = p.first;
			point.z = p.second;
			region.points.push_back(point);
		}
		PreparedTerrain terrain;
		String error;
		REQUIRE(terrain.Prepare(ops, [](float, float) { return 0.0f; }, {}, region, 10.0f, error));
		CHECK(error.empty());
		return terrain;
	}

	float Written(const TerrainTarget& t, const float base)
	{
		return t.heightWeight > 0.0f ? base + (t.height - base) * std::min(1.0f, t.heightWeight) : base;
	}
}

TEST_CASE("Terrain nodes are registered in category Terrain with a Terrain output", "[graph_nodes_terrain]")
{
	for (const char* type : { "TerrainNoise", "RaiseAlongSpline", "CarveAlongSpline", "PaintSplat", "WaterArea" })
	{
		const NodeTypeDef* def = NodeRegistry::Default().Find(type);
		REQUIRE(def != nullptr);
		CHECK(def->category == "Terrain");
		REQUIRE(def->outputs.size() == 1);
		CHECK(def->outputs[0].name == "Terrain");
		CHECK(def->contextDeps == context_dep::None);
	}
}

TEST_CASE("TerrainNoise emits a Noise op with the defaults and the node seed", "[graph_nodes_terrain]")
{
	DataItem area = MakeSquare(0, 0, 50);
	area.sourceId = 7;
	area.pointIds = { 1, 2, 3, 4 };
	area.tags = { "oasis" };
	const NodeOutput out = Run("TerrainNoise", {}, area, 99);
	REQUIRE(out.error.empty());
	const Collection& terrain = out.pins.at("Terrain");
	REQUIRE(terrain.size() == 1);

	const DataItem& item = terrain[0];
	CHECK(item.kind == ItemKind::Terrain);
	CHECK(item.op.type == TerrainOpType::Noise);
	CHECK(item.sourceId == 7);
	CHECK(item.pointIds == std::vector<uint64>({ 1, 2, 3, 4 }));
	CHECK(item.tags.count("oasis") == 1);
	CHECK(item.spline.closed);
	CHECK(item.spline.points.size() == 4);
	CHECK(Number(item, "amplitude") == 1.5f);
	CHECK(Number(item, "wavelength") == 24.0f);
	CHECK(Number(item, "octaves") == 3.0f);
	CHECK(Number(item, "falloff") == 8.0f);
	CHECK(std::get<int64>(item.op.params.at("seed")) == 99);

	// A different node seed gives a different noise seed.
	CHECK(std::get<int64>(Run("TerrainNoise", {}, area, 100).pins.at("Terrain")[0].op.params.at("seed")) == 100);
}

TEST_CASE("Item attributes override the terrain node settings", "[graph_nodes_terrain]")
{
	DataItem area = MakeSquare(0, 0, 50);
	area.attributes["amplitude"] = 3.0f;
	area.attributes["octaves"] = int64(5);
	area.attributes["seed"] = int64(555);
	const NodeOutput noise = Run("TerrainNoise", { { "amplitude", 2.0f }, { "wavelength", 10.0f } }, area);
	const DataItem& n = noise.pins.at("Terrain")[0];
	CHECK(Number(n, "amplitude") == 3.0f);
	CHECK(Number(n, "wavelength") == 10.0f);
	CHECK(Number(n, "octaves") == 5.0f);
	CHECK(std::get<int64>(n.op.params.at("seed")) == 555);

	DataItem road = Line(0, 0, 40, 0);
	road.attributes["depth"] = 0.5f;
	const NodeOutput carve = Run("CarveAlongSpline", { { "width", 1.0f } }, road);
	const DataItem& c = carve.pins.at("Terrain")[0];
	CHECK(c.op.type == TerrainOpType::Carve);
	CHECK(Number(c, "width") == 1.0f);
	CHECK(Number(c, "depth") == 0.5f);
	CHECK(Number(c, "bank") == 3.0f);

	DataItem river = Line(0, 0, 40, 0);
	river.attributes["width"] = int64(20);
	const NodeOutput raise = Run("RaiseAlongSpline", {}, river);
	const DataItem& r = raise.pins.at("Terrain")[0];
	CHECK(r.op.type == TerrainOpType::Raise);
	CHECK(Number(r, "width") == 20.0f);
	CHECK(Number(r, "falloffPower") == 2.0f);

	DataItem lake = MakeSquare(0, 0, 50);
	lake.attributes["level"] = 4.5f;
	lake.attributes["material"] = String("Water/Lake.hmat");
	const NodeOutput water = Run("WaterArea", { { "level", 1.0f }, { "material", String("Water/Default.hmat") } }, lake);
	const DataItem& w = water.pins.at("Terrain")[0];
	CHECK(w.op.type == TerrainOpType::WaterArea);
	CHECK(Number(w, "level") == 4.5f);
	CHECK(Number(w, "levelOffset") == 0.0f);
	CHECK(Number(w, "margin") == 4.0f);
	CHECK(w.op.asset == "Water/Lake.hmat");
	CHECK(w.op.params.count("material") == 0);
}

TEST_CASE("Carve defaults to a 2 m half width", "[graph_nodes_terrain]")
{
	const DataItem c = Run("CarveAlongSpline", {}, Line(0, 0, 40, 0)).pins.at("Terrain")[0];
	CHECK(Number(c, "width") == 2.0f);
	CHECK(Number(c, "depth") == 1.5f);
}

TEST_CASE("PaintSplat takes areas and splines, width only for open splines", "[graph_nodes_terrain]")
{
	const AttributeSet settings{ { "texture", String("Terrain/Sand.htex") } };

	const DataItem open = Run("PaintSplat", settings, Line(0, 0, 40, 0)).pins.at("Terrain")[0];
	CHECK(open.op.type == TerrainOpType::Paint);
	CHECK(std::get<String>(open.op.params.at("texture")) == "Terrain/Sand.htex");
	CHECK(Number(open, "weight") == 1.0f);
	CHECK(Number(open, "falloff") == 2.0f);
	CHECK(Number(open, "width") == 4.0f);

	const DataItem area = Run("PaintSplat", settings, MakeSquare(0, 0, 30)).pins.at("Terrain")[0];
	CHECK(area.op.params.count("width") == 0);
	CHECK(Number(area, "weight") == 1.0f);

	DataItem overridden = Line(0, 0, 40, 0);
	overridden.attributes["texture"] = String("Terrain/Mud.htex");
	CHECK(std::get<String>(Run("PaintSplat", settings, overridden).pins.at("Terrain")[0].op.params.at("texture")) == "Terrain/Mud.htex");
}

TEST_CASE("An Int seed attribute is taken as is, even beyond the float range check", "[graph_nodes_terrain]")
{
	DataItem area = MakeSquare(0, 0, 50);
	area.attributes["seed"] = int64(9'200'000'000'000'000'000);
	NodeOutput out = Run("TerrainNoise", {}, area);
	CHECK(out.warnings.empty());
	CHECK(std::get<int64>(out.pins.at("Terrain")[0].op.params.at("seed")) == int64(9'200'000'000'000'000'000));

	area.attributes["seed"] = int64(-9'200'000'000'000'000'000);
	out = Run("TerrainNoise", {}, area);
	CHECK(std::get<int64>(out.pins.at("Terrain")[0].op.params.at("seed")) == int64(-9'200'000'000'000'000'000));

	// A Float is rounded; one out of the int64 range is ignored with a warning (the node seed is used).
	area.attributes["seed"] = 41.6f;
	CHECK(std::get<int64>(Run("TerrainNoise", {}, area).pins.at("Terrain")[0].op.params.at("seed")) == 42);
	area.attributes["seed"] = 1.0e19f;
	out = Run("TerrainNoise", {}, area, 77);
	CHECK(out.warnings.size() == 1);
	CHECK(std::get<int64>(out.pins.at("Terrain")[0].op.params.at("seed")) == 77);
}

TEST_CASE("PaintSplat without a texture warns once", "[graph_nodes_terrain]")
{
	std::map<String, Collection> inputs;
	inputs["In"].push_back(Line(0, 0, 40, 0));
	inputs["In"].push_back(MakeSquare(0, 0, 30));
	const NodeOutput out = ExecNode("PaintSplat", {}, inputs, EvalContext(), 1);
	CHECK(out.error.empty());
	CHECK(out.pins.at("Terrain").size() == 2);
	REQUIRE(out.warnings.size() == 1);
	CHECK(out.warnings[0].find("texture") != String::npos);

	CHECK(Run("PaintSplat", { { "texture", String("Terrain/Sand.htex") } }, Line(0, 0, 40, 0)).warnings.empty());
}

TEST_CASE("A wrong item kind is skipped with a warning", "[graph_nodes_terrain]")
{
	const NodeOutput noise = Run("TerrainNoise", {}, Line(0, 0, 10, 0));
	CHECK(noise.error.empty());
	CHECK(noise.pins.at("Terrain").empty());
	CHECK(noise.warnings.size() == 1);

	const NodeOutput raise = Run("RaiseAlongSpline", {}, MakeSquare(0, 0, 10));
	CHECK(raise.pins.at("Terrain").empty());
	CHECK(raise.warnings.size() == 1);

	DataItem points = MakePoints({ { 1, 1 } });
	CHECK(Run("PaintSplat", { { "texture", String("a.htex") } }, points).pins.at("Terrain").empty());
}

TEST_CASE("A link of the wrong kind into a terrain node is a validation issue", "[graph_nodes_terrain]")
{
	Graph g;
	NodeInstance params;
	params.id = 1;
	params.type = "GraphParameters";
	NodeInstance noise;
	noise.id = 2;
	noise.type = "TerrainNoise";
	g.nodes = { params, noise };
	g.links.push_back(Link{ 1, "Out", 2, "In" });
	g.nextNodeId = 3;

	const std::vector<GraphIssue> issues = ValidateGraph(g, NodeRegistry::Default());
	REQUIRE_FALSE(issues.empty());
	CHECK(issues[0].message.find("does not accept") != String::npos);
}

TEST_CASE("Terrain nodes give empty output for empty input and are deterministic", "[graph_nodes_terrain]")
{
	for (const char* type : { "TerrainNoise", "RaiseAlongSpline", "CarveAlongSpline", "PaintSplat", "WaterArea" })
	{
		CHECK(ExecNode(type, AttributeSet(), {}, EvalContext()).pins.at("Terrain").empty());

		std::map<String, Collection> none;
		none["In"];
		CHECK(ExecNode(type, AttributeSet(), none, EvalContext()).pins.at("Terrain").empty());
	}

	const DataItem area = MakeSquare(0, 0, 50);
	CHECK(ContentHash(Run("TerrainNoise", {}, area).pins.at("Terrain")) == ContentHash(Run("TerrainNoise", {}, area).pins.at("Terrain")));
}

TEST_CASE("Output keeps the input order", "[graph_nodes_terrain]")
{
	std::map<String, Collection> inputs;
	for (uint64 i = 1; i <= 3; ++i)
	{
		DataItem line = Line(0, static_cast<float>(i) * 10.0f, 40, static_cast<float>(i) * 10.0f);
		line.sourceId = i;
		inputs["In"].push_back(line);
	}
	const NodeOutput out = ExecNode("RaiseAlongSpline", {}, inputs, EvalContext());
	REQUIRE(out.pins.at("Terrain").size() == 3);
	for (size_t i = 0; i < 3; ++i)
	{
		CHECK(out.pins.at("Terrain")[i].sourceId == i + 1);
	}
}

TEST_CASE("The evaluator stamps the node id on terrain node output", "[graph_nodes_terrain]")
{
	ControlType type;
	type.name = "Lake";
	type.shape = ControlShape::ClosedLoop;

	ControlInstance inst;
	inst.id = 5;
	inst.typePath = "Controls/Lake.hctl";
	inst.spline.closed = true;
	inst.spline.points = { { 0, 0 }, { 20, 0 }, { 20, 20 }, { 0, 20 } };
	inst.pointIds = { 1, 2, 3, 4 };
	inst.nextPointId = 5;

	ResolvedControl rc;
	std::vector<String> warnings;
	REQUIRE(ResolveControl(type, inst, rc, warnings));
	const std::vector<ResolvedControl> resolved{ rc };

	Graph g;
	NodeInstance controls;
	controls.id = 1;
	controls.type = "GetControls";
	g.nodes.push_back(controls);
	NodeInstance noise;
	noise.id = 9;
	noise.type = "TerrainNoise";
	g.nodes.push_back(noise);
	g.links.push_back(Link{ 1, "Out", 9, "In" });
	g.nextNodeId = 10;

	EvalContext ctx;
	ctx.controls = &resolved;
	const EvalResult result = Evaluate(g, NodeRegistry::Default(), ctx, nullptr);
	REQUIRE(result.ok);
	const Collection& out = result.nodes.at(9).output.pins.at("Terrain");
	REQUIRE(out.size() == 1);
	CHECK(out[0].op.order == 9);

	// The seed depends on the instance seed, so two graph instances differ.
	EvalContext other = ctx;
	other.instanceSeed = ctx.instanceSeed + 1;
	const EvalResult second = Evaluate(g, NodeRegistry::Default(), other, nullptr);
	REQUIRE(second.ok);
	CHECK(std::get<int64>(second.nodes.at(9).output.pins.at("Terrain")[0].op.params.at("seed")) != std::get<int64>(out[0].op.params.at("seed")));
}

TEST_CASE("Noise node output composes to a height change bounded by the amplitude", "[graph_nodes_terrain]")
{
	const PreparedTerrain terrain = Compose(Run("TerrainNoise", { { "amplitude", 2.0f } }, MakeSquare(0, 0, 100)), 3);
	float largest = 0.0f;
	for (float z = -10.0f; z <= 110.0f; z += 3.7f)
	{
		for (float x = -10.0f; x <= 110.0f; x += 3.7f)
		{
			const float delta = Written(terrain.Evaluate(x, z, 5.0f, {}), 5.0f) - 5.0f;
			CHECK(std::abs(delta) <= 2.0f + 1e-4f);
			largest = std::max(largest, std::abs(delta));
			if (x < 0.0f || x > 100.0f || z < 0.0f || z > 100.0f)
			{
				CHECK(delta == 0.0f);
			}
		}
	}
	CHECK(largest > 0.2f);
}

TEST_CASE("Carve node output composes to the depth at the centre and none past the bank", "[graph_nodes_terrain]")
{
	const PreparedTerrain terrain = Compose(Run("CarveAlongSpline", {}, Line(-50, 0, 50, 0)), 2);
	CHECK(Written(terrain.Evaluate(0.0f, 0.0f, 10.0f, {}), 10.0f) == Approx(8.5f));
	CHECK(Written(terrain.Evaluate(0.0f, 1.9f, 10.0f, {}), 10.0f) == Approx(8.5f));
	CHECK(Written(terrain.Evaluate(0.0f, 6.0f, 10.0f, {}), 10.0f) == Approx(10.0f));
}

TEST_CASE("Raise node output composes to a rise along a spline lifted above the ground", "[graph_nodes_terrain]")
{
	DataItem ridge = MakeSplineItem({ { -50, 0 }, { 50, 0 } }, false);
	for (SplinePoint& p : ridge.spline.points)
	{
		p.y = 6.0f;
	}
	const PreparedTerrain terrain = Compose(Run("RaiseAlongSpline", { { "width", 10.0f } }, ridge), 2);
	CHECK(Written(terrain.Evaluate(0.0f, 0.0f, 0.0f, {}), 0.0f) == Approx(6.0f));
	const float mid = Written(terrain.Evaluate(0.0f, 5.0f, 0.0f, {}), 0.0f);
	CHECK(mid > 0.0f);
	CHECK(mid < 6.0f);
	CHECK(Written(terrain.Evaluate(0.0f, 12.0f, 0.0f, {}), 0.0f) == 0.0f);
}

TEST_CASE("Water node output composes to the level inside and the material", "[graph_nodes_terrain]")
{
	DataItem lake = MakeSquare(0, 0, 50);
	lake.attributes["level"] = 3.0f;
	const PreparedTerrain terrain = Compose(Run("WaterArea", { { "levelOffset", 0.5f }, { "material", String("Water/Lake.hmat") } }, lake), 1);
	const TerrainTarget inside = terrain.Evaluate(25.0f, 25.0f, 0.0f, {});
	CHECK(inside.waterType == 1);
	CHECK(inside.waterHeight == Approx(3.5f));
	CHECK(terrain.Evaluate(80.0f, 25.0f, 0.0f, {}).waterType == 0);
	CHECK(terrain.GetWaterMaterial() == "Water/Lake.hmat");
}

TEST_CASE("Paint node output composes to the looked-up layer", "[graph_nodes_terrain]")
{
	const PreparedTerrain terrain = Compose(Run("PaintSplat", { { "texture", String("Terrain/Sand.htex") } }, Line(-50, 0, 50, 0)), 2);
	const TextureLayerLookup layers = [](float, float, const String& key) { return static_cast<int8>(key == "Terrain/Sand.htex" ? 2 : -1); };
	const TerrainTarget on = terrain.Evaluate(0.0f, 0.0f, 0.0f, layers);
	CHECK(on.splatWeight > 0.99f);
	CHECK(on.splat[2] == 255);
	CHECK(terrain.Evaluate(0.0f, 20.0f, 0.0f, layers).splatWeight == 0.0f);
}

TEST_CASE("Terrain nodes carry the stackOrder setting, overridable per item", "[graph_nodes_terrain]")
{
	const NodeTypeDef* noiseDef = NodeRegistry::Default().Find("TerrainNoise");
	REQUIRE(noiseDef != nullptr);
	const auto setting = std::find_if(noiseDef->settings.begin(), noiseDef->settings.end(), [](const SettingDef& s) { return s.name == "stackOrder"; });
	REQUIRE(setting != noiseDef->settings.end());
	CHECK(std::get<int64>(setting->defaultValue) == 0);
	CHECK(setting->min == -1000.0f);
	CHECK(setting->max == 1000.0f);

	CHECK(Run("TerrainNoise", {}, MakeSquare(0, 0, 50)).pins.at("Terrain")[0].op.stackOrder == 0);

	DataItem area = MakeSquare(0, 0, 50);
	CHECK(Run("TerrainNoise", { { "stackOrder", int64(-4) } }, area).pins.at("Terrain")[0].op.stackOrder == -4);
	CHECK(Run("PaintSplat", { { "stackOrder", int64(6) }, { "texture", String("T.htex") } }, area).pins.at("Terrain")[0].op.stackOrder == 6);
	CHECK(Run("WaterArea", { { "stackOrder", int64(2) } }, area).pins.at("Terrain")[0].op.stackOrder == 2);
	CHECK(Run("CarveAlongSpline", { { "stackOrder", int64(3) } }, Line(0, 0, 40, 0)).pins.at("Terrain")[0].op.stackOrder == 3);
	CHECK(Run("RaiseAlongSpline", { { "stackOrder", int64(-9) } }, Line(0, 0, 40, 0)).pins.at("Terrain")[0].op.stackOrder == -9);

	// The item attribute wins.
	area.attributes["stackOrder"] = int64(8);
	CHECK(Run("TerrainNoise", { { "stackOrder", int64(-4) } }, area).pins.at("Terrain")[0].op.stackOrder == 8);
	// Out of range values are clamped.
	area.attributes["stackOrder"] = int64(100000);
	CHECK(Run("TerrainNoise", {}, area).pins.at("Terrain")[0].op.stackOrder == 1000);
}

TEST_CASE("WaterArea has depth and shoreWidth, defaulting to no basin", "[graph_nodes_terrain]")
{
	const NodeTypeDef* def = NodeRegistry::Default().Find("WaterArea");
	REQUIRE(def != nullptr);
	bool depth = false;
	bool shore = false;
	for (const SettingDef& s : def->settings)
	{
		if (s.name == "depth")
		{
			depth = true;
			CHECK(s.min == 0.0f);
			CHECK(s.max == 200.0f);
		}
		if (s.name == "shoreWidth")
		{
			shore = true;
			CHECK(s.min == 0.5f);
			CHECK(s.max == 200.0f);
		}
	}
	CHECK(depth);
	CHECK(shore);

	const DataItem lake = MakeSquare(0, 0, 50);
	const DataItem plain = Run("WaterArea", {}, lake).pins.at("Terrain")[0];
	CHECK(Number(plain, "depth") == 0.0f);
	CHECK(Number(plain, "shoreWidth") == 6.0f);

	DataItem deep = MakeSquare(0, 0, 50);
	deep.attributes["depth"] = 2.5f;
	const DataItem item = Run("WaterArea", { { "shoreWidth", 8.0f } }, deep).pins.at("Terrain")[0];
	CHECK(Number(item, "depth") == 2.5f);
	CHECK(Number(item, "shoreWidth") == 8.0f);
}

TEST_CASE("Water node output with depth composes to a basin", "[graph_nodes_terrain]")
{
	DataItem lake = MakeSquare(0, 0, 50);
	lake.attributes["level"] = 21.5f;
	const PreparedTerrain terrain = Compose(Run("WaterArea", { { "depth", 3.0f } }, lake), 1);
	CHECK(Written(terrain.Evaluate(25.0f, 25.0f, 25.0f, {}), 25.0f) == Approx(18.5f));
}

TEST_CASE("River emits a River op with the defaults and the material", "[graph_nodes_terrain]")
{
	const NodeTypeDef* def = NodeRegistry::Default().Find("River");
	REQUIRE(def != nullptr);
	CHECK(def->category == "Terrain");
	REQUIRE(def->inputs.size() == 1);
	CHECK(def->inputs[0].accepts == std::vector<ItemKind>({ ItemKind::Spline }));
	REQUIRE(def->outputs.size() == 1);
	CHECK(def->outputs[0].name == "Terrain");
	const auto material = std::find_if(def->settings.begin(), def->settings.end(), [](const SettingDef& s) { return s.name == "material"; });
	REQUIRE(material != def->settings.end());
	CHECK(material->widget == "asset:.hmat");

	const NodeOutput out = Run("River", {}, Line(0, 0, 40, 0));
	REQUIRE(out.error.empty());
	REQUIRE(out.pins.at("Terrain").size() == 1);
	const DataItem& item = out.pins.at("Terrain")[0];
	CHECK(item.op.type == TerrainOpType::River);
	CHECK(Number(item, "width") == 5.0f);
	CHECK(Number(item, "bank") == 4.0f);
	CHECK(Number(item, "depth") == 1.5f);
	CHECK(Number(item, "surfaceOffset") == -0.4f);
	CHECK(Number(item, "waterMargin") == 3.0f);
	CHECK(item.op.asset == "Worlds/Water_River.hmat");
	CHECK(item.op.params.count("material") == 0);
	CHECK(item.op.stackOrder == 0);
	CHECK(item.sourceId == 42);
	CHECK(item.pointIds == std::vector<uint64>({ 11, 12 }));
	CHECK(String(TerrainOpTypeName(TerrainOpType::River)) == "River");
}

TEST_CASE("River settings are overridden by item attributes", "[graph_nodes_terrain]")
{
	DataItem river = Line(0, 0, 40, 0);
	river.attributes["width"] = int64(8);
	river.attributes["material"] = String("Water/Creek.hmat");
	const DataItem item = Run("River", { { "depth", 2.0f }, { "stackOrder", int64(5) } }, river).pins.at("Terrain")[0];
	CHECK(Number(item, "width") == 8.0f);
	CHECK(Number(item, "depth") == 2.0f);
	CHECK(item.op.asset == "Water/Creek.hmat");
	CHECK(item.op.stackOrder == 5);

	// An area is not a river.
	const NodeOutput area = Run("River", {}, MakeSquare(0, 0, 10));
	CHECK(area.pins.at("Terrain").empty());
	CHECK(area.warnings.size() == 1);
}

TEST_CASE("An Area link into a River node is a validation issue", "[graph_nodes_terrain]")
{
	Graph g;
	NodeInstance region;
	region.id = 1;
	region.type = "GetRegion";
	NodeInstance river;
	river.id = 2;
	river.type = "River";
	g.nodes = { region, river };
	g.links.push_back(Link{ 1, "Out", 2, "In" });
	g.nextNodeId = 3;

	const std::vector<GraphIssue> issues = ValidateGraph(g, NodeRegistry::Default());
	REQUIRE_FALSE(issues.empty());
	CHECK(issues[0].message.find("does not accept") != String::npos);
}

TEST_CASE("River node output composes to a bed under the water surface", "[graph_nodes_terrain]")
{
	// Compose uses flat ground at 0 and a spline at y 0: surface -0.4, bed -1.9.
	const PreparedTerrain terrain = Compose(Run("River", { { "material", String("Water/River.hmat") } }, Line(-50, 0, 50, 0)), 1);
	const TerrainTarget centre = terrain.Evaluate(0.0f, 0.0f, 0.0f, {});
	CHECK(Written(centre, 0.0f) == Approx(-1.9f));
	CHECK(centre.waterType == 1);
	CHECK(centre.waterHeight == Approx(-0.4f));
	CHECK(terrain.GetWaterMaterial() == "Water/River.hmat");
}
