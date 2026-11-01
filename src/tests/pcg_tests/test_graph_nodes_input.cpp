// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/graph/controls.h"
#include "pcg/graph/evaluator.h"
#include "pcg/graph/node_registry.h"

#include <memory>

using namespace mmo;
using namespace mmo::pcg::graph;

namespace
{
	/// Owns control types and instances so the resolved controls' pointers stay valid.
	struct ControlWorld
	{
		std::vector<std::unique_ptr<ControlType>> types;
		std::vector<std::unique_ptr<ControlInstance>> instances;
		std::vector<ResolvedControl> resolved;

		const ControlType* AddType(const String& name, const ControlShape shape, const std::set<String>& tags)
		{
			auto t = std::make_unique<ControlType>();
			t->name = name;
			t->shape = shape;
			t->defaultTags = tags;
			PropertyGroup group;
			group.name = "Main";
			PropertyDef density;
			density.name = "density";
			density.type = PropertyType::Float;
			density.defaultValue = 0.5f;
			group.properties.push_back(density);
			t->groups.push_back(group);
			types.push_back(std::move(t));
			return types.back().get();
		}

		void Add(const ControlType* type, std::unique_ptr<ControlInstance> inst)
		{
			instances.push_back(std::move(inst));
			ResolvedControl rc;
			std::vector<String> warnings;
			REQUIRE(ResolveControl(*type, *instances.back(), rc, warnings));
			resolved.push_back(rc);
		}

		void AddLoop(const ControlType* type, const uint64 id, const String& typePath, const float x0, const float z0, const float size, const std::set<String>& extraTags = {})
		{
			auto inst = std::make_unique<ControlInstance>();
			inst->id = id;
			inst->typePath = typePath;
			inst->extraTags = extraTags;
			inst->spline.closed = true;
			inst->spline.points = { { x0, z0 }, { x0 + size, z0 }, { x0 + size, z0 + size }, { x0, z0 + size } };
			inst->pointIds = { 1, 2, 3, 4 };
			inst->nextPointId = 5;
			Add(type, std::move(inst));
		}

		void AddSpline(const ControlType* type, const uint64 id, const String& typePath, const float x0, const float z0, const float x1, const float z1)
		{
			auto inst = std::make_unique<ControlInstance>();
			inst->id = id;
			inst->typePath = typePath;
			inst->spline.closed = false;
			inst->spline.points = { { x0, z0 }, { x1, z1 } };
			inst->pointIds = { 1, 2 };
			inst->nextPointId = 3;
			Add(type, std::move(inst));
		}

		void AddTransform(const ControlType* type, const uint64 id, const String& typePath, const Vector3& pos, const Vector3& halfExtents = Vector3(5, 5, 5))
		{
			auto inst = std::make_unique<ControlInstance>();
			inst->id = id;
			inst->typePath = typePath;
			inst->position = pos;
			inst->halfExtents = halfExtents;
			Add(type, std::move(inst));
		}
	};

	PathSpline Square(const float x0, const float z0, const float size)
	{
		PathSpline s;
		s.closed = true;
		s.points = { { x0, z0 }, { x0 + size, z0 }, { x0 + size, z0 + size }, { x0, z0 + size } };
		return s;
	}

	/// Evaluates a single node and returns its output.
	NodeOutput RunNode(const String& type, const AttributeSet& settings, const EvalContext& ctx, const std::vector<ParameterDef>& parameters = {})
	{
		Graph g;
		NodeInstance n;
		n.id = 1;
		n.type = type;
		n.settings = settings;
		g.nodes.push_back(n);
		g.parameters = parameters;
		g.nextNodeId = 2;
		const EvalResult result = Evaluate(g, NodeRegistry::Default(), ctx, nullptr);
		REQUIRE(result.issues.empty());
		REQUIRE(result.ok);
		return result.nodes.at(1).output;
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
}

TEST_CASE("Input node types are registered in the default registry", "[graph_nodes_input]")
{
	const NodeRegistry& r = NodeRegistry::Default();
	for (const char* type : { "GetControls", "GraphParameters", "GetRegion" })
	{
		const NodeTypeDef* def = r.Find(type);
		REQUIRE(def != nullptr);
		CHECK(def->category == "Input");
		CHECK(def->inputs.empty());
		REQUIRE(def->outputs.size() == 1);
		CHECK(def->outputs[0].name == "Out");
	}
	CHECK(r.Find("GetControls")->contextDeps == (context_dep::Controls | context_dep::Region));
	CHECK(r.Find("GraphParameters")->contextDeps == context_dep::Parameters);
	CHECK(r.Find("GetRegion")->contextDeps == context_dep::Region);
}

TEST_CASE("Get Controls filters by tags (all of), exclude tags and control type", "[graph_nodes_input]")
{
	ControlWorld w;
	const ControlType* forest = w.AddType("Forest", ControlShape::ClosedLoop, { "forest", "area" });
	const ControlType* lake = w.AddType("Lake", ControlShape::ClosedLoop, { "water", "area" });
	w.AddLoop(forest, 30, "Controls/Forest.hctl", 0, 0, 10);
	w.AddLoop(forest, 10, "Controls/Forest.hctl", 20, 0, 10, { "dense" });
	w.AddLoop(lake, 20, "Controls/Lake.hctl", 40, 0, 10);

	EvalContext ctx;
	ctx.controls = &w.resolved;

	SECTION("no filter returns every control, sorted by control id")
	{
		const NodeOutput out = RunNode("GetControls", {}, ctx);
		CHECK(out.error.empty());
		CHECK(SourceIds(out.pins.at("Out")) == std::vector<uint64>{ 10, 20, 30 });
	}
	SECTION("single tag")
	{
		const NodeOutput out = RunNode("GetControls", { { "tags", String("forest") } }, ctx);
		CHECK(SourceIds(out.pins.at("Out")) == std::vector<uint64>{ 10, 30 });
	}
	SECTION("all listed tags must match")
	{
		const NodeOutput out = RunNode("GetControls", { { "tags", String("forest, dense") } }, ctx);
		CHECK(SourceIds(out.pins.at("Out")) == std::vector<uint64>{ 10 });
	}
	SECTION("shared tag")
	{
		const NodeOutput out = RunNode("GetControls", { { "tags", String("area") } }, ctx);
		CHECK(SourceIds(out.pins.at("Out")) == std::vector<uint64>{ 10, 20, 30 });
	}
	SECTION("exclude tags drop any control with one of them")
	{
		const NodeOutput out = RunNode("GetControls", { { "tags", String("area") }, { "excludeTags", String("water,dense") } }, ctx);
		CHECK(SourceIds(out.pins.at("Out")) == std::vector<uint64>{ 30 });
	}
	SECTION("control type path")
	{
		const NodeOutput out = RunNode("GetControls", { { "controlType", String("Controls/Lake.hctl") } }, ctx);
		CHECK(SourceIds(out.pins.at("Out")) == std::vector<uint64>{ 20 });
	}
	SECTION("unknown tag matches nothing")
	{
		const NodeOutput out = RunNode("GetControls", { { "tags", String("desert") } }, ctx);
		CHECK(out.pins.at("Out").empty());
	}
}

TEST_CASE("Get Controls culls controls outside the region bounds", "[graph_nodes_input]")
{
	ControlWorld w;
	const ControlType* forest = w.AddType("Forest", ControlShape::ClosedLoop, { "forest" });
	const ControlType* road = w.AddType("Road", ControlShape::OpenSpline, { "road" });
	const ControlType* rock = w.AddType("Rock", ControlShape::Point, { "rock" });
	const ControlType* box = w.AddType("Box", ControlShape::Volume, { "box" });
	w.AddLoop(forest, 1, "F", 0, 0, 10);            // inside
	w.AddLoop(forest, 2, "F", 95, 95, 10);          // overlaps the corner
	w.AddLoop(forest, 3, "F", 200, 200, 10);        // outside
	w.AddSpline(road, 4, "R", -50, 50, 150, 50);    // crosses
	w.AddSpline(road, 5, "R", -50, 150, 150, 150);  // outside
	w.AddTransform(rock, 6, "K", Vector3(50, 0, 50));
	w.AddTransform(rock, 7, "K", Vector3(-1, 0, 50));
	w.AddTransform(box, 8, "B", Vector3(-4, 0, 50), Vector3(5, 5, 5)); // reaches into the region
	w.AddTransform(box, 9, "B", Vector3(-6, 0, 50), Vector3(5, 5, 5)); // stays outside

	EvalContext ctx;
	ctx.controls = &w.resolved;
	ctx.region = Square(0, 0, 100);

	const NodeOutput out = RunNode("GetControls", {}, ctx);
	CHECK(SourceIds(out.pins.at("Out")) == std::vector<uint64>{ 1, 2, 4, 6, 8 });

	SECTION("an empty region does not cull")
	{
		ctx.region = PathSpline();
		const NodeOutput all = RunNode("GetControls", {}, ctx);
		CHECK(all.pins.at("Out").size() == 9);
	}
}

TEST_CASE("Get Controls maps shapes to item kinds and carries tags and attributes", "[graph_nodes_input]")
{
	ControlWorld w;
	const ControlType* forest = w.AddType("Forest", ControlShape::ClosedLoop, { "forest" });
	const ControlType* road = w.AddType("Road", ControlShape::OpenSpline, { "road" });
	const ControlType* rock = w.AddType("Rock", ControlShape::Point, { "rock" });
	const ControlType* box = w.AddType("Box", ControlShape::Volume, { "box" });
	w.AddLoop(forest, 1, "F", 0, 0, 10, { "extra" });
	w.AddSpline(road, 2, "R", 0, 0, 30, 0);
	w.AddTransform(rock, 3, "K", Vector3(4, 2, 6));
	w.AddTransform(box, 4, "B", Vector3(10, 3, 20), Vector3(2, 1.5f, 4));
	w.instances[0]->overrides["density"] = 0.75f;
	std::vector<String> warnings;
	REQUIRE(ResolveControl(*forest, *w.instances[0], w.resolved[0], warnings));

	EvalContext ctx;
	ctx.controls = &w.resolved;
	const NodeOutput out = RunNode("GetControls", {}, ctx);
	const Collection& c = out.pins.at("Out");
	REQUIRE(c.size() == 4);

	// Loop -> Area
	CHECK(c[0].kind == ItemKind::Area);
	CHECK(c[0].sourceId == 1);
	CHECK(c[0].tags == std::set<String>{ "forest", "extra" });
	REQUIRE(c[0].attributes.count("density") == 1);
	CHECK(std::get<float>(c[0].attributes.at("density")) == Approx(0.75f));
	CHECK(c[0].spline.closed);
	CHECK(c[0].spline.points.size() == 4);

	// Open spline -> Spline
	CHECK(c[1].kind == ItemKind::Spline);
	CHECK_FALSE(c[1].spline.closed);
	CHECK(c[1].spline.points.size() == 2);
	CHECK(std::get<float>(c[1].attributes.at("density")) == Approx(0.5f));

	// Point -> Points with one point at the control's transform
	CHECK(c[2].kind == ItemKind::Points);
	REQUIRE(c[2].points.points.size() == 1);
	CHECK(c[2].points.points[0].position.x == Approx(4));
	CHECK(c[2].points.points[0].position.y == Approx(2));
	CHECK(c[2].points.points[0].position.z == Approx(6));
	CHECK(c[2].points.points[0].seed != 0);
	CHECK(c[2].tags == std::set<String>{ "rock" });

	// Volume -> Area (box footprint) with volumeHeight
	CHECK(c[3].kind == ItemKind::Area);
	CHECK(c[3].spline.closed);
	REQUIRE(c[3].spline.points.size() == 4);
	REQUIRE(c[3].attributes.count("volumeHeight") == 1);
	CHECK(std::get<float>(c[3].attributes.at("volumeHeight")) == Approx(3.0f));
	float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
	for (const SplinePoint& p : c[3].spline.points)
	{
		minX = std::min(minX, p.x);
		maxX = std::max(maxX, p.x);
		minZ = std::min(minZ, p.z);
		maxZ = std::max(maxZ, p.z);
		CHECK(p.y == Approx(1.5f));
	}
	CHECK(minX == Approx(8));
	CHECK(maxX == Approx(12));
	CHECK(minZ == Approx(16));
	CHECK(maxZ == Approx(24));
}

TEST_CASE("Get Controls without controls outputs an empty collection", "[graph_nodes_input]")
{
	EvalContext ctx;
	SECTION("null controls")
	{
		const NodeOutput out = RunNode("GetControls", { { "tags", String("forest") } }, ctx);
		CHECK(out.error.empty());
		CHECK(out.pins.at("Out").empty());
	}
	SECTION("empty controls")
	{
		std::vector<ResolvedControl> none;
		ctx.controls = &none;
		const NodeOutput out = RunNode("GetControls", {}, ctx);
		CHECK(out.pins.at("Out").empty());
	}
}

TEST_CASE("Get Controls output is deterministic and re-runs when controls change", "[graph_nodes_input]")
{
	ControlWorld w;
	const ControlType* rock = w.AddType("Rock", ControlShape::Point, { "rock" });
	w.AddTransform(rock, 3, "K", Vector3(4, 2, 6));

	EvalContext ctx;
	ctx.controls = &w.resolved;
	ctx.controlsRevision = ControlsHash(w.resolved);

	Graph g;
	NodeInstance n;
	n.id = 1;
	n.type = "GetControls";
	g.nodes.push_back(n);
	GraphCache cache;
	const EvalResult a = Evaluate(g, NodeRegistry::Default(), ctx, &cache);
	const EvalResult b = Evaluate(g, NodeRegistry::Default(), ctx, nullptr);
	CHECK(a.nodes.at(1).hash == b.nodes.at(1).hash);

	w.instances[0]->position = Vector3(5, 2, 6);
	ctx.controlsRevision = ControlsHash(w.resolved);
	const EvalResult c = Evaluate(g, NodeRegistry::Default(), ctx, &cache);
	CHECK_FALSE(c.nodes.at(1).fromCache);
	CHECK(c.nodes.at(1).hash != a.nodes.at(1).hash);
}

TEST_CASE("Graph Parameters merges overrides over the graph defaults", "[graph_nodes_input]")
{
	std::vector<ParameterDef> params;
	params.push_back(ParameterDef{ "density", 0.5f, "" });
	params.push_back(ParameterDef{ "count", int64(4), "" });
	params.push_back(ParameterDef{ "biome", String("forest"), "" });

	EvalContext ctx;
	ctx.parameters["density"] = 0.8f;
	ctx.parameters["count"] = String("wrong type");
	ctx.parameters["extra"] = true;

	const NodeOutput out = RunNode("GraphParameters", {}, ctx, params);
	const Collection& c = out.pins.at("Out");
	REQUIRE(c.size() == 1);
	CHECK(c[0].kind == ItemKind::Params);
	const AttributeSet& a = c[0].attributes;
	CHECK(std::get<float>(a.at("density")) == Approx(0.8f));
	CHECK(std::get<int64>(a.at("count")) == 4);             // mismatched override falls back to the default
	CHECK(std::get<String>(a.at("biome")) == "forest");     // default
	CHECK(std::get<bool>(a.at("extra")) == true);           // undeclared override is kept
	CHECK_FALSE(out.warnings.empty());                      // the mismatch is reported

	SECTION("no parameters and no overrides -> one empty Params item")
	{
		const NodeOutput empty = RunNode("GraphParameters", {}, EvalContext());
		REQUIRE(empty.pins.at("Out").size() == 1);
		CHECK(empty.pins.at("Out")[0].attributes.empty());
	}
}

TEST_CASE("Changing a graph parameter default re-executes a cached Graph Parameters node", "[graph_nodes_input]")
{
	Graph g;
	NodeInstance n;
	n.id = 1;
	n.type = "GraphParameters";
	g.nodes.push_back(n);
	g.parameters.push_back(ParameterDef{ "density", 0.5f, "" });
	const EvalContext ctx;

	GraphCache cache;
	const EvalResult a = Evaluate(g, NodeRegistry::Default(), ctx, &cache);
	const EvalResult b = Evaluate(g, NodeRegistry::Default(), ctx, &cache);
	CHECK(b.nodes.at(1).fromCache);

	g.parameters[0].defaultValue = 0.7f;
	const EvalResult c = Evaluate(g, NodeRegistry::Default(), ctx, &cache);
	CHECK_FALSE(c.nodes.at(1).fromCache);
	CHECK(std::get<float>(c.nodes.at(1).output.pins.at("Out").at(0).attributes.at("density")) == Approx(0.7f));
	CHECK(c.nodes.at(1).hash != a.nodes.at(1).hash);
}

TEST_CASE("MergeParameters overlays matching overrides", "[graph_nodes_input]")
{
	std::vector<ParameterDef> params;
	params.push_back(ParameterDef{ "a", 1.0f, "" });
	params.push_back(ParameterDef{ "b", int64(2), "" });
	AttributeSet overrides;
	overrides["a"] = 3.0f;
	overrides["b"] = 1.0f;
	std::vector<String> warnings;
	const AttributeSet merged = MergeParameters(params, overrides, &warnings);
	CHECK(std::get<float>(merged.at("a")) == 3.0f);
	CHECK(std::get<int64>(merged.at("b")) == 2);
	CHECK(warnings.size() == 1);
}

TEST_CASE("Get Region outputs the instance region as an Area item", "[graph_nodes_input]")
{
	EvalContext ctx;
	ctx.region = Square(0, 0, 50);
	const NodeOutput out = RunNode("GetRegion", {}, ctx);
	const Collection& c = out.pins.at("Out");
	REQUIRE(c.size() == 1);
	CHECK(c[0].kind == ItemKind::Area);
	CHECK(c[0].spline.closed);
	CHECK(c[0].spline.points.size() == 4);
	CHECK(c[0].sourceId == 0);

	SECTION("an empty or degenerate region outputs nothing")
	{
		ctx.region = PathSpline();
		CHECK(RunNode("GetRegion", {}, ctx).pins.at("Out").empty());
		ctx.region.points = { { 0, 0 }, { 1, 0 } };
		CHECK(RunNode("GetRegion", {}, ctx).pins.at("Out").empty());
	}
}
