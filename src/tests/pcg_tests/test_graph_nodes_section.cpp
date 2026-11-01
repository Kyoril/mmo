// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <array>

#include "graph_node_test_util.h"

#include "pcg/graph/controls.h"
#include "pcg/graph/evaluator.h"
#include "pcg/graph/node_registry.h"
#include "pcg/graph/terrain_compose.h"
#include "pcg/graph/terrain_op.h"
#include "pcg/section_path.h"

using namespace mmo;
using namespace mmo::pcg;
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

namespace
{
	const char* const StampPath = "Sections/TestRoad.hsec";

	/// L 20, halfWidth 3, a 0.5 m deep bed for |x| <= 2 and one baked prop at local (4, 10).
	SectionAsset MakeStampRoad()
	{
		SectionAsset asset;
		asset.name = "TestRoad";
		asset.length = 20.0f;
		asset.halfWidth = 3.0f;
		asset.sideFalloff = 4.0f;
		SectionVariant v;
		v.heights.spacing = 1.0f;
		v.heights.originX = -8;
		v.heights.width = 17;
		v.heights.depth = 21;
		v.paint.spacing = 1.0f;
		v.paint.originX = -8;
		v.paint.width = 17;
		v.paint.depth = 21;
		for (uint32 j = 0; j < 21; ++j)
		{
			for (uint32 i = 0; i < 17; ++i)
			{
				const bool road = std::abs(-8 + static_cast<int32>(i)) <= 2;
				v.heights.values.push_back(road ? -0.5f : 0.0f);
				v.paint.packed.push_back(road ? 55u : 255u);
				v.paint.coverage.push_back(road ? 255 : 0);
			}
		}
		SectionPlacement prop;
		prop.placement.key = 42;
		prop.placement.asset = "Props/Lamp.hmsh";
		prop.placement.position = Vector3(4.0f, 0.0f, 10.0f);
		v.placements.push_back(prop);
		asset.variants.push_back(v);
		return asset;
	}

	/// A Points item with one point per (x, y, z, yaw degrees); point seeds are 100 + index.
	DataItem StampPoints(const std::vector<std::array<float, 4>>& pts)
	{
		DataItem item;
		item.kind = ItemKind::Points;
		item.sourceId = 7;
		for (size_t i = 0; i < pts.size(); ++i)
		{
			Point p;
			p.position = Vector3(pts[i][0], pts[i][1], pts[i][2]);
			p.rotation = Quaternion(Radian(pts[i][3] * 3.14159265f / 180.0f), Vector3::UnitY);
			p.seed = 100 + i;
			item.points.points.push_back(p);
		}
		return item;
	}

	NodeOutput Stamp(const AttributeSet& settings, const DataItem& points)
	{
		std::map<String, Collection> inputs;
		inputs["In"].push_back(points);
		return ExecNode("StampSectionAtPoints", settings, inputs, EvalContext());
	}

	AttributeSet StampSettings(const char* anchor, const float length)
	{
		AttributeSet settings;
		settings["section"] = String(StampPath);
		settings["anchor"] = String(anchor);
		settings["length"] = length;
		return settings;
	}

	PathSpline StampRegion(const float x0, const float z0, const float size)
	{
		return MakeSquare(x0, z0, size).spline;
	}

	/// Prepares the node's items as one terrain and returns the placements.
	std::vector<Placement> StampPlacements(const Collection& items, const PathSpline& region)
	{
		const std::map<String, SectionAsset> assets{ { StampPath, MakeStampRoad() } };
		const SectionAssetProvider provider = [&assets](const String& path, String& err) -> const SectionAsset*
		{
			const auto it = assets.find(path);
			if (it == assets.end())
			{
				err = "not found";
				return nullptr;
			}
			return &it->second;
		};
		std::vector<TerrainOpInput> ops;
		for (size_t i = 0; i < items.size(); ++i)
		{
			TerrainOpInput in;
			in.op = items[i].op;
			in.op.order = 4;
			in.spline = items[i].spline;
			in.pointIds = items[i].pointIds;
			in.sourceId = items[i].sourceId;
			in.itemIndex = static_cast<uint32>(i);
			ops.push_back(in);
		}
		PreparedTerrain t;
		String err;
		const bool ok = t.Prepare(ops, [](float, float) { return 0.0f; }, provider, region, 10.0f, err);
		REQUIRE(ok);
		std::vector<Placement> placements;
		t.BuildPlacements([](float, float) { return 0.0f; }, placements);
		return placements;
	}
}

TEST_CASE("StampSectionAtPoints lays the spline ends along each point's facing", "[graph_nodes_section]")
{
	const DataItem points = StampPoints({ { 10, 3, 20, 0 }, { 10, 3, 20, 90 }, { 10, 3, 20, 180 } });

	const NodeOutput centered = Stamp(StampSettings("center", 20.0f), points);
	REQUIRE(centered.error.empty());
	const Collection& c = centered.pins.at("Terrain");
	REQUIRE(c.size() == 3);
	// yaw 0 faces +Z, 90 faces +X, 180 faces -Z.
	CHECK(c[0].spline.points[0].x == Approx(10.0f).margin(1e-4f));
	CHECK(c[0].spline.points[0].z == Approx(10.0f).margin(1e-4f));
	CHECK(c[0].spline.points[1].x == Approx(10.0f).margin(1e-4f));
	CHECK(c[0].spline.points[1].z == Approx(30.0f).margin(1e-4f));
	CHECK(c[1].spline.points[0].x == Approx(0.0f).margin(1e-4f));
	CHECK(c[1].spline.points[1].x == Approx(20.0f).margin(1e-4f));
	CHECK(c[1].spline.points[1].z == Approx(20.0f).margin(1e-4f));
	CHECK(c[2].spline.points[0].z == Approx(30.0f).margin(1e-4f));
	CHECK(c[2].spline.points[1].z == Approx(10.0f).margin(1e-4f));

	const NodeOutput started = Stamp(StampSettings("start", 20.0f), points);
	REQUIRE(started.error.empty());
	const Collection& s = started.pins.at("Terrain");
	REQUIRE(s.size() == 3);
	CHECK(s[0].spline.points[0].z == Approx(20.0f).margin(1e-4f));
	CHECK(s[0].spline.points[1].z == Approx(40.0f).margin(1e-4f));
	CHECK(s[1].spline.points[0].x == Approx(10.0f).margin(1e-4f));
	CHECK(s[1].spline.points[1].x == Approx(30.0f).margin(1e-4f));
	CHECK(s[2].spline.points[0].z == Approx(20.0f).margin(1e-4f));
	CHECK(s[2].spline.points[1].z == Approx(0.0f).margin(1e-4f));

	for (const DataItem& item : c)
	{
		CHECK(item.kind == ItemKind::Terrain);
		CHECK(item.op.type == TerrainOpType::SectionPath);
		CHECK(item.op.asset == StampPath);
		CHECK_FALSE(item.spline.closed);
		CHECK(item.spline.interpolation == SplineInterpolation::Linear);
		// The point's y lands in both spline points.
		CHECK(item.spline.points[0].y == 3.0f);
		CHECK(item.spline.points[1].y == 3.0f);
		CHECK(item.sourceId == 7);
	}
}

TEST_CASE("StampSectionAtPoints follows the spline height unless told otherwise", "[graph_nodes_section]")
{
	const DataItem points = StampPoints({ { 0, 0, 0, 0 } });
	CHECK(std::get<String>(Stamp(StampSettings("center", 20.0f), points).pins.at("Terrain")[0].op.params.at("heightMode")) == "followSpline");

	AttributeSet settings = StampSettings("center", 20.0f);
	settings["heightMode"] = String("followGround");
	CHECK(std::get<String>(Stamp(settings, points).pins.at("Terrain")[0].op.params.at("heightMode")) == "followGround");

	// Every other section param keeps the Lay Section default.
	const NodeOutput plain = Stamp(StampSettings("center", 20.0f), points);
	const DataItem& item = plain.pins.at("Terrain")[0];
	CHECK(FloatParam(item, "joinBlend") == 2.0f);
	CHECK(item.op.stackOrder == 0);
	CHECK(item.op.params.count(StampLengthParam) == 0);

	const NodeTypeDef* def = NodeRegistry::Default().Find("StampSectionAtPoints");
	REQUIRE(def != nullptr);
	CHECK(def->category == "Spawners");
}

TEST_CASE("StampSectionAtPoints ids come from the point seeds in point order", "[graph_nodes_section]")
{
	const DataItem points = StampPoints({ { 0, 0, 0, 0 }, { 1, 0, 0, 0 } });
	const NodeOutput out = Stamp(StampSettings("center", 20.0f), points);
	const Collection& items = out.pins.at("Terrain");
	REQUIRE(items.size() == 2);
	CHECK(items[0].pointIds == std::vector<uint64>({ pcg::Hash(100, 0), pcg::Hash(100, 1) }));
	CHECK(items[1].pointIds == std::vector<uint64>({ pcg::Hash(101, 0), pcg::Hash(101, 1) }));
	CHECK(items[0].pointIds != items[1].pointIds);
}

TEST_CASE("A straight up point stamps along +Z with one warning", "[graph_nodes_section]")
{
	DataItem points = StampPoints({ { 0, 0, 0, 0 }, { 5, 0, 0, 0 } });
	for (Point& p : points.points.points)
	{
		p.rotation = Quaternion(Radian(-1.5707963f), Vector3::UnitX);
	}
	const NodeOutput out = Stamp(StampSettings("start", 10.0f), points);
	REQUIRE(out.error.empty());
	CHECK(out.pins.at("Terrain")[0].spline.points[1].z == Approx(10.0f).margin(1e-3f));
	CHECK(out.warnings.size() == 1);
}

TEST_CASE("StampSectionAtPoints with length 0 uses the asset's length when composed", "[graph_nodes_section]")
{
	const DataItem points = StampPoints({ { 0, 0, 50, 0 } });
	const NodeOutput zero = Stamp(StampSettings("center", 0.0f), points);
	const NodeOutput fixed = Stamp(StampSettings("center", 20.0f), points);
	const std::vector<Placement> a = StampPlacements(zero.pins.at("Terrain"), StampRegion(-200, -200, 400));
	const std::vector<Placement> b = StampPlacements(fixed.pins.at("Terrain"), StampRegion(-200, -200, 400));
	REQUIRE(a.size() == 1);
	REQUIRE(b.size() == 1);
	CHECK(a[0].key == b[0].key);
	CHECK(a[0].position.x == Approx(b[0].position.x).margin(1e-4f));
	CHECK(a[0].position.z == Approx(50.0f).margin(1e-3f));
	CHECK(std::abs(a[0].position.x) == Approx(4.0f).margin(1e-3f));

	// Anchored at the start the copy covers 50..70, so its prop sits at z 60; yawed 90 degrees it lies along +X.
	const NodeOutput started = Stamp(StampSettings("start", 0.0f), StampPoints({ { 0, 0, 50, 0 }, { 100, 0, 50, 90 } }));
	const std::vector<Placement> c = StampPlacements(started.pins.at("Terrain"), StampRegion(-200, -200, 400));
	REQUIRE(c.size() == 2);
	CHECK(c[0].position.z == Approx(60.0f).margin(1e-3f));
	CHECK(c[1].position.x == Approx(110.0f).margin(1e-3f));
}

TEST_CASE("Two stamps one metre apart keep distinct placement keys", "[graph_nodes_section]")
{
	const NodeOutput out = Stamp(StampSettings("center", 20.0f), StampPoints({ { 0, 0, 50, 0 }, { 1, 0, 50, 0 } }));
	const std::vector<Placement> placements = StampPlacements(out.pins.at("Terrain"), StampRegion(-200, -200, 400));
	REQUIRE(placements.size() == 2);
	CHECK(placements[0].key != placements[1].key);
}

TEST_CASE("Stamps are clipped to the region", "[graph_nodes_section]")
{
	// Outside the region: nothing.
	const NodeOutput outside = Stamp(StampSettings("center", 20.0f), StampPoints({ { 0, 0, 500, 0 } }));
	CHECK(StampPlacements(outside.pins.at("Terrain"), StampRegion(-10, -10, 100)).empty());

	// Region border at z 34: the stamp at z 0..20 keeps its prop (z 10), the one at 20..40 loses it (4 m inside the
	// border the fade is below 0.5).
	const NodeOutput across = Stamp(StampSettings("start", 20.0f), StampPoints({ { 0, 0, 0, 0 }, { 0, 0, 20, 0 } }));
	const std::vector<Placement> kept = StampPlacements(across.pins.at("Terrain"), StampRegion(-10, -10, 44));
	REQUIRE(kept.size() == 1);
	CHECK(kept[0].position.z == Approx(10.0f).margin(0.01f));
}

TEST_CASE("StampSectionAtPoints validates and handles empty input", "[graph_nodes_section]")
{
	const NodeOutput missing = Stamp(AttributeSet(), StampPoints({ { 0, 0, 0, 0 } }));
	CHECK_FALSE(missing.error.empty());
	CHECK(missing.error.find("section") != String::npos);

	const NodeOutput none = Stamp(StampSettings("center", 20.0f), StampPoints({}));
	CHECK(none.error.empty());
	CHECK(none.pins.at("Terrain").empty());

	CHECK_FALSE(Stamp(StampSettings("middle", 20.0f), StampPoints({ { 0, 0, 0, 0 } })).error.empty());
	CHECK_FALSE(Stamp(StampSettings("center", -1.0f), StampPoints({ { 0, 0, 0, 0 } })).error.empty());
}
