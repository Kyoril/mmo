// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "graph_node_test_util.h"

#include "pcg/graph/controls.h"
#include "pcg/graph/evaluator.h"
#include "pcg/graph/node_registry.h"
#include "pcg/graph/terrain_op.h"

using namespace mmo;
using namespace mmo::pcg::graph;

namespace
{
	const char* const RoadPath = "Editor/Pcg/Sections/TestRoad.hsec";

	DataItem RoadSpline()
	{
		DataItem item = MakeSplineItem({ { 0, 0 }, { 20, 0 }, { 40, 10 } }, false);
		item.sourceId = 42;
		item.pointIds = { 11, 12, 13 };
		item.tags = { "road" };
		item.attributes["section"] = String(RoadPath);
		return item;
	}

	NodeOutput Lay(const String& type, const AttributeSet& settings, const DataItem& item)
	{
		std::map<String, Collection> inputs;
		inputs["In"].push_back(item);
		return ExecNode(type, settings, inputs, EvalContext());
	}

	float FloatParam(const DataItem& item, const char* name)
	{
		const auto it = item.op.params.find(name);
		REQUIRE(it != item.op.params.end());
		REQUIRE(std::holds_alternative<float>(it->second));
		return std::get<float>(it->second);
	}
}

TEST_CASE("LaySectionAlongSpline emits a SectionPath op per spline", "[graph_nodes_section]")
{
	AttributeSet settings;
	const NodeOutput out = Lay("LaySectionAlongSpline", settings, RoadSpline());
	REQUIRE(out.error.empty());
	const Collection& terrain = out.pins.at("Terrain");
	REQUIRE(terrain.size() == 1);

	const DataItem& item = terrain[0];
	CHECK(item.kind == ItemKind::Terrain);
	CHECK(item.op.type == TerrainOpType::SectionPath);
	CHECK(item.op.asset == RoadPath);
	CHECK(item.pointIds == std::vector<uint64>({ 11, 12, 13 }));
	CHECK(item.sourceId == 42);
	CHECK(item.tags.count("road") == 1);
	CHECK(item.spline.points.size() == 3);
	CHECK_FALSE(item.spline.closed);

	CHECK(FloatParam(item, "joinBlend") == 2.0f);
	CHECK(FloatParam(item, "groundSmoothing") == 20.0f);
	// Spec "Resolved 1": sections follow the smoothed base ground unless a graph asks for exact spline heights.
	CHECK(std::get<String>(item.op.params.at("heightMode")) == "smoothedGround");
	CHECK(std::get<bool>(item.op.params.at("floorInside")) == true);
	CHECK(std::get<int64>(item.op.params.at("seed")) == 1);
	CHECK(std::get<int64>(item.op.params.at("massPaintLayer")) == -1);
}

TEST_CASE("The item attribute overrides the section setting", "[graph_nodes_section]")
{
	AttributeSet settings;
	settings["section"] = String("Editor/Pcg/Sections/Other.hsec");
	CHECK(Lay("LaySectionAlongSpline", settings, RoadSpline()).pins.at("Terrain")[0].op.asset == RoadPath);

	DataItem plain = RoadSpline();
	plain.attributes.erase("section");
	CHECK(Lay("LaySectionAlongSpline", settings, plain).pins.at("Terrain")[0].op.asset == "Editor/Pcg/Sections/Other.hsec");

	settings["sectionAttribute"] = String("kind");
	plain.attributes["kind"] = String("Editor/Pcg/Sections/Kind.hsec");
	CHECK(Lay("LaySectionAlongSpline", settings, plain).pins.at("Terrain")[0].op.asset == "Editor/Pcg/Sections/Kind.hsec");
}

TEST_CASE("An empty section path is a node error", "[graph_nodes_section]")
{
	DataItem plain = RoadSpline();
	plain.attributes.erase("section");
	const NodeOutput out = Lay("LaySectionAlongSpline", AttributeSet(), plain);
	CHECK_FALSE(out.error.empty());
}

TEST_CASE("An item attribute overrides a section param setting", "[graph_nodes_section]")
{
	AttributeSet settings;
	settings["groundSmoothing"] = 10.0f;
	DataItem road = RoadSpline();
	CHECK(FloatParam(Lay("LaySectionAlongSpline", settings, road).pins.at("Terrain")[0], "groundSmoothing") == 10.0f);

	road.attributes["groundSmoothing"] = int64(40);
	const NodeOutput out = Lay("LaySectionAlongSpline", settings, road);
	REQUIRE(out.error.empty());
	CHECK(FloatParam(out.pins.at("Terrain")[0], "groundSmoothing") == 40.0f);
}

TEST_CASE("Bool section params take 0 and 1 of any number type", "[graph_nodes_section]")
{
	DataItem road = RoadSpline();
	road.attributes["massEnabled"] = 1.0f;
	road.attributes["mirrorCopies"] = int64(1);
	AttributeSet settings;
	settings["sequentialVariants"] = 0.0f;
	const NodeOutput out = Lay("LaySectionAlongSpline", settings, road);
	REQUIRE(out.error.empty());
	const DataItem& item = out.pins.at("Terrain")[0];
	REQUIRE(std::holds_alternative<bool>(item.op.params.at("massEnabled")));
	CHECK(std::get<bool>(item.op.params.at("massEnabled")) == true);
	REQUIRE(std::holds_alternative<bool>(item.op.params.at("mirrorCopies")));
	CHECK(std::get<bool>(item.op.params.at("mirrorCopies")) == true);
	REQUIRE(std::holds_alternative<bool>(item.op.params.at("sequentialVariants")));
	CHECK(std::get<bool>(item.op.params.at("sequentialVariants")) == false);

	// Other numbers stay numbers (and SetSectionPathParam decides).
	road.attributes["massEnabled"] = 0.5f;
	const NodeOutput half = Lay("LaySectionAlongSpline", AttributeSet(), road);
	if (half.error.empty())
	{
		CHECK_FALSE(std::holds_alternative<bool>(half.pins.at("Terrain")[0].op.params.at("massEnabled")));
	}
}

TEST_CASE("LaySectionAlongLoop takes closed areas only and forces closed", "[graph_nodes_section]")
{
	DataItem square = MakeSquare(0, 0, 30);
	square.attributes["section"] = String("Editor/Pcg/Sections/Cliff.hsec");
	const NodeOutput ok = Lay("LaySectionAlongLoop", AttributeSet(), square);
	REQUIRE(ok.error.empty());
	REQUIRE(ok.pins.at("Terrain").size() == 1);
	CHECK(ok.pins.at("Terrain")[0].spline.closed);
	CHECK(std::get<bool>(ok.pins.at("Terrain")[0].op.params.at("floorInside")) == true);

	AttributeSet settings;
	settings["floorInside"] = false;
	CHECK(std::get<bool>(Lay("LaySectionAlongLoop", settings, square).pins.at("Terrain")[0].op.params.at("floorInside")) == false);

	DataItem open = square;
	open.spline.closed = false;
	const NodeOutput bad = Lay("LaySectionAlongLoop", AttributeSet(), open);
	CHECK_FALSE(bad.error.empty());
}

TEST_CASE("An invalid param value is a node error naming the setting", "[graph_nodes_section]")
{
	AttributeSet settings;
	settings["heightMode"] = String("sideways");
	const NodeOutput out = Lay("LaySectionAlongSpline", settings, RoadSpline());
	CHECK(out.error.find("heightMode") != String::npos);

	DataItem road = RoadSpline();
	road.attributes["massPaintLayer"] = int64(9);
	CHECK(Lay("LaySectionAlongSpline", AttributeSet(), road).error.find("massPaintLayer") != String::npos);
}

TEST_CASE("Section nodes give empty output for empty input and are deterministic", "[graph_nodes_section]")
{
	CHECK(ExecNode("LaySectionAlongSpline", AttributeSet(), {}, EvalContext()).pins.at("Terrain").empty());

	std::map<String, Collection> none;
	none["In"];
	CHECK(ExecNode("LaySectionAlongLoop", AttributeSet(), none, EvalContext()).pins.at("Terrain").empty());

	const NodeOutput a = Lay("LaySectionAlongSpline", AttributeSet(), RoadSpline());
	const NodeOutput b = Lay("LaySectionAlongSpline", AttributeSet(), RoadSpline());
	CHECK(ContentHash(a.pins.at("Terrain")) == ContentHash(b.pins.at("Terrain")));
}

TEST_CASE("The evaluator stamps the node id as the op order", "[graph_nodes_section]")
{
	ControlType type;
	type.name = "Road";
	type.shape = ControlShape::OpenSpline;

	ControlInstance inst;
	inst.id = 5;
	inst.typePath = "Controls/Road.hctl";
	inst.spline.closed = false;
	inst.spline.points = { { 0, 0 }, { 5, 0 }, { 10, 3 } };
	inst.pointIds = { 11, 4, 8 };
	inst.nextPointId = 12;

	ResolvedControl rc;
	std::vector<String> warnings;
	REQUIRE(ResolveControl(type, inst, rc, warnings));
	const std::vector<ResolvedControl> resolved{ rc };

	Graph g;
	NodeInstance controls;
	controls.id = 1;
	controls.type = "GetControls";
	g.nodes.push_back(controls);
	NodeInstance lay;
	lay.id = 7;
	lay.type = "LaySectionAlongSpline";
	lay.settings["section"] = String(RoadPath);
	g.nodes.push_back(lay);
	g.links.push_back(Link{ 1, "Out", 7, "In" });
	g.nextNodeId = 8;

	EvalContext ctx;
	ctx.controls = &resolved;
	const EvalResult result = Evaluate(g, NodeRegistry::Default(), ctx, nullptr);
	REQUIRE(result.ok);
	const Collection& out = result.nodes.at(7).output.pins.at("Terrain");
	REQUIRE(out.size() == 1);
	CHECK(out[0].op.order == 7);
	CHECK(out[0].pointIds == std::vector<uint64>{ 11, 4, 8 });
}

TEST_CASE("A non-String section attribute is ignored with a warning", "[graph_nodes_section]")
{
	AttributeSet settings;
	settings["section"] = String("Editor/Pcg/Sections/Other.hsec");
	DataItem road = RoadSpline();
	road.attributes["section"] = int64(5);
	const NodeOutput out = Lay("LaySectionAlongSpline", settings, road);
	REQUIRE(out.error.empty());
	CHECK(out.pins.at("Terrain")[0].op.asset == "Editor/Pcg/Sections/Other.hsec");
	REQUIRE(out.warnings.size() == 1);
	CHECK(out.warnings[0].find("section") != String::npos);
}

TEST_CASE("LaySectionAlongSpline keeps a closed spline closed", "[graph_nodes_section]")
{
	DataItem ring = MakeSplineItem({ { 0, 0 }, { 30, 0 }, { 30, 30 }, { 0, 30 } }, false);
	ring.kind = ItemKind::Spline;
	ring.spline.closed = true;
	ring.attributes["section"] = String(RoadPath);
	const NodeOutput out = Lay("LaySectionAlongSpline", AttributeSet(), ring);
	REQUIRE(out.error.empty());
	REQUIRE(out.pins.at("Terrain").size() == 1);
	CHECK(out.pins.at("Terrain")[0].spline.closed);
}

TEST_CASE("A cached re-evaluation keeps the op order", "[graph_nodes_section]")
{
	Graph g;
	NodeInstance lay;
	lay.id = 7;
	lay.type = "LaySectionAlongSpline";
	lay.settings["section"] = String(RoadPath);
	g.nodes.push_back(lay);
	g.nextNodeId = 8;
	// No input link: the node outputs nothing, so feed it through a controls node instead.
	ControlType type;
	type.name = "Road";
	type.shape = ControlShape::OpenSpline;
	ControlInstance inst;
	inst.id = 5;
	inst.typePath = "Controls/Road.hctl";
	inst.spline.points = { { 0, 0 }, { 5, 0 }, { 10, 3 } };
	inst.pointIds = { 1, 2, 3 };
	inst.nextPointId = 4;
	ResolvedControl rc;
	std::vector<String> warnings;
	REQUIRE(ResolveControl(type, inst, rc, warnings));
	const std::vector<ResolvedControl> resolved{ rc };
	NodeInstance controls;
	controls.id = 1;
	controls.type = "GetControls";
	g.nodes.push_back(controls);
	g.links.push_back(Link{ 1, "Out", 7, "In" });

	EvalContext ctx;
	ctx.controls = &resolved;
	GraphCache cache;
	const EvalResult first = Evaluate(g, NodeRegistry::Default(), ctx, &cache);
	const EvalResult second = Evaluate(g, NodeRegistry::Default(), ctx, &cache);
	REQUIRE(first.ok);
	REQUIRE(second.ok);
	CHECK(second.nodes.at(7).fromCache);
	REQUIRE(second.nodes.at(7).output.pins.at("Terrain").size() == 1);
	CHECK(second.nodes.at(7).output.pins.at("Terrain")[0].op.order == 7);
}

TEST_CASE("Section nodes carry the stackOrder setting, overridable per item", "[graph_nodes_section]")
{
	for (const char* type : { "LaySectionAlongSpline", "LaySectionAlongLoop" })
	{
		const NodeTypeDef* def = NodeRegistry::Default().Find(type);
		REQUIRE(def != nullptr);
		CHECK(std::any_of(def->settings.begin(), def->settings.end(), [](const SettingDef& s) { return s.name == "stackOrder"; }));
	}

	DataItem spline = RoadSpline();
	CHECK(Lay("LaySectionAlongSpline", {}, spline).pins.at("Terrain")[0].op.stackOrder == 0);
	CHECK(Lay("LaySectionAlongSpline", { { "stackOrder", int64(-1) } }, spline).pins.at("Terrain")[0].op.stackOrder == -1);
	spline.attributes["stackOrder"] = int64(5);
	CHECK(Lay("LaySectionAlongSpline", { { "stackOrder", int64(-1) } }, spline).pins.at("Terrain")[0].op.stackOrder == 5);

	DataItem area = MakeSquare(0, 0, 30);
	area.attributes["section"] = String("Editor/Pcg/Sections/Cliff.hsec");
	CHECK(Lay("LaySectionAlongLoop", { { "stackOrder", int64(7) } }, area).pins.at("Terrain")[0].op.stackOrder == 7);
}
