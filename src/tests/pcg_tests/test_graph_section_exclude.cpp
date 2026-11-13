// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "graph_node_test_util.h"

#include "pcg/graph/evaluator.h"
#include "pcg/graph/graph.h"
#include "pcg/graph/graph_data.h"
#include "pcg/graph/node_registry.h"
#include "pcg/graph/terrain_compose.h"
#include "pcg/section_path.h"

#include <algorithm>
#include <cmath>
#include <map>

using namespace mmo;
using namespace mmo::pcg;
using namespace mmo::pcg::graph;

namespace
{
	const char* const RoadPath = "Editor/Pcg/Sections/TestRoad.hsec";

	AttributeSet SectionSettings()
	{
		AttributeSet settings;
		settings["section"] = String(RoadPath);
		return settings;
	}

	DataItem StampPoints()
	{
		return MakePoints({ { 0.0f, 0.0f }, { 30.0f, 5.0f } });
	}

	/// A spline item along x at the given z, with a clearance attribute.
	DataItem ClearanceSpline(const float z, const float clearance)
	{
		DataItem item = MakeSplineItem({ { -50.0f, z }, { 50.0f, z } }, false);
		item.attributes["clearance"] = clearance;
		return item;
	}

	/// The main input of a section node type, with the given Exclude items.
	std::map<String, Collection> SectionInputs(const String& type, const Collection& exclude)
	{
		std::map<String, Collection> inputs;
		if (type == "LaySectionAlongLoop")
		{
			inputs["In"].push_back(MakeSquare(0.0f, 0.0f, 40.0f));
		}
		else if (type == "LaySectionAlongSpline")
		{
			inputs["In"].push_back(MakeSplineItem({ { 0.0f, 0.0f }, { 20.0f, 0.0f }, { 40.0f, 10.0f } }, false));
		}
		else
		{
			inputs["In"].push_back(StampPoints());
		}
		if (!exclude.empty())
		{
			inputs["Exclude"] = exclude;
		}
		return inputs;
	}

	/// TestRoad-like section (as in test_graph_terrain_compose.cpp): L 20, halfWidth 3, sideFalloff 4, bed 0.5 deep
	/// for |x| <= 2, and one baked prop at local (4, 10) per copy, so a long road has a run of props 20 m apart.
	SectionAsset MakeRoad()
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
				const int32 x = -8 + static_cast<int32>(i);
				const bool road = std::abs(x) <= 2;
				v.heights.values.push_back(road ? -0.5f : 0.0f);
				v.paint.packed.push_back(road ? (55u | (200u << 16)) : 255u);
				v.paint.coverage.push_back(road ? 255 : 0);
			}
		}
		SectionPlacement prop;
		prop.placement.key = 42;
		prop.placement.asset = "Props/Fence.hmsh";
		prop.placement.position = Vector3(4.0f, 0.0f, 10.0f);
		v.placements.push_back(prop);
		asset.variants.push_back(v);
		return asset;
	}

	PathSpline SquareSpline(const float x0, const float z0, const float sizeX, const float sizeZ)
	{
		PathSpline s;
		s.closed = true;
		s.points = { { x0, z0 }, { x0 + sizeX, z0 }, { x0 + sizeX, z0 + sizeZ }, { x0, z0 + sizeZ } };
		return s;
	}

	/// One SectionPath op along x = 0 from z = -100 to 100, with the given exclusions.
	TerrainOpInput RoadOp(const std::vector<ExclusionShape>& exclusions)
	{
		TerrainOpInput in;
		in.op.type = TerrainOpType::SectionPath;
		in.op.order = 4;
		in.op.asset = RoadPath;
		in.spline.points = { { 0.0f, -100.0f }, { 0.0f, 100.0f } };
		in.pointIds = { 100, 101 };
		in.sourceId = 9;
		in.exclusions = exclusions;
		return in;
	}

	float Flat0(float, float)
	{
		return 0.0f;
	}

	struct Composed
	{
		std::map<String, SectionAsset> assets;
		PreparedTerrain terrain;
	};

	void Compose(Composed& c, const std::vector<ExclusionShape>& exclusions)
	{
		c.assets[RoadPath] = MakeRoad();
		const std::map<String, SectionAsset>& assets = c.assets;
		const SectionAssetProvider provider = [&assets](const String& path, String& error) -> const SectionAsset*
		{
			const auto it = assets.find(path);
			if (it == assets.end())
			{
				error = "not found";
				return nullptr;
			}
			return &it->second;
		};
		String error;
		REQUIRE(c.terrain.Prepare({ RoadOp(exclusions) }, Flat0, provider, SquareSpline(-500.0f, -500.0f, 1000.0f, 1000.0f), 10.0f, error));
		CHECK(error.empty());
	}

	std::vector<Placement> Placements(const std::vector<ExclusionShape>& exclusions)
	{
		Composed c;
		Compose(c, exclusions);
		std::vector<Placement> out;
		c.terrain.BuildPlacements(Flat0, out);
		return out;
	}
}

TEST_CASE("section nodes carry the Exclude input on their terrain items", "[graph_section_exclude]")
{
	for (const char* type : { "LaySectionAlongSpline", "LaySectionAlongLoop", "StampSectionAtPoints" })
	{
		INFO(type);
		const NodeTypeDef* def = NodeRegistry::Default().Find(type);
		REQUIRE(def != nullptr);
		const auto pin = std::find_if(def->inputs.begin(), def->inputs.end(), [](const PinDef& p) { return p.name == "Exclude"; });
		REQUIRE(pin != def->inputs.end());
		CHECK(pin->accepts == std::vector<ItemKind>{ ItemKind::Area, ItemKind::Spline });
		CHECK(pin->multiple);
		CHECK_FALSE(pin->required);
		size_t distanceSettings = 0;
		size_t clearanceSettings = 0;
		for (const SettingDef& s : def->settings)
		{
			if (s.name == "distance")
			{
				++distanceSettings;
				CHECK(std::get<float>(s.defaultValue) == 0.0f);
				CHECK(s.min == 0.0f);
				CHECK(s.max == 100.0f);
			}
			if (s.name == "clearanceAttribute")
			{
				++clearanceSettings;
				CHECK(std::get<String>(s.defaultValue) == "clearance");
			}
		}
		CHECK(distanceSettings == 1);
		CHECK(clearanceSettings == 1);

		AttributeSet settings = SectionSettings();
		settings["distance"] = 1.0f;
		const DataItem area = MakeSquare(5.0f, 5.0f, 10.0f);
		const NodeOutput out = ExecNode(type, settings, SectionInputs(type, { area, ClearanceSpline(20.0f, 6.0f) }), EvalContext());
		REQUIRE(out.error.empty());
		const Collection& terrain = out.pins.at("Terrain");
		REQUIRE_FALSE(terrain.empty());
		for (const DataItem& item : terrain)
		{
			REQUIRE(item.exclusions.size() == 2);
			CHECK(item.exclusions[0].area);
			CHECK(item.exclusions[0].spline.closed);
			CHECK(item.exclusions[0].spline.points.size() == area.spline.points.size());
			CHECK_FALSE(item.exclusions[1].area);
			CHECK(item.exclusions[1].radius == 7.0f);
			CHECK(item.exclusions[1].spline.points.size() == 2);
		}
	}

	// A spline with clearance 0 and distance 0 is not carried.
	const NodeOutput none = ExecNode("LaySectionAlongLoop", SectionSettings(), SectionInputs("LaySectionAlongLoop", { ClearanceSpline(20.0f, 0.0f) }), EvalContext());
	REQUIRE(none.error.empty());
	REQUIRE(none.pins.at("Terrain").size() == 1);
	CHECK(none.pins.at("Terrain")[0].exclusions.empty());

	// The clearance attribute can be renamed or switched off.
	AttributeSet off = SectionSettings();
	off["clearanceAttribute"] = String();
	const NodeOutput ignored = ExecNode("LaySectionAlongLoop", off, SectionInputs("LaySectionAlongLoop", { ClearanceSpline(20.0f, 6.0f) }), EvalContext());
	REQUIRE(ignored.error.empty());
	CHECK(ignored.pins.at("Terrain")[0].exclusions.empty());
}

TEST_CASE("Exclude items of other kinds are skipped with a warning", "[graph_section_exclude]")
{
	const NodeOutput out = ExecNode("LaySectionAlongLoop", SectionSettings(), SectionInputs("LaySectionAlongLoop", { StampPoints() }), EvalContext());
	REQUIRE(out.error.empty());
	CHECK(out.pins.at("Terrain")[0].exclusions.empty());
	CHECK(out.warnings.size() == 1);
}

TEST_CASE("an unconnected Exclude leaves the content hash unchanged", "[graph_section_exclude]")
{
	// Pinned from a run of the same inputs before the Exclude input existed (parent commit d0d5f51c).
	const NodeOutput loop = ExecNode("LaySectionAlongLoop", SectionSettings(), SectionInputs("LaySectionAlongLoop", {}), EvalContext());
	REQUIRE(loop.error.empty());
	CHECK(ContentHash(loop.pins.at("Terrain")) == 4635202377681792909ull);

	const NodeOutput stamp = ExecNode("StampSectionAtPoints", SectionSettings(), SectionInputs("StampSectionAtPoints", {}), EvalContext());
	REQUIRE(stamp.error.empty());
	CHECK(ContentHash(stamp.pins.at("Terrain")) == 6678002978610041825ull);

	// An Exclude that carries nothing hashes like no Exclude.
	const NodeOutput carriesNothing = ExecNode("LaySectionAlongLoop", SectionSettings(), SectionInputs("LaySectionAlongLoop", { ClearanceSpline(20.0f, 0.0f) }), EvalContext());
	CHECK(ContentHash(carriesNothing.pins.at("Terrain")) == 4635202377681792909ull);
}

TEST_CASE("moving an exclusion changes the content hash", "[graph_section_exclude]")
{
	const String type = "LaySectionAlongLoop";
	const auto hashWith = [&](const Collection& exclude, const float distance)
	{
		AttributeSet settings = SectionSettings();
		settings["distance"] = distance;
		const NodeOutput out = ExecNode(type, settings, SectionInputs(type, exclude), EvalContext());
		REQUIRE(out.error.empty());
		return ContentHash(out.pins.at("Terrain"));
	};

	const uint64 none = hashWith({}, 0.0f);
	const uint64 area = hashWith({ MakeSquare(5.0f, 5.0f, 10.0f) }, 0.0f);
	const uint64 moved = hashWith({ MakeSquare(6.0f, 5.0f, 10.0f) }, 0.0f);
	const uint64 spline = hashWith({ ClearanceSpline(20.0f, 6.0f) }, 0.0f);
	const uint64 wider = hashWith({ ClearanceSpline(20.0f, 6.0f) }, 1.0f);
	CHECK(area != none);
	CHECK(moved != area);
	CHECK(spline != none);
	CHECK(wider != spline);
	CHECK(hashWith({ MakeSquare(5.0f, 5.0f, 10.0f) }, 0.0f) == area);
}

TEST_CASE("CollectTerrainOps copies the exclusions", "[graph_section_exclude]")
{
	Graph graph;
	NodeInstance node;
	node.id = 3;
	node.type = "LaySectionAlongLoop";
	graph.nodes.push_back(node);

	DataItem item;
	item.kind = ItemKind::Terrain;
	item.op.type = TerrainOpType::SectionPath;
	ExclusionShape shape;
	shape.spline = SquareSpline(0.0f, 0.0f, 5.0f, 5.0f);
	shape.area = true;
	item.exclusions.push_back(shape);
	EvalResult result;
	result.nodes[3].output.pins["Terrain"].push_back(item);

	const std::vector<TerrainOpInput> ops = CollectTerrainOps(graph, result);
	REQUIRE(ops.size() == 1);
	REQUIRE(ops[0].exclusions.size() == 1);
	CHECK(ops[0].exclusions[0].area);
	CHECK(ops[0].exclusions[0].spline.points.size() == 4);
}

TEST_CASE("the terrain stage drops excluded section props", "[graph_section_exclude]")
{
	const std::vector<Placement> all = Placements({});
	REQUIRE(all.size() >= 8);

	// An area over part of the fence run, and a spline (radius 7) across it at z = 50.
	ExclusionShape area;
	area.spline = SquareSpline(-10.0f, -5.0f, 20.0f, 50.0f);
	area.area = true;
	ExclusionShape line;
	line.spline.points = { { -50.0f, 50.0f }, { 50.0f, 50.0f } };
	line.radius = 7.0f;

	const auto excluded = [](const Placement& p)
	{
		const bool inArea = p.position.x > -10.0f && p.position.x < 10.0f && p.position.z > -5.0f && p.position.z < 45.0f;
		const bool nearLine = std::abs(p.position.z - 50.0f) <= 7.0f;
		return inArea || nearLine;
	};
	std::vector<Placement> expected;
	for (const Placement& p : all)
	{
		if (!excluded(p))
		{
			expected.push_back(p);
		}
	}
	REQUIRE(expected.size() < all.size() - 1);
	REQUIRE_FALSE(expected.empty());

	const std::vector<Placement> kept = Placements({ area, line });
	REQUIRE(kept.size() == expected.size());
	for (size_t i = 0; i < kept.size(); ++i)
	{
		CHECK(kept[i].key == expected[i].key);
		CHECK(kept[i].position.x == expected[i].position.x);
		CHECK(kept[i].position.y == expected[i].position.y);
		CHECK(kept[i].position.z == expected[i].position.z);
		CHECK(kept[i].asset == expected[i].asset);
	}
}

TEST_CASE("exclusions do not cut the section's terrain height or paint", "[graph_section_exclude]")
{
	ExclusionShape area;
	area.spline = SquareSpline(-10.0f, -5.0f, 20.0f, 50.0f);
	area.area = true;

	Composed plain;
	Compose(plain, {});
	Composed cut;
	Compose(cut, { area });
	for (float z = -4.0f; z <= 44.0f; z += 3.0f)
	{
		for (float x = -8.0f; x <= 8.0f; x += 1.5f)
		{
			const TerrainTarget a = plain.terrain.Evaluate(x, z, 0.0f, {});
			const TerrainTarget b = cut.terrain.Evaluate(x, z, 0.0f, {});
			CHECK(a.height == b.height);
			CHECK(a.heightWeight == b.heightWeight);
			CHECK(a.splatWeight == b.splatWeight);
		}
	}
	CHECK(plain.terrain.Evaluate(0.0f, 20.0f, 0.0f, {}).heightWeight > 0.0f);
}

TEST_CASE("unusable exclusion shapes are skipped with a warning", "[graph_section_exclude]")
{
	ExclusionShape open;
	open.spline.points = { { -10.0f, 0.0f }, { 10.0f, 0.0f }, { 10.0f, 20.0f } };
	open.area = true;

	Composed c;
	Compose(c, { open });
	CHECK_FALSE(c.terrain.GetWarnings().empty());
	std::vector<Placement> out;
	c.terrain.BuildPlacements(Flat0, out);
	CHECK(out.size() == Placements({}).size());
}
