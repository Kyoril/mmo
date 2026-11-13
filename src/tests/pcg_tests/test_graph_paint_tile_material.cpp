// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "fake_terrain.h"
#include "graph_node_test_util.h"

#include "pcg/graph/graph_data.h"
#include "pcg/graph/terrain_compose.h"
#include "pcg/terrain_contribution.h"

#include <cmath>
#include <map>

using namespace mmo;
using namespace mmo::pcg;
using namespace mmo::pcg::graph;

namespace
{
	/// The 2x2 tiles of 8 m of FakeTerrain (world 0..16).
	GridLayout Layout()
	{
		return FakeTerrain().GetLayout();
	}

	const WorldRect kAllTiles{ 0.0f, 0.0f, 16.0f, 16.0f };

	PathSpline Loop(const std::vector<std::pair<float, float>>& xz)
	{
		PathSpline s;
		s.closed = true;
		for (const auto& [x, z] : xz)
		{
			SplinePoint p;
			p.x = x;
			p.z = z;
			s.points.push_back(p);
		}
		return s;
	}

	PathSpline Square(const float x0, const float z0, const float size)
	{
		return Loop({ { x0, z0 }, { x0 + size, z0 }, { x0 + size, z0 + size }, { x0, z0 + size } });
	}

	/// A diamond inside tile (0, 0) except its east corner, which reaches 1 m into tile (1, 0) (whose centre (12, 4) is
	/// far outside it).
	PathSpline Diamond()
	{
		return Loop({ { 9.0f, 4.0f }, { 6.0f, 7.0f }, { 3.0f, 4.0f }, { 6.0f, 1.0f } });
	}

	TerrainOpInput PaintOp(const PathSpline& spline, const uint32 order, const String& tileMaterial, const String& texture = "Soil.htex")
	{
		TerrainOpInput in;
		in.op.type = TerrainOpType::Paint;
		in.op.order = order;
		in.op.params["texture"] = texture;
		in.op.params["falloff"] = 0.5f;
		if (!spline.closed)
		{
			in.op.params["width"] = 2.0f;
		}
		if (!tileMaterial.empty())
		{
			in.op.params["tileMaterial"] = tileMaterial;
		}
		in.spline = spline;
		in.sourceId = 3;
		return in;
	}

	void PrepareOk(PreparedTerrain& t, const std::vector<TerrainOpInput>& ops)
	{
		String error;
		REQUIRE(t.Prepare(ops, [](float, float) { return 0.0f; }, {}, Square(-100.0f, -100.0f, 200.0f), 0.0f, error));
		CHECK(error.empty());
	}

	bool HasTile(const std::vector<TileMaterialRequest>& requests, const int32 x, const int32 z, const String& material)
	{
		for (const TileMaterialRequest& r : requests)
		{
			if (r.tileX == x && r.tileZ == z && r.material == material)
			{
				return true;
			}
		}
		return false;
	}

	bool Contains(const std::vector<String>& warnings, const String& text)
	{
		for (const String& w : warnings)
		{
			if (w.find(text) != String::npos)
			{
				return true;
			}
		}
		return false;
	}
}

TEST_CASE("a paint op with tileMaterial requests every tile it touches", "[graph_paint_tile_material]")
{
	// Review Focus 3: a tile is requested when the painted region touches it, not when its centre is painted.
	PreparedTerrain area;
	PrepareOk(area, { PaintOp(Diamond(), 4, "Farm.hmi") });
	area.ResolveTileMaterials(Layout(), kAllTiles);
	const std::vector<TileMaterialRequest>& requests = area.GetTileMaterials();
	REQUIRE(requests.size() == 2);
	CHECK(requests[0].tileX == 0);
	CHECK(requests[0].tileZ == 0);
	CHECK(requests[0].material == "Farm.hmi");
	CHECK(requests[1].tileX == 1);
	CHECK(requests[1].tileZ == 0);
	CHECK(requests[1].material == "Farm.hmi");

	// An open spline paints within its width, so it reaches a tile with its end cap: (6.5, 4) is 1.5 m from tile (1, 0).
	PreparedTerrain spline;
	PathSpline line;
	line.points.resize(2);
	line.points[0].x = 1.0f;
	line.points[0].z = 4.0f;
	line.points[1].x = 6.5f;
	line.points[1].z = 4.0f;
	PrepareOk(spline, { PaintOp(line, 4, "Farm.hmi") });
	spline.ResolveTileMaterials(Layout(), kAllTiles);
	CHECK(spline.GetTileMaterials().size() == 2);
	CHECK(HasTile(spline.GetTileMaterials(), 0, 0, "Farm.hmi"));
	CHECK(HasTile(spline.GetTileMaterials(), 1, 0, "Farm.hmi"));

	// Only tiles overlapping the given area are resolved.
	area.ResolveTileMaterials(Layout(), WorldRect{ 9.0f, 0.0f, 16.0f, 16.0f });
	REQUIRE(area.GetTileMaterials().size() == 1);
	CHECK(HasTile(area.GetTileMaterials(), 1, 0, "Farm.hmi"));

	// An area that merely touches a tile border does not request the tile behind it.
	PreparedTerrain touching;
	PrepareOk(touching, { PaintOp(Square(2.0f, 2.0f, 6.0f), 4, "Farm.hmi") });
	touching.ResolveTileMaterials(Layout(), kAllTiles);
	REQUIRE(touching.GetTileMaterials().size() == 1);
	CHECK(HasTile(touching.GetTileMaterials(), 0, 0, "Farm.hmi"));
}

TEST_CASE("the later stacked op wins a tile and the conflict is described", "[graph_paint_tile_material]")
{
	PreparedTerrain t;
	PrepareOk(t, { PaintOp(Square(1.0f, 1.0f, 3.0f), 5, "Plowed.hmi"), PaintOp(Square(5.0f, 5.0f, 2.0f), 9, "Sprouts.hmi") });
	t.ResolveTileMaterials(Layout(), kAllTiles);
	REQUIRE(t.GetTileMaterials().size() == 1);
	CHECK(HasTile(t.GetTileMaterials(), 0, 0, "Sprouts.hmi"));

	std::vector<String> warnings;
	t.DescribeNotes(TerrainEvaluateNotes{}, warnings);
	REQUIRE(warnings.size() == 1);
	CHECK(warnings[0] == "Tile (0, 0): 'Plowed.hmi' and 'Sprouts.hmi' both requested; 'Sprouts.hmi' wins");
	CHECK(Contains(warnings, "'Plowed.hmi' and 'Sprouts.hmi' both requested"));

	// The losing op paints nothing on that tile; the winner paints.
	const TextureLayerLookup anyLayer = [](float, float, const String&) { return static_cast<int8>(1); };
	TerrainEvaluateNotes notes;
	CHECK(t.Evaluate(2.5f, 2.5f, 0.0f, anyLayer, &notes, terrain_channel::Splat).splatWeight == 0.0f);
	CHECK(t.Evaluate(6.0f, 6.0f, 0.0f, anyLayer, &notes, terrain_channel::Splat).splatWeight > 0.0f);
	CHECK(notes.missingLayerOps.empty());
	CHECK_FALSE(t.IsTileMaterialPaint(2.5f, 2.5f));
	CHECK(t.IsTileMaterialPaint(6.0f, 6.0f));
	CHECK_FALSE(t.IsTileMaterialPaint(4.5f, 4.5f));

	// The same material twice is no conflict.
	PreparedTerrain same;
	PrepareOk(same, { PaintOp(Square(1.0f, 1.0f, 3.0f), 5, "Plowed.hmi"), PaintOp(Square(5.0f, 5.0f, 2.0f), 9, "Plowed.hmi") });
	same.ResolveTileMaterials(Layout(), kAllTiles);
	std::vector<String> none;
	same.DescribeNotes(TerrainEvaluateNotes{}, none);
	CHECK(none.empty());
}

TEST_CASE("Evaluate reports the tile material in the material channel only", "[graph_paint_tile_material]")
{
	PreparedTerrain t;
	PrepareOk(t, { PaintOp(Diamond(), 4, "Farm.hmi") });
	t.ResolveTileMaterials(Layout(), kAllTiles);

	// Tile centres, as the material pass of Apply samples them; (12, 4) lies outside the diamond.
	CHECK(t.Evaluate(4.0f, 4.0f, 0.0f, {}, nullptr, terrain_channel::Material).material == "Farm.hmi");
	CHECK(t.Evaluate(12.0f, 4.0f, 0.0f, {}, nullptr, terrain_channel::Material).material == "Farm.hmi");
	CHECK(t.Evaluate(4.0f, 12.0f, 0.0f, {}, nullptr, terrain_channel::Material).material.empty());
	CHECK(t.Evaluate(12.0f, 4.0f, 0.0f, {}, nullptr, terrain_channel::All).material == "Farm.hmi");
	CHECK_FALSE(t.Evaluate(4.0f, 4.0f, 0.0f, {}, nullptr, terrain_channel::Material).setMaterial);
	for (const terrain_channel::Type channel : { terrain_channel::Heights, terrain_channel::Splat, terrain_channel::Water })
	{
		CHECK(t.Evaluate(4.0f, 4.0f, 0.0f, {}, nullptr, channel).material.empty());
	}

	// Apply switches exactly the requested tiles.
	FakeTerrain terrain;
	TerrainContribution contribution;
	const ChannelTerrainTargetFunction target = [&t](const float x, const float z, const float base, const terrain_channel::Type channels)
	{
		return t.Evaluate(x, z, base, {}, nullptr, channels);
	};
	Apply(terrain, kAllTiles, target, String(), contribution);
	CHECK(terrain.GetTileMaterial(0, 0) == "Farm.hmi");
	CHECK(terrain.GetTileMaterial(1, 0) == "Farm.hmi");
	CHECK(terrain.GetTileMaterial(0, 1) == "Base");
	CHECK(terrain.GetTileMaterial(1, 1) == "Base");
	CHECK(contribution.materials.size() == 2);
	Unapply(terrain, contribution);
	CHECK(terrain.GetTileMaterial(0, 0) == "Base");
	CHECK(terrain.GetTileMaterial(1, 0) == "Base");
}

TEST_CASE("a paint op without tileMaterial requests nothing and hashes as before", "[graph_paint_tile_material]")
{
	std::map<String, Collection> inputs;
	inputs["In"].push_back(MakeSquare(2.0f, 2.0f, 4.0f));
	inputs["In"].push_back(MakeSplineItem({ { 1.0f, 4.0f }, { 6.5f, 4.0f } }, false));

	const NodeTypeDef* def = NodeRegistry::Default().Find("PaintSplat");
	REQUIRE(def != nullptr);
	size_t found = 0;
	for (const SettingDef& s : def->settings)
	{
		if (s.name == "tileMaterial")
		{
			++found;
			CHECK(std::get<String>(s.defaultValue).empty());
		}
	}
	CHECK(found == 1);

	// Pinned from a run of the same inputs before the setting existed (parent commit 6f84ea91).
	const NodeOutput out = ExecNode("PaintSplat", { { "texture", String("Soil.htex") } }, inputs, EvalContext());
	REQUIRE(out.error.empty());
	CHECK(out.warnings.empty());
	CHECK(ContentHash(out.pins.at("Terrain")) == 11912809531636052319ull);
	for (const DataItem& item : out.pins.at("Terrain"))
	{
		CHECK(item.op.params.count("tileMaterial") == 0);
	}

	// The setting, and an item attribute overriding it.
	const NodeOutput set = ExecNode("PaintSplat", { { "texture", String("Soil.htex") }, { "tileMaterial", String("Farm.hmi") } }, inputs, EvalContext());
	REQUIRE(set.error.empty());
	CHECK(std::get<String>(set.pins.at("Terrain")[0].op.params.at("tileMaterial")) == "Farm.hmi");
	CHECK(ContentHash(set.pins.at("Terrain")) != ContentHash(out.pins.at("Terrain")));
	std::map<String, Collection> overridden = inputs;
	overridden["In"][0].attributes["tileMaterial"] = String("Sprouts.hmi");
	const NodeOutput attribute = ExecNode("PaintSplat", { { "texture", String("Soil.htex") }, { "tileMaterial", String("Farm.hmi") } }, overridden, EvalContext());
	CHECK(std::get<String>(attribute.pins.at("Terrain")[0].op.params.at("tileMaterial")) == "Sprouts.hmi");
	CHECK(std::get<String>(attribute.pins.at("Terrain")[1].op.params.at("tileMaterial")) == "Farm.hmi");

	// Prepared without it, nothing is requested and no tile material is reported.
	PreparedTerrain t;
	PrepareOk(t, { PaintOp(Diamond(), 4, String()) });
	t.ResolveTileMaterials(Layout(), kAllTiles);
	CHECK(t.GetTileMaterials().empty());
	CHECK(t.Evaluate(4.0f, 4.0f, 0.0f, {}, nullptr, terrain_channel::Material).material.empty());
	CHECK_FALSE(t.IsTileMaterialPaint(6.0f, 4.0f));
}

TEST_CASE("the texture lookup is asked with the op's texture on switched tiles", "[graph_paint_tile_material]")
{
	PreparedTerrain t;
	PrepareOk(t, { PaintOp(Diamond(), 4, "Farm.hmi") });
	t.ResolveTileMaterials(Layout(), kAllTiles);

	// Layer 2 carries the soil on the tiles switched to Farm; no other tile has it.
	const std::vector<TileMaterialRequest> requests = t.GetTileMaterials();
	const TextureLayerLookup lookup = [&requests](const float x, const float z, const String& key)
	{
		const int32 tileX = static_cast<int32>(std::floor(x / 8.0f));
		const int32 tileZ = static_cast<int32>(std::floor(z / 8.0f));
		return HasTile(requests, tileX, tileZ, "Farm.hmi") && key == "Soil.htex" ? static_cast<int8>(2) : static_cast<int8>(-1);
	};

	TerrainEvaluateNotes notes;
	for (const auto& [x, z] : { std::make_pair(6.0f, 4.0f), std::make_pair(4.0f, 4.0f), std::make_pair(8.5f, 4.0f) })
	{
		const TerrainTarget target = t.Evaluate(x, z, 0.0f, lookup, &notes, terrain_channel::Splat);
		CHECK(target.splatWeight > 0.0f);
		CHECK(target.splat[2] == 255);
		CHECK(t.IsTileMaterialPaint(x, z));
	}
	CHECK(notes.missingLayerOps.empty());
	CHECK(t.Evaluate(1.0f, 1.0f, 0.0f, lookup, &notes, terrain_channel::Splat).splatWeight == 0.0f);
	CHECK_FALSE(t.IsTileMaterialPaint(1.0f, 1.0f));
	CHECK_FALSE(t.IsTileMaterialPaint(12.0f, 4.0f));
}

TEST_CASE("a paint area reaching into an edge tile switches it", "[graph_paint_tile_material]")
{
	// The rect's tile range comes from TileRangeOverlapping, as the material pass of Apply samples it: tile (1, 0)
	// overlaps the rect, its centre (12, 4) lies outside it.
	const WorldRect rect{ 1.0f, 1.0f, 11.0f, 7.5f };
	PreparedTerrain t;
	PrepareOk(t, { PaintOp(Diamond(), 4, "Farm.hmi") });
	t.ResolveTileMaterials(Layout(), rect);
	REQUIRE(t.GetTileMaterials().size() == 2);
	CHECK(HasTile(t.GetTileMaterials(), 1, 0, "Farm.hmi"));

	FakeTerrain terrain;
	TerrainContribution contribution;
	const ChannelTerrainTargetFunction target = [&t](const float x, const float z, const float base, const terrain_channel::Type channels)
	{
		return t.Evaluate(x, z, base, {}, nullptr, channels);
	};
	Apply(terrain, rect, target, String(), contribution);
	CHECK(terrain.GetTileMaterial(0, 0) == "Farm.hmi");
	CHECK(terrain.GetTileMaterial(1, 0) == "Farm.hmi");
	CHECK(terrain.GetTileMaterial(1, 1) == "Base");
	Unapply(terrain, contribution);
	CHECK(terrain.GetTileMaterial(1, 0) == "Base");
}

TEST_CASE("a discarded tile material switches no tile", "[graph_paint_tile_material]")
{
	PreparedTerrain t;
	PrepareOk(t, { PaintOp(Square(1.0f, 1.0f, 3.0f), 5, "Plowed.hmi"), PaintOp(Diamond(), 9, "Missing.hmi") });
	t.ResolveTileMaterials(Layout(), kAllTiles);
	REQUIRE(t.GetTileMaterials().size() == 2);
	t.DiscardTileMaterial("Missing.hmi");
	CHECK(t.GetTileMaterials().empty());
	CHECK(t.Evaluate(4.0f, 4.0f, 0.0f, {}, nullptr, terrain_channel::Material).material.empty());
	t.DiscardTileMaterial("Unknown.hmi");
	CHECK(t.GetTileMaterials().empty());
}
