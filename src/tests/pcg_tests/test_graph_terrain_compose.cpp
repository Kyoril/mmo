// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/graph/evaluator.h"
#include "pcg/graph/node_registry.h"
#include "pcg/graph/terrain_compose.h"
#include "pcg/section_path.h"

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
