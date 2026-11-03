// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/graph/evaluator.h"
#include "pcg/graph/node_registry.h"
#include "pcg/graph/terrain_compose.h"
#include "pcg/section_path.h"
#include "terrain_io/water_flow.h"

#include <chrono>
#include <cmath>
#include <map>

using namespace mmo;
using namespace mmo::pcg;
using namespace mmo::pcg::graph;

namespace
{
	/// TestRoad-like section: L 20, halfWidth 3, sideFalloff 4, bed 0.5 deep for |x| <= 2 painted layer 0 = 55 /
	/// layer 2 = 200, and one baked prop at local (4, 10).
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
		prop.placement.asset = "Props/Lamp.hmsh";
		prop.placement.position = Vector3(4.0f, 0.0f, 10.0f);
		v.placements.push_back(prop);
		asset.variants.push_back(v);
		return asset;
	}

	PathSpline Square(const float x0, const float z0, const float size)
	{
		PathSpline s;
		s.closed = true;
		SplinePoint a; a.x = x0; a.z = z0;
		SplinePoint b; b.x = x0 + size; b.z = z0;
		SplinePoint c; c.x = x0 + size; c.z = z0 + size;
		SplinePoint d; d.x = x0; d.z = z0 + size;
		s.points = { a, b, c, d };
		return s;
	}

	PathSpline Line(const float x0, const float z0, const float x1, const float z1, const float y = 0.0f)
	{
		PathSpline s;
		SplinePoint a; a.x = x0; a.z = z0; a.y = y;
		SplinePoint b; b.x = x1; b.z = z1; b.y = y;
		s.points = { a, b };
		return s;
	}

	TerrainOpInput MakeOp(const TerrainOpType type, const PathSpline& spline, const uint32 order, const AttributeSet& params = {})
	{
		TerrainOpInput in;
		in.op.type = type;
		in.op.order = order;
		in.op.params = params;
		in.spline = spline;
		for (size_t i = 0; i < spline.points.size(); ++i)
		{
			in.pointIds.push_back(100 + i);
		}
		in.sourceId = 9;
		return in;
	}

	float Flat0(float, float)
	{
		return 0.0f;
	}

	SectionAssetProvider Provider(const std::map<String, SectionAsset>& assets)
	{
		return [&assets](const String& path, String& error) -> const SectionAsset*
		{
			const auto it = assets.find(path);
			if (it == assets.end())
			{
				error = "not found";
				return nullptr;
			}
			return &it->second;
		};
	}

	void PrepareOk(PreparedTerrain& t, const std::vector<TerrainOpInput>& ops, const PathSpline& region, const float borderFade,
		const SectionAssetProvider& sections = {})
	{
		String error;
		REQUIRE(t.Prepare(ops, Flat0, sections, region, borderFade, error));
		CHECK(error.empty());
	}

	/// Height written by Apply for this target at this base.
	float Written(const TerrainTarget& t, const float base)
	{
		return t.heightWeight > 0.0f ? base + (t.height - base) * std::min(1.0f, t.heightWeight) : base;
	}

	float Smooth(const float t)
	{
		return t * t * (3.0f - 2.0f * t);
	}

	const PathSpline kBigRegion = Square(-500.0f, -500.0f, 1000.0f);
}

TEST_CASE("No ops: empty, nothing written", "[graph_terrain_compose]")
{
	PreparedTerrain t;
	PrepareOk(t, {}, Square(0.0f, 0.0f, 100.0f), 10.0f);
	CHECK(t.IsEmpty());
	const WorldRect r = t.GetInfluence();
	CHECK(r.minX == 0.0f);
	CHECK(r.maxX == 0.0f);
	for (float x = -10.0f; x <= 110.0f; x += 7.5f)
	{
		const TerrainTarget target = t.Evaluate(x, 50.0f, 3.0f, {});
		CHECK(target.heightWeight == 0.0f);
		CHECK(target.splatWeight == 0.0f);
		CHECK(target.waterType == 0);
	}
}

TEST_CASE("Noise is bounded by amplitude, zero outside, deterministic and world-anchored", "[graph_terrain_compose]")
{
	const AttributeSet params{ { "amplitude", 2.0f }, { "wavelength", 24.0f }, { "falloff", 8.0f } };
	PreparedTerrain a;
	PrepareOk(a, { MakeOp(TerrainOpType::Noise, Square(0.0f, 0.0f, 100.0f), 3, params) }, kBigRegion, 10.0f);
	PreparedTerrain b;
	PrepareOk(b, { MakeOp(TerrainOpType::Noise, Square(20.0f, 20.0f, 100.0f), 3, params) }, kBigRegion, 10.0f);

	float largest = 0.0f;
	for (float z = -20.0f; z <= 120.0f; z += 3.3f)
	{
		for (float x = -20.0f; x <= 120.0f; x += 3.3f)
		{
			const TerrainTarget t = a.Evaluate(x, z, 5.0f, {});
			const bool inside = x > 0.0f && x < 100.0f && z > 0.0f && z < 100.0f;
			const float delta = Written(t, 5.0f) - 5.0f;
			CHECK(std::abs(delta) <= 2.0f + 1e-4f);
			largest = std::max(largest, std::abs(delta));
			if (!inside)
			{
				CHECK(delta == 0.0f);
				CHECK(t.heightWeight == 0.0f);
			}

			const TerrainTarget again = a.Evaluate(x, z, 5.0f, {});
			CHECK(again.height == t.height);
			CHECK(again.heightWeight == t.heightWeight);

			// Deep inside both areas the translated area gives the same world-anchored value.
			if (x >= 30.0f && x <= 90.0f && z >= 30.0f && z <= 90.0f)
			{
				CHECK(b.Evaluate(x, z, 5.0f, {}).height == t.height);
			}
		}
	}
	CHECK(largest > 0.2f);
}

TEST_CASE("Ops stack relative to the base height", "[graph_terrain_compose]")
{
	PreparedTerrain t;
	PrepareOk(t, { MakeOp(TerrainOpType::Noise, Square(0.0f, 0.0f, 100.0f), 3, { { "amplitude", 2.0f } }) }, kBigRegion, 10.0f);
	for (float x = 20.0f; x <= 80.0f; x += 6.1f)
	{
		const float low = Written(t.Evaluate(x, 47.0f, 1.0f, {}), 1.0f);
		const float high = Written(t.Evaluate(x, 47.0f, 6.0f, {}), 6.0f);
		CHECK(high - low == Approx(5.0f).margin(1e-4f));
	}
}

TEST_CASE("Raise and Carve stack to the hand-computed height", "[graph_terrain_compose]")
{
	const PathSpline line = Line(-50.0f, 0.0f, 150.0f, 0.0f, 10.0f);
	const TerrainOpInput raise = MakeOp(TerrainOpType::Raise, line, 2, { { "width", 40.0f }, { "falloffPower", 2.0f } });
	const TerrainOpInput carve = MakeOp(TerrainOpType::Carve, line, 5, { { "width", 4.0f }, { "depth", 1.5f }, { "bank", 3.0f } });

	PreparedTerrain t;
	PrepareOk(t, { raise, carve }, kBigRegion, 10.0f);

	// Centre: peak 10 (spline 10 over flat 0), bed depth 1.5.
	CHECK(Written(t.Evaluate(50.0f, 0.0f, 0.0f, {}), 0.0f) == Approx(10.0f - 1.5f).margin(1e-4f));

	// 5 m across (in the bank: flat bed 4 m per side, bank 3 m): raise 10 x smoothstep(1 - 5/40)^2,
	// carve 1.5 x (1 - smoothstep((5 - 4) / 3)) = 1.5 x (1 - 7/27).
	const float r = Smooth(1.0f - 5.0f / 40.0f);
	const float expected = 10.0f * r * r - 1.5f * (1.0f - 7.0f / 27.0f);
	CHECK(Written(t.Evaluate(50.0f, 5.0f, 0.0f, {}), 0.0f) == Approx(expected).margin(1e-4f));
	// 3 m across is still on the flat bed.
	const float r3 = Smooth(1.0f - 3.0f / 40.0f);
	CHECK(Written(t.Evaluate(50.0f, 3.0f, 0.0f, {}), 0.0f) == Approx(10.0f * r3 * r3 - 1.5f).margin(1e-4f));

	// Both are relative, so they commute.
	PreparedTerrain reversed;
	PrepareOk(reversed, { carve, raise }, kBigRegion, 10.0f);
	CHECK(Written(reversed.Evaluate(50.0f, 5.0f, 0.0f, {}), 0.0f) == Approx(expected).margin(1e-4f));

	// Raise uses the base under the spline from Prepare: over a base of 4 the peak is 6.
	PreparedTerrain raised;
	String error;
	REQUIRE(raised.Prepare({ raise }, [](float, float) { return 4.0f; }, {}, kBigRegion, 10.0f, error));
	CHECK(Written(raised.Evaluate(50.0f, 0.0f, 4.0f, {}), 4.0f) == Approx(10.0f).margin(1e-4f));
}

TEST_CASE("Op order matters against an absolute section target", "[graph_terrain_compose]")
{
	const std::map<String, SectionAsset> assets{ { "Sections/TestRoad.hsec", MakeRoad() } };
	const PathSpline line = Line(0.0f, -100.0f, 0.0f, 100.0f, 10.0f);
	TerrainOpInput section = MakeOp(TerrainOpType::SectionPath, line, 1);
	section.op.asset = "Sections/TestRoad.hsec";
	const TerrainOpInput carve = MakeOp(TerrainOpType::Carve, line, 2, { { "width", 4.0f }, { "depth", 1.5f }, { "bank", 3.0f } });

	PreparedTerrain sectionFirst;
	PrepareOk(sectionFirst, { section, carve }, kBigRegion, 10.0f, Provider(assets));
	PreparedTerrain carveFirst;
	PrepareOk(carveFirst, { carve, section }, kBigRegion, 10.0f, Provider(assets));

	// Section: centre 10 + bed -0.5 = 9.5 at weight 1; the carve then lowers it by 1.5, or is overwritten by it.
	CHECK(Written(sectionFirst.Evaluate(0.0f, 30.0f, 0.0f, {}), 0.0f) == Approx(8.0f).margin(1e-3f));
	CHECK(Written(carveFirst.Evaluate(0.0f, 30.0f, 0.0f, {}), 0.0f) == Approx(9.5f).margin(1e-3f));
}

TEST_CASE("The region border fades writes smoothly to zero", "[graph_terrain_compose]")
{
	const PathSpline region = Square(0.0f, 0.0f, 100.0f);
	PreparedTerrain t;
	PrepareOk(t, { MakeOp(TerrainOpType::Carve, Line(-50.0f, 50.0f, 150.0f, 50.0f), 1, { { "width", 4.0f }, { "depth", 1.5f } }) }, region, 10.0f);

	CHECK(Written(t.Evaluate(50.0f, 50.0f, 0.0f, {}), 0.0f) == Approx(-1.5f).margin(1e-4f));
	CHECK(Written(t.Evaluate(5.0f, 50.0f, 0.0f, {}), 0.0f) / -1.5f == Approx(0.5f).margin(0.05f));
	const TerrainTarget outside = t.Evaluate(-1.0f, 50.0f, 0.0f, {});
	CHECK(outside.heightWeight == 0.0f);
	const WorldRect influence = t.GetInfluence();
	CHECK(influence.minX >= 0.0f);
	CHECK(influence.maxX <= 100.0f);

	const float amplitude = 2.0f;
	PreparedTerrain noise;
	PrepareOk(noise, { MakeOp(TerrainOpType::Noise, Square(-50.0f, -50.0f, 200.0f), 1,
		{ { "amplitude", amplitude }, { "wavelength", 200.0f } }) }, region, 10.0f);
	float previous = Written(noise.Evaluate(-5.0f, 50.0f, 0.0f, {}), 0.0f);
	float largestStep = 0.0f;
	float largestValue = 0.0f;
	for (float x = -4.5f; x <= 15.0f; x += 0.5f)
	{
		const float h = Written(noise.Evaluate(x, 50.0f, 0.0f, {}), 0.0f);
		largestStep = std::max(largestStep, std::abs(h - previous));
		largestValue = std::max(largestValue, std::abs(h));
		previous = h;
	}
	CHECK(largestStep < amplitude * 0.15f);
	CHECK(largestValue > 0.01f);
}

TEST_CASE("A SectionPath op matches EvaluateSectionTerrain on the same shape", "[graph_terrain_compose]")
{
	const std::map<String, SectionAsset> assets{ { "Sections/TestRoad.hsec", MakeRoad() } };
	const PathSpline line = Line(0.0f, -100.0f, 0.0f, 100.0f, 10.0f);
	TerrainOpInput op = MakeOp(TerrainOpType::SectionPath, line, 4, { { "joinBlend", 1.0f } });
	op.op.asset = "Sections/TestRoad.hsec";

	PreparedTerrain t;
	PrepareOk(t, { op }, kBigRegion, 10.0f, Provider(assets));

	SectionPathParams params;
	params.joinBlend = 1.0f;
	SectionPathShape shape;
	String error;
	REQUIRE(PrepareSectionPath(line, op.pointIds, assets.at("Sections/TestRoad.hsec"), params, Flat0, shape, error));

	for (float x = -8.0f; x <= 8.0f; x += 0.7f)
	{
		const TerrainTarget direct = EvaluateSectionTerrain(shape, x, 13.0f, 2.0f, {});
		const TerrainTarget composed = t.Evaluate(x, 13.0f, 2.0f, {});
		CHECK(Written(composed, 2.0f) == Approx(Written(direct, 2.0f)).margin(1e-4f));
		CHECK(composed.splatWeight == Approx(direct.splatWeight).margin(1e-4f));
		if (direct.splatWeight > 0.0f)
		{
			for (size_t i = 0; i < 4; ++i)
			{
				CHECK(std::abs(static_cast<int>(composed.splat[i]) - static_cast<int>(direct.splat[i])) <= 1);
			}
		}
	}
}

TEST_CASE("Section placement keys are stable when an unrelated op is added", "[graph_terrain_compose]")
{
	const std::map<String, SectionAsset> assets{ { "Sections/TestRoad.hsec", MakeRoad() } };
	TerrainOpInput op = MakeOp(TerrainOpType::SectionPath, Line(0.0f, 0.0f, 0.0f, 80.0f, 0.0f), 4);
	op.op.asset = "Sections/TestRoad.hsec";

	PreparedTerrain one;
	PrepareOk(one, { op }, kBigRegion, 10.0f, Provider(assets));
	std::vector<Placement> first;
	one.BuildPlacements(Flat0, first);
	REQUIRE(first.size() == 4);

	PreparedTerrain two;
	PrepareOk(two, { MakeOp(TerrainOpType::Noise, Square(-50.0f, -50.0f, 30.0f), 1), op }, kBigRegion, 10.0f, Provider(assets));
	std::vector<Placement> second;
	two.BuildPlacements(Flat0, second);
	REQUIRE(second.size() == first.size());
	for (size_t i = 0; i < first.size(); ++i)
	{
		CHECK(second[i].key == first[i].key);
		CHECK(second[i].position.x == first[i].position.x);
		CHECK(second[i].position.z == first[i].position.z);
	}

	// Clipped to the region: a region covering only the first 40 m keeps two of the four copies' props.
	PreparedTerrain clipped;
	PrepareOk(clipped, { op }, Square(-10.0f, -10.0f, 50.0f), 10.0f, Provider(assets));
	std::vector<Placement> inside;
	clipped.BuildPlacements(Flat0, inside);
	CHECK(inside.size() == 2);

	// The prop at z 30 lies 4 m inside the border of a region ending at z 34: fade smoothstep(0.4) < 0.5, dropped.
	PreparedTerrain faded;
	PrepareOk(faded, { op }, Square(-10.0f, -10.0f, 44.0f), 10.0f, Provider(assets));
	std::vector<Placement> kept;
	faded.BuildPlacements(Flat0, kept);
	REQUIRE(kept.size() == 1);
	CHECK(kept[0].position.z == Approx(10.0f).margin(0.01f));
}

TEST_CASE("Generated section items of one node keep all their props", "[graph_terrain_compose]")
{
	const std::map<String, SectionAsset> assets{ { "Sections/TestRoad.hsec", MakeRoad() } };
	TerrainOpInput a = MakeOp(TerrainOpType::SectionPath, Line(0.0f, 0.0f, 0.0f, 80.0f), 4);
	a.op.asset = "Sections/TestRoad.hsec";
	a.pointIds.clear();
	a.sourceId = 0;
	TerrainOpInput b = a;
	b.spline = Line(100.0f, 0.0f, 100.0f, 80.0f);
	b.itemIndex = 1;

	PreparedTerrain t;
	PrepareOk(t, { a, b }, kBigRegion, 10.0f, Provider(assets));
	std::vector<Placement> placements;
	std::vector<String> warnings;
	t.BuildPlacements(Flat0, placements, &warnings);
	CHECK(placements.size() == 8);
	CHECK(warnings.empty());
}

TEST_CASE("Section paint does not depend on the base height", "[graph_terrain_compose]")
{
	const std::map<String, SectionAsset> assets{ { "Sections/TestRoad.hsec", MakeRoad() } };
	for (const bool mass : { false, true })
	{
		TerrainOpInput op = MakeOp(TerrainOpType::SectionPath, Line(0.0f, -100.0f, 0.0f, 100.0f, 10.0f), 4,
			{ { "massEnabled", mass } });
		op.op.asset = "Sections/TestRoad.hsec";
		PreparedTerrain t;
		PrepareOk(t, { op }, kBigRegion, 10.0f, Provider(assets));
		for (float x = -8.0f; x <= 70.0f; x += 1.3f)
		{
			const TerrainTarget low = t.Evaluate(x, 13.0f, 0.0f, {});
			const TerrainTarget high = t.Evaluate(x, 13.0f, 50.0f, {});
			CHECK(low.splatWeight == high.splatWeight);
			CHECK(low.splat == high.splat);
		}
	}
}

TEST_CASE("Noise is seeded from the seed param, else from the op order", "[graph_terrain_compose]")
{
	const PathSpline area = Square(0.0f, 0.0f, 100.0f);
	PreparedTerrain a;
	PrepareOk(a, { MakeOp(TerrainOpType::Noise, area, 3, { { "seed", int64(77) } }) }, kBigRegion, 10.0f);
	PreparedTerrain b;
	PrepareOk(b, { MakeOp(TerrainOpType::Noise, area, 9, { { "seed", 77.0f } }) }, kBigRegion, 10.0f);
	PreparedTerrain c;
	PrepareOk(c, { MakeOp(TerrainOpType::Noise, area, 3, { { "seed", int64(78) } }) }, kBigRegion, 10.0f);
	PreparedTerrain byOrder;
	PrepareOk(byOrder, { MakeOp(TerrainOpType::Noise, area, 3) }, kBigRegion, 10.0f);
	PreparedTerrain otherOrder;
	PrepareOk(otherOrder, { MakeOp(TerrainOpType::Noise, area, 4) }, kBigRegion, 10.0f);

	int differentSeed = 0;
	int differentOrder = 0;
	for (float x = 20.0f; x <= 80.0f; x += 4.7f)
	{
		const float h = a.Evaluate(x, 50.0f, 0.0f, {}).height;
		CHECK(b.Evaluate(x, 50.0f, 0.0f, {}).height == h);
		differentSeed += c.Evaluate(x, 50.0f, 0.0f, {}).height != h ? 1 : 0;
		differentOrder += byOrder.Evaluate(x, 50.0f, 0.0f, {}).height != otherOrder.Evaluate(x, 50.0f, 0.0f, {}).height ? 1 : 0;
	}
	CHECK(differentSeed > 5);
	CHECK(differentOrder > 5);
}

TEST_CASE("A missing section asset or an unknown section param fails Prepare", "[graph_terrain_compose]")
{
	const std::map<String, SectionAsset> assets{ { "Sections/TestRoad.hsec", MakeRoad() } };
	TerrainOpInput op = MakeOp(TerrainOpType::SectionPath, Line(0.0f, 0.0f, 0.0f, 80.0f), 6);
	op.op.asset = "Sections/Missing.hsec";

	PreparedTerrain t;
	String error;
	CHECK_FALSE(t.Prepare({ op }, Flat0, Provider(assets), kBigRegion, 10.0f, error));
	CHECK(error.find("Sections/Missing.hsec") != String::npos);
	CHECK(error.find("node 6") != String::npos);
	CHECK(t.IsEmpty());

	op.op.asset = "Sections/TestRoad.hsec";
	op.op.params["bogusParam"] = 1.0f;
	error.clear();
	CHECK_FALSE(t.Prepare({ op }, Flat0, Provider(assets), kBigRegion, 10.0f, error));
	CHECK(error.find("bogusParam") != String::npos);

	// A known param is accepted.
	op.op.params.clear();
	op.op.params["mirrorCopies"] = true;
	op.op.params["heightMode"] = String("followGround");
	error.clear();
	CHECK(t.Prepare({ op }, Flat0, Provider(assets), kBigRegion, 10.0f, error));
	CHECK(error.empty());
}

TEST_CASE("Section paint layers without a tile layer give one warning per op", "[graph_terrain_compose]")
{
	// The road paints section layers 0 and 2; layer 3 names a texture but is never painted.
	SectionAsset road = MakeRoad();
	road.layerTextures = { "Textures/Dirt.htex", "", "Textures/Gravel.htex", "Textures/Unused.htex" };
	const std::map<String, SectionAsset> assets{ { "Sections/TestRoad.hsec", road } };
	TerrainOpInput op = MakeOp(TerrainOpType::SectionPath, Line(0.0f, -100.0f, 0.0f, 100.0f), 7);
	op.op.asset = "Sections/TestRoad.hsec";
	PreparedTerrain t;
	PrepareOk(t, { op }, kBigRegion, 10.0f, Provider(assets));

	// Dirt is tile layer 1 everywhere, Gravel and Unused are no tile layer.
	const TextureLayerLookup dirtOnly = [](float, float, const String& key) -> int8 { return key == "Textures/Dirt.htex" ? 1 : -1; };
	TerrainEvaluateNotes notes;
	for (float z = -50.0f; z <= 50.0f; z += 7.0f)
	{
		const TerrainTarget target = t.Evaluate(0.0f, z, 0.0f, dirtOnly, &notes);
		CHECK(target.splatWeight > 0.0f);
	}
	// Off the painted bed nothing is noted.
	TerrainEvaluateNotes offBed;
	t.Evaluate(50.0f, 0.0f, 0.0f, dirtOnly, &offBed);
	CHECK(offBed.missingSectionLayers.empty());

	REQUIRE(notes.missingSectionLayers.size() == 1);
	CHECK(notes.missingSectionLayers.begin()->second == 2);
	std::vector<String> warnings;
	t.DescribeNotes(notes, warnings);
	REQUIRE(warnings.size() == 1);
	CHECK(warnings[0].find("Sections/TestRoad.hsec") != String::npos);
	CHECK(warnings[0].find("node 7") != String::npos);
	CHECK(warnings[0].find("Textures/Gravel.htex") != String::npos);
	CHECK(warnings[0].find("Textures/Unused.htex") == String::npos);
	CHECK(warnings[0].find("Textures/Dirt.htex") == String::npos);

	// Every painted layer matched: no notes.
	const TextureLayerLookup all = [](float, float, const String& key) -> int8 { return key == "Textures/Dirt.htex" ? 1 : 2; };
	TerrainEvaluateNotes none;
	for (float z = -50.0f; z <= 50.0f; z += 7.0f)
	{
		t.Evaluate(0.0f, z, 0.0f, all, &none);
	}
	CHECK(none.missingSectionLayers.empty());
}

TEST_CASE("Paint uses the looked-up layer and skips with one warning where there is none", "[graph_terrain_compose]")
{
	const TerrainOpInput op = MakeOp(TerrainOpType::Paint, Square(0.0f, 0.0f, 50.0f), 1,
		{ { "texture", String("Textures/Sand.htex") }, { "weight", 0.8f }, { "falloff", 2.0f } });

	PreparedTerrain t;
	PrepareOk(t, { op }, kBigRegion, 10.0f);
	const TextureLayerLookup layer2 = [](float, float, const String& key) -> int8 { return key == "Textures/Sand.htex" ? 2 : -1; };
	const TerrainTarget inside = t.Evaluate(25.0f, 25.0f, 0.0f, layer2);
	CHECK(inside.splatWeight == Approx(0.8f));
	CHECK(inside.splat[2] == 255);
	CHECK(inside.heightWeight == 0.0f);
	CHECK(t.Evaluate(1.0f, 25.0f, 0.0f, layer2).splatWeight == Approx(0.8f * Smooth(0.5f)).margin(1e-4f));
	CHECK(t.Evaluate(60.0f, 25.0f, 0.0f, layer2).splatWeight == 0.0f);
	TerrainEvaluateNotes notes;
	t.Evaluate(25.0f, 25.0f, 0.0f, layer2, &notes);
	CHECK(notes.missingLayerOps.empty());
	CHECK(t.GetWarnings().empty());

	const TextureLayerLookup none = [](float, float, const String&) -> int8 { return -1; };
	for (float x = 5.0f; x < 45.0f; x += 5.0f)
	{
		const TerrainTarget target = t.Evaluate(x, 25.0f, 0.0f, none, &notes);
		CHECK(target.splatWeight == 0.0f);
	}
	std::vector<String> warnings;
	t.DescribeNotes(notes, warnings);
	REQUIRE(warnings.size() == 1);
	CHECK(warnings[0].find("Textures/Sand.htex") != String::npos);
	CHECK(t.GetWarnings().empty());

	// Along an open spline: painted within width.
	PreparedTerrain road;
	PrepareOk(road, { MakeOp(TerrainOpType::Paint, Line(0.0f, 0.0f, 100.0f, 0.0f), 1,
		{ { "texture", String("Textures/Sand.htex") }, { "width", 4.0f }, { "falloff", 2.0f } }) }, kBigRegion, 10.0f);
	CHECK(road.Evaluate(50.0f, 1.0f, 0.0f, layer2).splatWeight == Approx(1.0f));
	CHECK(road.Evaluate(50.0f, 5.0f, 0.0f, layer2).splatWeight == 0.0f);
}

TEST_CASE("Water fills the area up to the margin past the shore", "[graph_terrain_compose]")
{
	TerrainOpInput op = MakeOp(TerrainOpType::WaterArea, Square(0.0f, 0.0f, 50.0f), 1,
		{ { "level", 3.0f }, { "levelOffset", 0.5f }, { "margin", 4.0f } });
	op.op.asset = "Materials/Water.hmat";

	PreparedTerrain t;
	PrepareOk(t, { op }, kBigRegion, 10.0f);
	CHECK(t.GetWaterMaterial() == "Materials/Water.hmat");

	const TerrainTarget inside = t.Evaluate(25.0f, 25.0f, 0.0f, {});
	CHECK(inside.waterType == 1);
	CHECK(inside.waterHeight == Approx(3.5f));
	CHECK(inside.heightWeight == 0.0f);
	CHECK(t.Evaluate(53.5f, 25.0f, 0.0f, {}).waterType == 1);
	CHECK(t.Evaluate(54.5f, 25.0f, 0.0f, {}).waterType == 0);
	CHECK(t.Evaluate(70.0f, 25.0f, 0.0f, {}).waterType == 0);
	// Independent of the base height (Apply's water pass passes 0).
	CHECK(t.Evaluate(25.0f, 25.0f, 17.0f, {}).waterHeight == Approx(3.5f));
}

TEST_CASE("Channel modes fill their channels exactly as All does", "[graph_terrain_compose]")
{
	// Every op type at once: noise, raise and carve under a section with mass and remapped paint, a paint op and water.
	SectionAsset road = MakeRoad();
	road.layerTextures = { "Textures/Dirt.htex", "", "Textures/Gravel.htex", "" };
	const std::map<String, SectionAsset> assets{ { "Sections/TestRoad.hsec", road } };
	TerrainOpInput section = MakeOp(TerrainOpType::SectionPath, Line(0.0f, -60.0f, 0.0f, 60.0f, 4.0f), 4, { { "massEnabled", true } });
	section.op.asset = "Sections/TestRoad.hsec";
	TerrainOpInput water = MakeOp(TerrainOpType::WaterArea, Square(20.0f, -20.0f, 30.0f), 6, { { "level", 2.0f } });
	water.op.asset = "Materials/Water.hmat";
	const std::vector<TerrainOpInput> ops{
		MakeOp(TerrainOpType::Noise, Square(-60.0f, -60.0f, 120.0f), 1, { { "amplitude", 2.0f } }),
		MakeOp(TerrainOpType::Raise, Line(-40.0f, 0.0f, 40.0f, 0.0f, 6.0f), 2),
		MakeOp(TerrainOpType::Carve, Line(-40.0f, 20.0f, 40.0f, 20.0f), 3),
		section,
		MakeOp(TerrainOpType::Paint, Square(-30.0f, -30.0f, 25.0f), 5, { { "texture", String("Textures/Sand.htex") }, { "weight", 0.7f } }),
		water,
	};
	PreparedTerrain t;
	PrepareOk(t, ops, Square(-55.0f, -55.0f, 110.0f), 8.0f, Provider(assets));

	// Gravel is missing west of x = 0, so the notes must match too.
	const TextureLayerLookup lookup = [](const float x, float, const String& key) -> int8
	{
		if (key == "Textures/Dirt.htex")
		{
			return 1;
		}
		if (key == "Textures/Sand.htex")
		{
			return 3;
		}
		return key == "Textures/Gravel.htex" && x >= 0.0f ? 2 : -1;
	};

	size_t splatSamples = 0;
	size_t waterSamples = 0;
	size_t heightSamples = 0;
	TerrainEvaluateNotes allNotes;
	TerrainEvaluateNotes splatNotes;
	for (float z = -58.0f; z <= 58.0f; z += 1.7f)
	{
		for (float x = -58.0f; x <= 58.0f; x += 1.3f)
		{
			const float base = 0.25f * x - 0.1f * z;
			const TerrainTarget all = t.Evaluate(x, z, base, lookup, &allNotes);
			const TerrainTarget heights = t.Evaluate(x, z, base, lookup, nullptr, terrain_channel::Heights);
			const TerrainTarget splat = t.Evaluate(x, z, 0.0f, lookup, &splatNotes, terrain_channel::Splat);
			const TerrainTarget waterOnly = t.Evaluate(x, z, 0.0f, lookup, nullptr, terrain_channel::Water);
			const TerrainTarget none = t.Evaluate(x, z, base, lookup, nullptr, terrain_channel::Material);

			REQUIRE(heights.heightWeight == all.heightWeight);
			REQUIRE(heights.height == all.height);
			REQUIRE(splat.splatWeight == all.splatWeight);
			REQUIRE(splat.splat == all.splat);
			REQUIRE(waterOnly.waterType == all.waterType);
			REQUIRE(waterOnly.waterHeight == all.waterHeight);

			// The other channels stay at their defaults.
			CHECK(heights.splatWeight == 0.0f);
			CHECK(heights.waterType == 0);
			CHECK(splat.heightWeight == 0.0f);
			CHECK(splat.waterType == 0);
			CHECK(waterOnly.heightWeight == 0.0f);
			CHECK(waterOnly.splatWeight == 0.0f);
			CHECK(none.heightWeight == 0.0f);
			CHECK(none.splatWeight == 0.0f);
			CHECK(none.waterType == 0);

			splatSamples += all.splatWeight > 0.0f ? 1 : 0;
			waterSamples += all.waterType != 0 ? 1 : 0;
			heightSamples += all.heightWeight > 0.0f ? 1 : 0;
		}
	}
	CHECK(splatSamples > 100);
	CHECK(waterSamples > 100);
	CHECK(heightSamples > 1000);
	CHECK(!allNotes.missingSectionLayers.empty());
	CHECK(splatNotes.missingSectionLayers == allNotes.missingSectionLayers);
	CHECK(splatNotes.missingLayerOps == allNotes.missingLayerOps);
}

TEST_CASE("CollectTerrainOps gathers Terrain pins in op order", "[graph_terrain_compose]")
{
	Graph graph;
	NodeInstance n1; n1.id = 1; n1.type = "A";
	NodeInstance n2; n2.id = 2; n2.type = "B";
	graph.nodes = { n1, n2 };

	EvalResult result;
	result.ok = true;
	DataItem a;
	a.kind = ItemKind::Terrain;
	a.op.type = TerrainOpType::Carve;
	a.op.order = 2;
	a.sourceId = 11;
	DataItem b = a;
	b.op.type = TerrainOpType::Raise;
	b.sourceId = 12;
	DataItem c = a;
	c.op.type = TerrainOpType::Noise;
	c.op.order = 1;
	DataItem points;
	points.kind = ItemKind::Points;
	result.nodes[2].output.pins["Terrain"] = { a, points, b };
	result.nodes[1].output.pins["Terrain"] = { c };
	result.nodes[1].output.pins["Out"] = { a };
	result.nodes[7].output.pins["Terrain"] = { a }; // not in the graph

	const std::vector<TerrainOpInput> ops = CollectTerrainOps(graph, result);
	REQUIRE(ops.size() == 3);
	CHECK(ops[0].op.type == TerrainOpType::Noise);
	CHECK(ops[1].op.type == TerrainOpType::Carve);
	CHECK(ops[1].sourceId == 11);
	CHECK(ops[2].op.type == TerrainOpType::Raise);
	CHECK(ops[1].itemIndex == 0);
	CHECK(ops[2].itemIndex == 2);
}

TEST_CASE("Stack order sorts across nodes before the node id", "[graph_terrain_compose]")
{
	const std::map<String, SectionAsset> assets{ { "Sections/TestRoad.hsec", MakeRoad() } };
	const PathSpline line = Line(0.0f, -100.0f, 0.0f, 100.0f, 10.0f);

	const auto build = [&](const int32 sectionStack, const int32 carveStack)
	{
		Graph graph;
		NodeInstance n2; n2.id = 2; n2.type = "Carve";
		NodeInstance n5; n5.id = 5; n5.type = "Section";
		graph.nodes = { n2, n5 };

		EvalResult result;
		result.ok = true;
		DataItem section;
		section.kind = ItemKind::Terrain;
		section.op.type = TerrainOpType::SectionPath;
		section.op.asset = "Sections/TestRoad.hsec";
		section.op.order = 5;
		section.op.stackOrder = sectionStack;
		section.spline = line;
		section.sourceId = 9;
		DataItem carve;
		carve.kind = ItemKind::Terrain;
		carve.op.type = TerrainOpType::Carve;
		carve.op.order = 2;
		carve.op.stackOrder = carveStack;
		carve.op.params = { { "width", 4.0f }, { "depth", 1.5f }, { "bank", 3.0f } };
		carve.spline = line;
		carve.sourceId = 9;
		result.nodes[5].output.pins["Terrain"] = { section };
		result.nodes[2].output.pins["Terrain"] = { carve };

		const std::vector<TerrainOpInput> ops = CollectTerrainOps(graph, result);
		PreparedTerrain t;
		PrepareOk(t, ops, kBigRegion, 10.0f, Provider(assets));
		return Written(t.Evaluate(0.0f, 30.0f, 0.0f, {}), 0.0f);
	};

	// All at 0: node order, the carve (node 2) first and the absolute section (node 5) overwrites it.
	CHECK(build(0, 0) == Approx(9.5f).margin(1e-3f));
	// Node 5 at -1 is stacked before node 2, so the carve lowers the height of the section.
	CHECK(build(-1, 0) == Approx(8.0f).margin(1e-3f));
	// Equal values keep node order.
	CHECK(build(-1, -1) == Approx(9.5f).margin(1e-3f));
	CHECK(build(2, 2) == Approx(9.5f).margin(1e-3f));
}

TEST_CASE("Stack order ties fall back to the node id, then the item index", "[graph_terrain_compose]")
{
	Graph graph;
	NodeInstance n1; n1.id = 1; n1.type = "A";
	NodeInstance n2; n2.id = 2; n2.type = "B";
	graph.nodes = { n1, n2 };

	EvalResult result;
	result.ok = true;
	DataItem a;
	a.kind = ItemKind::Terrain;
	a.op.type = TerrainOpType::Carve;
	a.op.order = 2;
	a.op.stackOrder = 4;
	a.sourceId = 11;
	DataItem b = a;
	b.op.type = TerrainOpType::Raise;
	b.sourceId = 12;
	DataItem c = a;
	c.op.type = TerrainOpType::Noise;
	c.op.order = 1;
	DataItem d = a;
	d.op.type = TerrainOpType::Paint;
	d.op.order = 1;
	d.op.stackOrder = 5;
	result.nodes[2].output.pins["Terrain"] = { a, b };
	result.nodes[1].output.pins["Terrain"] = { d, c };

	const std::vector<TerrainOpInput> ops = CollectTerrainOps(graph, result);
	REQUIRE(ops.size() == 4);
	CHECK(ops[0].op.type == TerrainOpType::Noise);
	CHECK(ops[1].op.type == TerrainOpType::Carve);
	CHECK(ops[2].op.type == TerrainOpType::Raise);
	CHECK(ops[3].op.type == TerrainOpType::Paint);
}

TEST_CASE("Stack order changes neither the placement keys nor their positions", "[graph_terrain_compose]")
{
	const std::map<String, SectionAsset> assets{ { "Sections/TestRoad.hsec", MakeRoad() } };
	TerrainOpInput op = MakeOp(TerrainOpType::SectionPath, Line(0.0f, 0.0f, 0.0f, 80.0f, 0.0f), 4);
	op.op.asset = "Sections/TestRoad.hsec";
	TerrainOpInput stacked = op;
	stacked.op.stackOrder = -7;

	PreparedTerrain one;
	PrepareOk(one, { op }, kBigRegion, 10.0f, Provider(assets));
	std::vector<Placement> first;
	one.BuildPlacements(Flat0, first);
	REQUIRE(first.size() == 4);

	PreparedTerrain two;
	PrepareOk(two, { stacked }, kBigRegion, 10.0f, Provider(assets));
	std::vector<Placement> second;
	two.BuildPlacements(Flat0, second);
	REQUIRE(second.size() == first.size());
	for (size_t i = 0; i < first.size(); ++i)
	{
		CHECK(second[i].key == first[i].key);
		CHECK(second[i].position.x == first[i].position.x);
		CHECK(second[i].position.z == first[i].position.z);
	}
}

TEST_CASE("Section node output prepares with default and overridden params", "[graph_terrain_compose][graph_nodes_section]")
{
	const std::map<String, SectionAsset> assets{ { "Sections/TestRoad.hsec", MakeRoad() } };
	const NodeTypeDef* def = NodeRegistry::Default().Find("LaySectionAlongSpline");
	REQUIRE(def != nullptr);

	DataItem road;
	road.kind = ItemKind::Spline;
	road.spline = Line(0.0f, 0.0f, 0.0f, 80.0f);
	road.sourceId = 9;
	road.attributes["section"] = String("Sections/TestRoad.hsec");

	for (const bool overridden : { false, true })
	{
		if (overridden)
		{
			road.attributes["heightMode"] = String("smoothedGround");
			road.attributes["groundSmoothing"] = int64(40);
			road.attributes["massEnabled"] = true;
		}
		AttributeSet settings;
		for (const SettingDef& s : def->settings)
		{
			settings.emplace(s.name, s.defaultValue);
		}
		std::map<String, Collection> inputs;
		inputs["In"].push_back(road);
		const NodeOutput out = def->execute(EvalContext(), 1, settings, inputs);
		REQUIRE(out.error.empty());
		const DataItem& item = out.pins.at("Terrain")[0];

		TerrainOpInput in;
		in.op = item.op;
		in.spline = item.spline;
		in.pointIds = item.pointIds;
		in.sourceId = item.sourceId;

		PreparedTerrain t;
		String error;
		CHECK(t.Prepare({ in }, Flat0, Provider(assets), kBigRegion, 10.0f, error));
		CHECK(error.empty());
		CHECK_FALSE(t.IsEmpty());
	}
}

namespace
{
	/// A lake: 50 m square, level 21.5, margin 4, on a flat base at 25 m unless `ground` says otherwise.
	TerrainOpInput MakeLake(const float depth, const float shoreWidth = 6.0f)
	{
		return MakeOp(TerrainOpType::WaterArea, Square(0.0f, 0.0f, 50.0f), 1,
			{ { "level", 21.5f }, { "depth", depth }, { "shoreWidth", shoreWidth } });
	}

	void PrepareLake(PreparedTerrain& t, const TerrainOpInput& lake, const std::function<float(float, float)>& ground)
	{
		String error;
		REQUIRE(t.Prepare({ lake }, ground, {}, kBigRegion, 0.0f, error));
		CHECK(error.empty());
	}

	float Flat25(float, float)
	{
		return 25.0f;
	}
}

TEST_CASE("A lake basin follows the shore profile down to the bed", "[graph_terrain_compose]")
{
	PreparedTerrain t;
	PrepareLake(t, MakeLake(3.0f), Flat25);

	// The edge keeps the running height, 3 m in is halfway down the shore, 10 m in is the bed.
	CHECK(Written(t.Evaluate(0.01f, 25.0f, 25.0f, {}), 25.0f) == Approx(25.0f).margin(0.01));
	CHECK(Written(t.Evaluate(3.0f, 25.0f, 25.0f, {}), 25.0f) == Approx(21.75f));
	CHECK(Written(t.Evaluate(10.0f, 25.0f, 25.0f, {}), 25.0f) == Approx(18.5f));
	CHECK(Written(t.Evaluate(25.0f, 25.0f, 25.0f, {}), 25.0f) == Approx(18.5f));

	const TerrainTarget inside = t.Evaluate(25.0f, 25.0f, 25.0f, {});
	CHECK(inside.waterType == 1);
	CHECK(inside.waterHeight == Approx(21.5f));

	// The basin owns the bed: a hand bump inside is flattened, one outside survives.
	CHECK(Written(t.Evaluate(10.0f, 25.0f, 27.0f, {}), 27.0f) == Approx(18.5f));
	CHECK(Written(t.Evaluate(-10.0f, 25.0f, 27.0f, {}), 27.0f) == Approx(27.0f));
	CHECK(t.Evaluate(-2.0f, 25.0f, 27.0f, {}).heightWeight == 0.0f);

	// Ground already below the bed stays.
	CHECK(Written(t.Evaluate(25.0f, 25.0f, 10.0f, {}), 10.0f) == Approx(10.0f));
}

TEST_CASE("A lake without depth writes no heights", "[graph_terrain_compose]")
{
	PreparedTerrain zero;
	PrepareLake(zero, MakeLake(0.0f), Flat25);
	PreparedTerrain absent;
	PrepareLake(absent, MakeOp(TerrainOpType::WaterArea, Square(0.0f, 0.0f, 50.0f), 1, { { "level", 21.5f } }), Flat25);

	for (int i = 0; i < 50; ++i)
	{
		const float x = -8.0f + 1.3f * static_cast<float>(i);
		const float z = 3.0f + 0.9f * static_cast<float>(i);
		const TerrainTarget a = zero.Evaluate(x, z, 25.0f, {});
		const TerrainTarget b = absent.Evaluate(x, z, 25.0f, {});
		CHECK(a.heightWeight == 0.0f);
		CHECK(a.heightWeight == b.heightWeight);
		CHECK(a.height == b.height);
		CHECK(a.waterType == b.waterType);
		CHECK(a.waterHeight == b.waterHeight);
	}
}

TEST_CASE("A lake warns once when the base rim is below the water level", "[graph_terrain_compose]")
{
	// The west side of the area is at 20 m, below the level 21.5.
	PreparedTerrain low;
	PrepareLake(low, MakeLake(3.0f), [](const float x, float) { return x < 1.0f ? 20.0f : 25.0f; });
	REQUIRE(low.GetWarnings().size() == 1);
	CHECK(low.GetWarnings()[0] == "WaterArea (node 1, control 9): base rim drops to 20 m, below the water level 21.5 m; water will show past the shore");

	PreparedTerrain high;
	PrepareLake(high, MakeLake(3.0f), Flat25);
	CHECK(high.GetWarnings().empty());
}

TEST_CASE("A water-only evaluation does not run the basin", "[graph_terrain_compose]")
{
	PreparedTerrain t;
	PrepareLake(t, MakeLake(3.0f), Flat25);

	const TerrainTarget water = t.Evaluate(25.0f, 25.0f, 25.0f, {}, nullptr, terrain_channel::Water);
	CHECK(water.heightWeight == 0.0f);
	CHECK(water.waterType == 1);
	CHECK(water.waterHeight == Approx(21.5f));

	const TerrainTarget heights = t.Evaluate(25.0f, 25.0f, 25.0f, {}, nullptr, terrain_channel::Heights);
	CHECK(Written(heights, 25.0f) == Approx(18.5f));
	CHECK(heights.waterType == 0);
}

TEST_CASE("A lake basin outline carries the area and a depth mark at its centroid", "[graph_terrain_compose]")
{
	PreparedTerrain t;
	PrepareLake(t, MakeLake(3.0f), Flat25);
	std::vector<TerrainOpOutline> outlines;
	t.GetOutlines(outlines);
	REQUIRE(outlines.size() == 1);
	CHECK(outlines[0].closed);
	CHECK(outlines[0].points.size() >= 4);
	CHECK(outlines[0].depth == Approx(3.0f));
	CHECK(outlines[0].level == Approx(21.5f));
	CHECK(outlines[0].centroid.x == Approx(25.0f));
	CHECK(outlines[0].centroid.z == Approx(25.0f));

	PreparedTerrain flat;
	PrepareLake(flat, MakeLake(0.0f), Flat25);
	flat.GetOutlines(outlines);
	REQUIRE(outlines.size() == 1);
	CHECK(outlines[0].depth == 0.0f);
}

namespace
{
	/// Ground falling 2 % towards +x: 10 m at x = 0.
	float Slope(const float x, float)
	{
		return 10.0f - 0.02f * x;
	}

	/// A river along z = 0 from x0 to x1, its points on the given heights.
	TerrainOpInput MakeRiver(const float x0, const float y0, const float x1, const float y1, const uint32 order = 1,
		const AttributeSet& params = {})
	{
		PathSpline s = Line(x0, 0.0f, x1, 0.0f);
		s.points[0].y = y0;
		s.points[1].y = y1;
		return MakeOp(TerrainOpType::River, s, order, params);
	}

	void PrepareRiver(PreparedTerrain& t, const std::vector<TerrainOpInput>& ops, const std::function<float(float, float)>& ground,
		const PathSpline& region = kBigRegion, const float borderFade = 0.0f)
	{
		String error;
		REQUIRE(t.Prepare(ops, ground, {}, region, borderFade, error));
		CHECK(error.empty());
	}

	bool HasWarning(const PreparedTerrain& t, const String& part)
	{
		for (const String& w : t.GetWarnings())
		{
			if (w.find(part) != String::npos)
			{
				return true;
			}
		}
		return false;
	}
}

TEST_CASE("A river carves its bed below a running water surface", "[terrain_compose]")
{
	// Defaults: width 5, bank 4, depth 1.5, surfaceOffset -0.4, waterMargin 3. The spline lies on the 2 % slope.
	PreparedTerrain t;
	PrepareRiver(t, { MakeRiver(0.0f, 10.0f, 200.0f, 6.0f) }, Slope);
	CHECK(t.GetWarnings().empty());

	// At x = 100 the ground is 8, the surface 7.6 and the bed 6.1.
	const TerrainTarget centre = t.Evaluate(100.0f, 0.0f, 8.0f, {});
	CHECK(Written(centre, 8.0f) == Approx(6.1f).margin(1e-4));
	CHECK(centre.waterType == 1);
	CHECK(centre.waterHeight == Approx(7.6f).margin(1e-4));
	CHECK(Written(t.Evaluate(100.0f, 4.9f, 8.0f, {}), 8.0f) == Approx(6.1f).margin(1e-4));

	// Halfway up the bank: smoothstep(5, 9, 7) = 0.5.
	CHECK(Written(t.Evaluate(100.0f, 7.0f, 8.0f, {}), 8.0f) == Approx(8.0f - 0.5f * 1.9f).margin(1e-4));

	// At width + bank the running height holds; beyond, ground above the water is not written.
	const TerrainTarget rim = t.Evaluate(100.0f, 9.0f, 8.0f, {});
	CHECK(rim.heightWeight == 1.0f);
	CHECK(Written(rim, 8.0f) == Approx(8.0f));
	CHECK(t.Evaluate(100.0f, 9.5f, 8.0f, {}).heightWeight == 0.0f);

	// Water inside width + bank + waterMargin = 12, none beyond.
	CHECK(t.Evaluate(100.0f, 11.9f, 8.0f, {}).waterType == 1);
	CHECK(t.Evaluate(100.0f, -11.9f, 8.0f, {}).waterHeight == Approx(7.6f).margin(1e-4));
	CHECK(t.Evaluate(100.0f, 12.1f, 8.0f, {}).waterType == 0);
	CHECK(t.Evaluate(100.0f, 30.0f, 8.0f, {}).waterType == 0);
}

TEST_CASE("A river bank rises above the water surface over low ground", "[terrain_compose]")
{
	// Ground at 2 beside a surface of 7.6 (bed 6.1): the bank climbs to the surface + 0.25 at width + bank, holds it
	// to the end of the water (width + bank + waterMargin = 12) and fades back to the ground over another bank (16).
	PreparedTerrain t;
	PrepareRiver(t, { MakeRiver(0.0f, 10.0f, 200.0f, 6.0f) }, Slope);
	const float top = 7.6f + 0.25f;

	// smoothstep(5, 9, 8) = 0.84375.
	CHECK(Written(t.Evaluate(100.0f, 8.0f, 2.0f, {}), 2.0f) == Approx(6.1f + 0.84375f * (top - 6.1f)).margin(1e-4));
	for (const float d : { 9.0f, 10.5f, 12.0f })
	{
		CHECK(Written(t.Evaluate(100.0f, d, 2.0f, {}), 2.0f) == Approx(top).margin(1e-4));
		CHECK(Written(t.Evaluate(100.0f, -d, 2.0f, {}), 2.0f) == Approx(top).margin(1e-4));
	}
	// Halfway down the outer slope: smoothstep(12, 16, 14) = 0.5.
	CHECK(Written(t.Evaluate(100.0f, 14.0f, 2.0f, {}), 2.0f) == Approx(2.0f + 0.5f * (top - 2.0f)).margin(1e-4));
	CHECK(t.Evaluate(100.0f, 16.5f, 2.0f, {}).heightWeight == 0.0f);

	// Every point that holds water but lies outside the bed is above the surface or below it only on the bank's
	// way down, so the water's edge is where the bank crosses the surface, not where the water ends.
	for (float d = 9.0f; d <= 12.0f; d += 0.25f)
	{
		const TerrainTarget target = t.Evaluate(100.0f, d, 2.0f, {});
		CHECK(target.waterType == 1);
		CHECK(Written(target, 2.0f) > target.waterHeight);
	}
}

TEST_CASE("A river closes its upstream end like its banks", "[terrain_compose]")
{
	// The river runs along z = 0 from x = 0 (upstream, surface 9.6, bed 8.1) to x = 200.
	PreparedTerrain t;
	PrepareRiver(t, { MakeRiver(0.0f, 10.0f, 200.0f, 6.0f) }, Slope);

	// Past the start the distance to the bed's end edge works like the distance past the width.
	const float ground = Slope(-2.0f, 0.0f);
	CHECK(Written(t.Evaluate(-2.0f, 0.0f, ground, {}), ground) == Approx(8.1f + 0.5f * (ground - 8.1f)).margin(1e-3));
	CHECK(t.Evaluate(-2.0f, 0.0f, ground, {}).waterType == 1);
	CHECK(t.Evaluate(-6.9f, 0.0f, ground, {}).waterHeight == Approx(9.6f).margin(1e-4));
	CHECK(t.Evaluate(-7.1f, 0.0f, ground, {}).waterType == 0);
	// hypot(8 - 5, 4) = 5: water still holds (<= 7), and low ground there is raised above the surface.
	const TerrainTarget corner = t.Evaluate(-4.0f, 8.0f, 2.0f, {});
	CHECK(corner.waterType == 1);
	CHECK(Written(corner, 2.0f) == Approx(9.6f + 0.25f).margin(1e-4));
}

TEST_CASE("A river has a flat downstream end: no water and no bed past it", "[terrain_compose]")
{
	// Defaults (width 5, bank 4, depth 1.5, waterMargin 3); the river runs along z = 0 from x = 0 to x = 200.
	PreparedTerrain t;
	PrepareRiver(t, { MakeRiver(0.0f, 10.0f, 200.0f, 6.0f) }, Slope);

	// Just inside either end: bed and water as along the river.
	for (const float x : { 0.25f, 199.75f })
	{
		const float ground = Slope(x, 0.0f);
		const TerrainTarget inside = t.Evaluate(x, 0.0f, ground, {});
		CHECK(inside.waterType == 1);
		CHECK(Written(inside, ground) == Approx(inside.waterHeight - 1.5f).margin(1e-4));
	}

	// Past the end (it flows into something: a waterfall, a lake).
	for (const float past : { 0.25f, 1.0f, 2.0f, 4.0f, 6.0f, 7.9f, 11.9f })
	{
		for (const float z : { 0.0f, 3.0f, -4.9f, 7.5f })
		{
			const float x = 200.0f + past;
			const float ground = Slope(x, z);
			const TerrainTarget target = t.Evaluate(x, z, ground, {});
			CHECK(target.waterType == 0);
			// No owned bed: the bank closing the bed only lowers, less the further past the end.
			const float bed = 5.6f - 1.5f;
			if (past >= 1.0f)
			{
				CHECK(Written(target, ground) > bed + 0.1f);
			}
			CHECK(Written(target, ground) <= ground);
			if (past >= 4.0f)
			{
				CHECK(target.heightWeight == 0.0f);
			}
		}
	}

	// The bank closing the bed meets the bed at the end line and the ground at bank distance past it.
	const float atEnd = Written(t.Evaluate(200.001f, 0.0f, Slope(200.0f, 0.0f), {}), Slope(200.0f, 0.0f));
	CHECK(atEnd == Approx(5.6f - 1.5f).margin(1e-2));
	// Halfway (smoothstep 0.5); ground below the bed is never raised.
	CHECK(Written(t.Evaluate(202.0f, 0.0f, 4.0f, {}), 4.0f) == Approx(4.0f));
	CHECK(Written(t.Evaluate(202.0f, 0.0f, 9.0f, {}), 9.0f) == Approx(9.0f - 0.5f * 4.9f).margin(1e-4));
}

TEST_CASE("A river surface stays flat over a bump in the spline, with one warning", "[terrain_compose]")
{
	PathSpline s;
	for (const auto& p : { std::make_pair(0.0f, 10.0f), std::make_pair(50.0f, 12.0f), std::make_pair(100.0f, 10.0f) })
	{
		SplinePoint point;
		point.x = p.first;
		point.y = p.second;
		s.points.push_back(point);
	}
	PreparedTerrain t;
	PrepareRiver(t, { MakeOp(TerrainOpType::River, s, 1) }, [](float, float) { return 10.0f; });

	for (const float x : { 10.0f, 30.0f, 50.0f, 70.0f, 90.0f })
	{
		const TerrainTarget target = t.Evaluate(x, 0.0f, 10.0f, {});
		CHECK(target.waterHeight == Approx(9.6f).margin(1e-4));
		CHECK(Written(target, 10.0f) == Approx(8.1f).margin(1e-4));
	}
	REQUIRE(t.GetWarnings().size() == 1);
	CHECK(t.GetWarnings()[0] == "River (node 1, control 9): the spline climbs 2 m above the water surface upstream of it; the surface stays flat there (is the river drawn upstream-first?)");
}

TEST_CASE("A river drawn uphill keeps the first point's surface, with one warning", "[terrain_compose]")
{
	PreparedTerrain t;
	PrepareRiver(t, { MakeRiver(200.0f, 6.0f, 0.0f, 10.0f) }, Slope);
	for (const float x : { 190.0f, 100.0f, 10.0f })
	{
		CHECK(t.Evaluate(x, 0.0f, Slope(x, 0.0f), {}).waterHeight == Approx(5.6f).margin(1e-4));
	}
	REQUIRE(t.GetWarnings().size() == 1);
	CHECK(HasWarning(t, "climbs 4 m"));
}

TEST_CASE("River flow paths follow the river inside the region", "[terrain_compose]")
{
	// The river runs from x = -50 to 150; the region spans x 0..100 with a 10 m border fade.
	PreparedTerrain t;
	PrepareRiver(t, { MakeRiver(-50.0f, 11.0f, 150.0f, 7.0f, 1, { { "width", 3.0f }, { "waterMargin", 1.5f } }) }, Slope,
		Square(0.0f, -50.0f, 100.0f), 10.0f);

	std::vector<terrain_io::water_flow::FlowPath> paths;
	t.GetFlowPaths(paths);
	REQUIRE(paths.size() == 1);
	const terrain_io::water_flow::FlowPath& path = paths[0];
	REQUIRE(path.size() == 25);
	CHECK(path.front().x == Approx(2.0f));
	CHECK(path.back().x == Approx(98.0f));
	for (size_t i = 0; i < path.size(); ++i)
	{
		CHECK(path[i].z == Approx(0.0f).margin(1e-5));
		CHECK(path[i].halfWidth == Approx(8.5f));
		if (i > 0)
		{
			CHECK(path[i].x - path[i - 1].x == Approx(4.0f));
		}
	}

	// A river wholly inside: one path from the first to the last point.
	PreparedTerrain inside;
	PrepareRiver(inside, { MakeRiver(0.0f, 10.0f, 30.0f, 9.4f) }, Slope);
	inside.GetFlowPaths(paths);
	REQUIRE(paths.size() == 1);
	REQUIRE(paths[0].size() == 9);
	CHECK(paths[0].front().x == Approx(0.0f));
	CHECK(paths[0].back().x == Approx(30.0f));
	CHECK(paths[0][7].x == Approx(28.0f));
	CHECK(paths[0][0].halfWidth == Approx(12.0f));

	// No rivers, no paths.
	PreparedTerrain none;
	PrepareRiver(none, { MakeLake(0.0f) }, Flat25);
	none.GetFlowPaths(paths);
	CHECK(paths.empty());
}

TEST_CASE("A river material wins over a WaterArea material, with one warning", "[terrain_compose]")
{
	TerrainOpInput lake = MakeOp(TerrainOpType::WaterArea, Square(0.0f, 0.0f, 50.0f), 1, { { "level", -1.0f } });
	lake.op.asset = "Water/Lake.hmat";
	TerrainOpInput river = MakeRiver(-20.0f, 0.0f, 80.0f, 0.0f, 2);
	river.op.asset = "Water/River.hmat";

	PreparedTerrain lakeFirst;
	PrepareRiver(lakeFirst, { lake, river }, Flat0);
	CHECK(lakeFirst.GetWaterMaterial() == "Water/River.hmat");
	CHECK(lakeFirst.GetWarnings().size() == 1);

	PreparedTerrain riverFirst;
	PrepareRiver(riverFirst, { river, lake }, Flat0);
	CHECK(riverFirst.GetWaterMaterial() == "Water/River.hmat");
	CHECK(riverFirst.GetWarnings().size() == 1);

	// Among rivers the first one wins.
	TerrainOpInput other = MakeRiver(-20.0f, 0.0f, 80.0f, 0.0f, 3);
	other.op.asset = "Water/Other.hmat";
	PreparedTerrain rivers;
	PrepareRiver(rivers, { river, other }, Flat0);
	CHECK(rivers.GetWaterMaterial() == "Water/River.hmat");
	CHECK(rivers.GetWarnings().size() == 1);

	// The same material twice: no warning.
	lake.op.asset = river.op.asset;
	PreparedTerrain same;
	PrepareRiver(same, { lake, river }, Flat0);
	CHECK(same.GetWaterMaterial() == "Water/River.hmat");
	CHECK(same.GetWarnings().empty());
}

TEST_CASE("A river prepares deterministically", "[terrain_compose]")
{
	const std::vector<TerrainOpInput> ops = { MakeRiver(-30.0f, 10.6f, 170.0f, 6.6f, 1, { { "width", 4.0f } }) };
	PreparedTerrain a;
	PreparedTerrain b;
	PrepareRiver(a, ops, Slope);
	PrepareRiver(b, ops, Slope);
	for (int i = 0; i < 200; ++i)
	{
		const float x = -40.0f + 1.07f * static_cast<float>(i);
		const float z = -12.0f + 0.13f * static_cast<float>(i);
		const TerrainTarget ta = a.Evaluate(x, z, Slope(x, z), {});
		const TerrainTarget tb = b.Evaluate(x, z, Slope(x, z), {});
		CHECK(ta.height == tb.height);
		CHECK(ta.heightWeight == tb.heightWeight);
		CHECK(ta.waterType == tb.waterType);
		CHECK(ta.waterHeight == tb.waterHeight);
	}
	std::vector<terrain_io::water_flow::FlowPath> pa;
	std::vector<terrain_io::water_flow::FlowPath> pb;
	a.GetFlowPaths(pa);
	b.GetFlowPaths(pb);
	REQUIRE(pa.size() == pb.size());
	for (size_t i = 0; i < pa.size(); ++i)
	{
		REQUIRE(pa[i].size() == pb[i].size());
		for (size_t j = 0; j < pa[i].size(); ++j)
		{
			CHECK(pa[i][j].x == pb[i][j].x);
			CHECK(pa[i][j].z == pb[i][j].z);
			CHECK(pa[i][j].halfWidth == pb[i][j].halfWidth);
		}
	}
}

TEST_CASE("A river outline carries its surface line and bed edges", "[terrain_compose]")
{
	PreparedTerrain t;
	PrepareRiver(t, { MakeRiver(0.0f, 10.0f, 200.0f, 6.0f) }, Slope);
	std::vector<TerrainOpOutline> outlines;
	t.GetOutlines(outlines);
	REQUIRE(outlines.size() == 1);
	const TerrainOpOutline& o = outlines[0];
	CHECK(o.type == TerrainOpType::River);
	CHECK_FALSE(o.closed);
	REQUIRE(o.points.size() == 201);
	CHECK(o.points[100].x == Approx(100.0f));
	CHECK(o.points[100].y == Approx(7.6f).margin(1e-4));
	REQUIRE(o.bedEdges.size() == 2);
	for (const std::vector<Vector3>& edge : o.bedEdges)
	{
		REQUIRE(edge.size() == o.points.size());
		CHECK(std::abs(edge[100].z) == Approx(5.0f));
		CHECK(edge[100].x == Approx(100.0f));
	}
	CHECK(o.bedEdges[0][100].z == -o.bedEdges[1][100].z);
}

namespace
{
	/// Noise over a wide area (no falloff at the seams) for the seam tests.
	TerrainOpInput WideNoise()
	{
		return MakeOp(TerrainOpType::Noise, Square(-100.0f, -100.0f, 400.0f), 3, { { "amplitude", 2.0f }, { "wavelength", 24.0f }, { "falloff", 8.0f } });
	}

	void PrepareWithNeighbours(PreparedTerrain& t, const PathSpline& region, const std::vector<PathSpline>& neighbours)
	{
		String error;
		REQUIRE(t.Prepare({ WideNoise() }, Flat0, {}, region, 10.0f, error, neighbours));
		CHECK(error.empty());
	}
}

TEST_CASE("Touching regions do not fade along their shared edge", "[terrain_compose]")
{
	const PathSpline regionA = Square(0.0f, 0.0f, 100.0f);
	const PathSpline regionB = Square(100.0f, 0.0f, 100.0f);
	PreparedTerrain a;
	PrepareWithNeighbours(a, regionA, { regionB });
	PreparedTerrain b;
	PrepareWithNeighbours(b, regionB, { regionA });
	PreparedTerrain reference;
	PrepareWithNeighbours(reference, kBigRegion, {});

	CHECK(a.GetSeamHash() != 0);
	CHECK(b.GetSeamHash() != 0);
	CHECK(a.GetSeamNeighbours() == std::vector<size_t>{ 0 });
	float largestNoise = 0.0f;

	for (float z = 10.0f; z <= 90.0f; z += 16.0f)
	{
		const TerrainTarget onA = a.Evaluate(99.75f, z, 0.0f, {});
		const TerrainTarget onB = b.Evaluate(100.25f, z, 0.0f, {});
		// Unfaded: the height is the one of the unclipped noise.
		CHECK(Written(onA, 0.0f) == Approx(reference.Evaluate(99.75f, z, 0.0f, {}).height).margin(1e-5f));
		largestNoise = std::max(largestNoise, std::abs(Written(onA, 0.0f)));
		CHECK(Written(onB, 0.0f) == Approx(reference.Evaluate(100.25f, z, 0.0f, {}).height).margin(1e-5f));

		// The two sides differ by no more than the noise changes over 0.5 m.
		const float change = std::abs(reference.Evaluate(99.75f, z, 0.0f, {}).height - reference.Evaluate(100.25f, z, 0.0f, {}).height);
		CHECK(std::abs(Written(onA, 0.0f) - Written(onB, 0.0f)) <= change + 1e-4f);

		// Writes still stop at the region edge.
		CHECK(a.Evaluate(100.25f, z, 0.0f, {}).heightWeight == 0.0f);
		CHECK(b.Evaluate(99.75f, z, 0.0f, {}).heightWeight == 0.0f);
	}
}

TEST_CASE("The outer edges of a region with a seam still fade", "[terrain_compose]")
{
	const PathSpline regionA = Square(0.0f, 0.0f, 100.0f);
	PreparedTerrain withSeam;
	PrepareWithNeighbours(withSeam, regionA, { Square(100.0f, 0.0f, 100.0f) });
	PreparedTerrain alone;
	PrepareWithNeighbours(alone, regionA, {});
	PreparedTerrain reference;
	PrepareWithNeighbours(reference, kBigRegion, {});

	const std::vector<std::pair<float, float>> spots = { { 50.0f, 2.0f }, { 50.0f, 98.0f }, { 2.0f, 50.0f }, { 5.0f, 5.0f }, { 0.5f, 50.0f } };
	for (const auto& [x, z] : spots)
	{
		const TerrainTarget fadedSeam = withSeam.Evaluate(x, z, 0.0f, {});
		const TerrainTarget fadedAlone = alone.Evaluate(x, z, 0.0f, {});
		const float unfaded = reference.Evaluate(x, z, 0.0f, {}).height;
		REQUIRE(std::abs(unfaded) > 0.05f);
		CHECK(std::abs(Written(fadedSeam, 0.0f)) < std::abs(unfaded));
		CHECK(fadedSeam.heightWeight == fadedAlone.heightWeight);
		CHECK(fadedSeam.height == fadedAlone.height);
	}
}

TEST_CASE("Without neighbours the result is identical", "[terrain_compose]")
{
	const PathSpline region = Square(0.0f, 0.0f, 100.0f);
	PreparedTerrain plain;
	String error;
	REQUIRE(plain.Prepare({ WideNoise() }, Flat0, {}, region, 10.0f, error));
	PreparedTerrain empty;
	PrepareWithNeighbours(empty, region, {});
	// A neighbour that is nowhere near.
	PreparedTerrain far;
	PrepareWithNeighbours(far, region, { Square(300.0f, 300.0f, 50.0f) });

	CHECK(plain.GetSeamHash() == 0);
	CHECK(far.GetSeamHash() == 0);
	CHECK(far.GetSeamNeighbours().empty());
	for (int j = 0; j < 10; ++j)
	{
		for (int i = 0; i < 10; ++i)
		{
			const float x = -3.0f + static_cast<float>(i) * 11.0f;
			const float z = -3.0f + static_cast<float>(j) * 11.0f;
			const TerrainTarget p = plain.Evaluate(x, z, 1.0f, {});
			for (const PreparedTerrain* other : { &empty, &far })
			{
				const TerrainTarget o = other->Evaluate(x, z, 1.0f, {});
				CHECK(o.height == p.height);
				CHECK(o.heightWeight == p.heightWeight);
			}
		}
	}
}

TEST_CASE("Touching 1 km regions find their seam quickly", "[terrain_compose]")
{
	// 16000 border samples each (0.25 m step). A linear scan of the neighbour per query took 2 x 10^9 segment tests.
	const PathSpline regionA = Square(0.0f, 0.0f, 1000.0f);
	const PathSpline regionB = Square(1000.0f, 0.0f, 1000.0f);
	const auto start = std::chrono::steady_clock::now();
	PreparedTerrain a;
	PrepareWithNeighbours(a, regionA, { regionB });
	PreparedTerrain b;
	PrepareWithNeighbours(b, regionB, { regionA });
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	WARN("two touching 1 km regions prepared in " << seconds << " s");
	CHECK(a.GetSeamHash() != 0);
	CHECK(b.GetSeamHash() != 0);
	CHECK(a.GetSeamNeighbours() == std::vector<size_t>{ 0 });
	CHECK(b.GetSeamNeighbours() == std::vector<size_t>{ 0 });
}

TEST_CASE("A neighbour that only touches at a corner creates no seam", "[terrain_compose]")
{
	const PathSpline regionA = Square(0.0f, 0.0f, 100.0f);
	PreparedTerrain corner;
	PrepareWithNeighbours(corner, regionA, { Square(100.0f, 100.0f, 100.0f) });
	PreparedTerrain alone;
	PrepareWithNeighbours(alone, regionA, {});
	PreparedTerrain reference;
	PrepareWithNeighbours(reference, kBigRegion, {});
	CHECK(corner.GetSeamHash() == 0);
	for (const auto& [x, z] : std::vector<std::pair<float, float>>{ { 99.75f, 50.0f }, { 50.0f, 99.75f } })
	{
		CHECK(corner.Evaluate(x, z, 0.0f, {}).height == alone.Evaluate(x, z, 0.0f, {}).height);
		REQUIRE(std::abs(reference.Evaluate(x, z, 0.0f, {}).height) > 0.05f);
		CHECK(std::abs(corner.Evaluate(x, z, 0.0f, {}).height) < std::abs(reference.Evaluate(x, z, 0.0f, {}).height));
	}
}

TEST_CASE("A neighbour with a vertex in the middle of the shared edge still makes a seam", "[terrain_compose]")
{
	PathSpline neighbour = Square(100.0f, 0.0f, 100.0f);
	SplinePoint mid;
	mid.x = 100.0f;
	mid.z = 50.0f;
	neighbour.points.push_back(mid);
	// Closed loop: (100,0) (200,0) (200,100) (100,100) (100,50): the last edge runs back down the shared border.
	PreparedTerrain a;
	PrepareWithNeighbours(a, Square(0.0f, 0.0f, 100.0f), { neighbour });
	CHECK(a.GetSeamHash() != 0);
	PreparedTerrain reference;
	PrepareWithNeighbours(reference, kBigRegion, {});
	CHECK(a.Evaluate(99.75f, 25.0f, 0.0f, {}).height == Approx(reference.Evaluate(99.75f, 25.0f, 0.0f, {}).height).margin(1e-5f));
}
