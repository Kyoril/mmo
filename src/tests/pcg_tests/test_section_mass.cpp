// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "fake_terrain.h"

#include "pcg/section_path.h"
#include "pcg/terrain_contribution.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

using namespace mmo;
using namespace mmo::pcg;

namespace
{
	constexpr float kCentre = 10.0f;

	/// Section L 20, halfWidth 3, sideFalloff 4, one straight variant per height function h(localX, localZ) on a 1 m
	/// grid over x -8..8, z 0..20. Paints layer 0 = 55 / layer 2 = 200 with coverage 255 for |x| <= 2.
	SectionAsset MakeAsset(const std::vector<std::function<float(float, float)>>& heights)
	{
		SectionAsset asset;
		asset.name = "mass";
		asset.length = 20.0f;
		asset.halfWidth = 3.0f;
		asset.sideFalloff = 4.0f;
		for (const auto& height : heights)
		{
			SectionVariant v;
			v.heights.spacing = 1.0f;
			v.heights.originX = -8;
			v.heights.originZ = 0;
			v.heights.width = 17;
			v.heights.depth = 21;
			v.paint.spacing = 1.0f;
			v.paint.originX = -8;
			v.paint.originZ = 0;
			v.paint.width = 17;
			v.paint.depth = 21;
			for (uint32 j = 0; j < 21; ++j)
			{
				for (uint32 i = 0; i < 17; ++i)
				{
					const float x = -8.0f + static_cast<float>(i);
					const bool road = std::fabs(x) <= 2.0f;
					v.heights.values.push_back(height(x, static_cast<float>(j)));
					v.paint.packed.push_back(road ? (55u | (200u << 16)) : 255u);
					v.paint.coverage.push_back(road ? 255 : 0);
				}
			}
			asset.variants.push_back(v);
		}
		return asset;
	}

	float Zero(float, float)
	{
		return 0.0f;
	}

	PathSpline Polyline(const std::vector<std::pair<float, float>>& points, const bool closed = false,
		const SplineInterpolation interpolation = SplineInterpolation::Linear)
	{
		PathSpline spline;
		spline.interpolation = interpolation;
		spline.closed = closed;
		for (const auto& [x, z] : points)
		{
			SplinePoint p;
			p.x = x;
			p.z = z;
			p.y = kCentre;
			spline.points.push_back(p);
		}
		return spline;
	}

	std::array<int8, 4> Identity(float, float)
	{
		return { 0, 1, 2, 3 };
	}

	void Prepare(const PathSpline& spline, const SectionAsset& asset, const SectionPathParams& params, SectionPathShape& shape)
	{
		std::vector<uint64> ids;
		for (size_t i = 0; i < spline.points.size(); ++i)
		{
			ids.push_back(70 + i);
		}
		String error;
		REQUIRE(PrepareSectionPath(spline, ids, asset, params, [](float, float) { return kCentre; }, shape, error));
	}

	/// Mass on with the given height and depth and no noise.
	SectionPathParams Mass(const float height = 30.0f, const float depth = 60.0f)
	{
		SectionPathParams params;
		params.massEnabled = true;
		params.massHeight = height;
		params.massDepth = depth;
		params.massNoiseAmplitude = 0.0f;
		params.massDetailAmplitude = 0.0f;
		return params;
	}

	bool Same(const TerrainTarget& a, const TerrainTarget& b)
	{
		return a.height == b.height && a.heightWeight == b.heightWeight && a.splat == b.splat && a.splatWeight == b.splatWeight &&
			a.waterType == b.waterType && a.waterHeight == b.waterHeight && a.setMaterial == b.setMaterial;
	}

	/// The height Apply would write over a base.
	float Written(const TerrainTarget& t, const float base)
	{
		return t.heightWeight > 0.0f ? base + (t.height - base) * std::min(1.0f, t.heightWeight) : base;
	}

	const PathSpline kStraight = Polyline({ { 0.0f, 0.0f }, { 0.0f, 200.0f } });
}

TEST_CASE("Mass settings are ignored while the mass is off", "[section_mass]")
{
	const SectionAsset asset = MakeAsset({ [](float x, float) { return std::fabs(x) <= 2.0f ? 5.0f : 0.0f; } });
	SectionPathShape plain;
	Prepare(kStraight, asset, SectionPathParams(), plain);

	SectionPathParams off = Mass(80.0f, 150.0f);
	off.massEnabled = false;
	SectionPathShape shape;
	Prepare(kStraight, asset, off, shape);

	CHECK(GetInfluenceRect(shape).maxX == GetInfluenceRect(plain).maxX);
	for (float x = -20.0f; x <= 40.0f; x += 0.75f)
	{
		for (float z = -10.0f; z <= 210.0f; z += 3.5f)
		{
			REQUIRE(Same(EvaluateSectionTerrain(shape, x, z, kCentre, Identity), EvaluateSectionTerrain(plain, x, z, kCentre, Identity)));
		}
	}
}

TEST_CASE("The mass rises behind the wall to the centre plus the mass height", "[section_mass]")
{
	const SectionAsset asset = MakeAsset({ Zero });
	SectionPathShape shape;
	Prepare(kStraight, asset, Mass(30.0f, 60.0f), shape);

	// Hold zone: u in [0.25, 0.7] = local x in [12, 39].
	for (const float x : { 14.0f, 20.0f, 30.0f, 38.0f })
	{
		const TerrainTarget t = EvaluateSectionTerrain(shape, x, 100.0f, kCentre, Identity);
		CHECK(Written(t, kCentre) == Approx(kCentre + 30.0f).margin(1e-3f));
	}

	// The influence reaches the end of the mass.
	CHECK(GetInfluenceRect(shape).maxX >= 57.0f);

	// With noise the height stays within the amplitudes and varies along the wall.
	SectionPathParams noisy = Mass(30.0f, 60.0f);
	noisy.massNoiseAmplitude = 15.0f;
	noisy.massDetailAmplitude = 3.0f;
	SectionPathShape shapeNoisy;
	Prepare(kStraight, asset, noisy, shapeNoisy);
	float lo = 1e9f;
	float hi = -1e9f;
	for (float z = 10.0f; z <= 190.0f; z += 5.0f)
	{
		const float h = Written(EvaluateSectionTerrain(shapeNoisy, 25.0f, z, kCentre, Identity), kCentre);
		CHECK(h >= kCentre + 30.0f - 18.0f - 1e-3f);
		CHECK(h <= kCentre + 30.0f + 18.0f + 1e-3f);
		lo = std::min(lo, h);
		hi = std::max(hi, h);
	}
	CHECK(hi - lo > 2.0f);
}

TEST_CASE("The mass never changes the floor side", "[section_mass]")
{
	const SectionAsset asset = MakeAsset({ [](float x, float) { return x * 0.5f; } });
	SectionPathShape plain;
	Prepare(kStraight, asset, SectionPathParams(), plain);
	SectionPathParams params = Mass();
	params.massNoiseAmplitude = 15.0f;
	SectionPathShape shape;
	Prepare(kStraight, asset, params, shape);

	for (float x = -30.0f; x < -3.0f; x += 0.5f)
	{
		for (float z = -20.0f; z <= 220.0f; z += 2.5f)
		{
			REQUIRE(Same(EvaluateSectionTerrain(shape, x, z, kCentre, Identity), EvaluateSectionTerrain(plain, x, z, kCentre, Identity)));
		}
	}
}

TEST_CASE("The mass fades to the base at its depth", "[section_mass]")
{
	const SectionAsset asset = MakeAsset({ Zero });
	SectionPathShape shape;
	Prepare(kStraight, asset, Mass(30.0f, 60.0f), shape);

	// Back of the mass at local x = 60 - 3 = 57.
	CHECK(EvaluateSectionTerrain(shape, 57.5f, 100.0f, kCentre, Identity).heightWeight == 0.0f);
	CHECK(EvaluateSectionTerrain(shape, 80.0f, 100.0f, kCentre, Identity).heightWeight == 0.0f);

	float previous = kCentre + 30.0f + 1e-3f;
	for (float x = 39.0f; x <= 57.0f; x += 0.5f)
	{
		const float h = Written(EvaluateSectionTerrain(shape, x, 100.0f, kCentre, Identity), kCentre);
		CHECK(h <= previous);
		previous = h;
	}
	CHECK(previous == Approx(kCentre).margin(0.05f));

	// The mass only raises: over a base above the mass the base stays.
	CHECK(Written(EvaluateSectionTerrain(shape, 25.0f, 100.0f, 100.0f, Identity), 100.0f) == Approx(100.0f));
}

TEST_CASE("The mass fades out past the ends of an open path", "[section_mass]")
{
	const SectionAsset asset = MakeAsset({ Zero });
	SectionPathShape shape;
	Prepare(kStraight, asset, Mass(30.0f, 60.0f), shape);

	// The mass tapers out over halfWidth + sideFalloff = 7 m past the ends, like the section itself.
	const float inside = Written(EvaluateSectionTerrain(shape, 25.0f, 199.0f, kCentre, Identity), kCentre);
	const float near = Written(EvaluateSectionTerrain(shape, 25.0f, 203.0f, kCentre, Identity), kCentre);
	CHECK(inside == Approx(kCentre + 30.0f).margin(1e-3f));
	CHECK(near < inside);
	CHECK(near > kCentre);
	CHECK(EvaluateSectionTerrain(shape, 25.0f, 207.5f, kCentre, Identity).heightWeight == 0.0f);
	CHECK(EvaluateSectionTerrain(shape, 25.0f, -7.5f, kCentre, Identity).heightWeight == 0.0f);

	// The height tapers too (not only the weight): over a base below the centre the end slopes down to the base
	// instead of keeping the full mass height at a lower weight.
	const float half = Written(EvaluateSectionTerrain(shape, 25.0f, 203.5f, 0.0f, Identity), 0.0f);
	CHECK(half < 0.5f * (kCentre + 30.0f));
}

TEST_CASE("The facade keeps its detail where it rises above the mass", "[section_mass]")
{
	const SectionAsset asset = MakeAsset({ [](float x, float) { return x >= 0.0f ? 50.0f : 0.0f; } });
	SectionPathShape plain;
	Prepare(kStraight, asset, SectionPathParams(), plain);
	SectionPathShape shape;
	Prepare(kStraight, asset, Mass(30.0f, 60.0f), shape);

	for (const float x : { 0.5f, 1.5f, 2.5f })
	{
		const TerrainTarget withMass = EvaluateSectionTerrain(shape, x, 100.0f, kCentre, Identity);
		const TerrainTarget without = EvaluateSectionTerrain(plain, x, 100.0f, kCentre, Identity);
		CHECK(Written(withMass, kCentre) == Approx(Written(without, kCentre)).margin(1e-3f));
	}

	// Behind the facade the mass takes over.
	CHECK(Written(EvaluateSectionTerrain(shape, 20.0f, 100.0f, kCentre, Identity), kCentre) == Approx(kCentre + 30.0f).margin(1e-3f));
}

TEST_CASE("Mass noise is anchored to the world", "[section_mass]")
{
	const SectionAsset asset = MakeAsset({ Zero });
	SectionPathParams params = Mass(30.0f, 60.0f);
	params.massNoiseAmplitude = 15.0f;
	params.massDetailAmplitude = 3.0f;
	SectionPathShape before;
	Prepare(Polyline({ { 0.0f, 0.0f }, { 0.0f, 100.0f }, { 0.0f, 300.0f } }), asset, params, before);
	SectionPathShape after;
	Prepare(Polyline({ { 0.0f, 0.0f }, { 0.0f, 100.0f }, { 80.0f, 300.0f } }), asset, params, after);

	std::set<float> heights;
	for (float x = 5.0f; x <= 20.0f; x += 2.5f)
	{
		for (float z = 20.0f; z <= 70.0f; z += 2.0f)
		{
			const TerrainTarget a = EvaluateSectionTerrain(before, x, z, kCentre, Identity);
			const TerrainTarget b = EvaluateSectionTerrain(after, x, z, kCentre, Identity);
			REQUIRE(Same(a, b));
			heights.insert(a.height);
		}
	}
	CHECK(heights.size() > 10);

	// The same world point behind a translated, longer or reversed path at the same distance across gets the same
	// mass: the noise belongs to the world, not to the arc length (all three run along x = 0 or x = 50, the point
	// lies 25 m to their right in the hold zone).
	SectionPathParams hold = Mass(30.0f, 60.0f);
	hold.massNoiseAmplitude = 15.0f;
	hold.massDetailAmplitude = 3.0f;
	SectionPathShape base;
	Prepare(Polyline({ { 0.0f, 0.0f }, { 0.0f, 200.0f } }), asset, hold, base);
	SectionPathShape longer;
	Prepare(Polyline({ { 0.0f, -100.0f }, { 0.0f, 300.0f } }), asset, hold, longer);
	SectionPathShape reversed;
	Prepare(Polyline({ { 50.0f, 300.0f }, { 50.0f, -100.0f } }), asset, hold, reversed);
	for (float z = 40.0f; z <= 160.0f; z += 7.0f)
	{
		const float h = Written(EvaluateSectionTerrain(base, 25.0f, z, kCentre, Identity), kCentre);
		CHECK(Written(EvaluateSectionTerrain(longer, 25.0f, z, kCentre, Identity), kCentre) == Approx(h).margin(1e-4f));
		CHECK(Written(EvaluateSectionTerrain(reversed, 25.0f, z, kCentre, Identity), kCentre) == Approx(h).margin(1e-4f));
	}
}

TEST_CASE("Facade variety is off by default", "[section_mass]")
{
	const SectionAsset asset = MakeAsset({ Zero });
	SectionPathShape shape;
	Prepare(kStraight, asset, SectionPathParams(), shape);
	for (const SectionCopy& copy : shape.layout.copies)
	{
		const SectionCopyFacade facade = GetCopyFacade(shape, copy);
		CHECK_FALSE(facade.mirrored);
		CHECK(facade.heightScale == 1.0f);
	}
}

TEST_CASE("Mirrored copies reflect their heights and props", "[section_mass]")
{
	SectionAsset asset = MakeAsset({ [](float, float z) { return z; } });
	SectionPlacement prop;
	prop.placement.key = 4242;
	prop.placement.kind = PlacementKind::Entity;
	prop.placement.asset = "Models/Rock.hmsh";
	prop.placement.position = Vector3(1.0f, 0.0f, 5.0f);
	prop.placement.rotation = Quaternion(Radian(30.0f * 3.14159265f / 180.0f), Vector3::UnitY);
	asset.variants[0].placements.push_back(prop);

	SectionPathParams params;
	params.mirrorCopies = true;
	SectionPathShape shape;
	Prepare(kStraight, asset, params, shape);
	REQUIRE(shape.layout.copies.size() == 10);

	std::vector<Placement> placements;
	BuildSectionPlacements(shape, [](float, float) { return kCentre; }, placements);

	uint32 mirrored = 0;
	for (const SectionCopy& copy : shape.layout.copies)
	{
		const SectionCopyFacade facade = GetCopyFacade(shape, copy);
		mirrored += facade.mirrored ? 1 : 0;
		const float expected = facade.mirrored ? 15.0f : 5.0f;
		const TerrainTarget t = EvaluateSectionTerrain(shape, 0.0f, copy.startDistance + 5.0f, kCentre, Identity);
		CHECK(t.height == Approx(kCentre + expected).margin(1e-3f));

		const uint64 key = SectionPlacementKey(copy, 0, prop.placement.key, prop.placement.kind);
		const auto it = std::find_if(placements.begin(), placements.end(), [key](const Placement& p) { return p.key == key; });
		REQUIRE(it != placements.end());
		CHECK(it->position.z == Approx(copy.startDistance + expected).margin(1e-3f));
		CHECK(it->position.x == Approx(1.0f).margin(1e-4f));

		// Travel along +Z: frame yaw 0, so the rotation is the authored yaw, negated when mirrored.
		const Quaternion want(Radian((facade.mirrored ? -30.0f : 30.0f) * 3.14159265f / 180.0f), Vector3::UnitY);
		CHECK(std::fabs(it->rotation.w * want.w + it->rotation.x * want.x + it->rotation.y * want.y + it->rotation.z * want.z) == Approx(1.0f).margin(1e-4f));
	}
	CHECK(mirrored > 0);
	CHECK(mirrored < 10);
}

TEST_CASE("Height jitter scales each copy's section heights", "[section_mass]")
{
	const SectionAsset asset = MakeAsset({ [](float, float) { return 10.0f; } });
	SectionPathParams params;
	params.facadeHeightJitter = 0.2f;
	SectionPathShape shape;
	Prepare(kStraight, asset, params, shape);

	std::set<float> scales;
	for (const SectionCopy& copy : shape.layout.copies)
	{
		const SectionCopyFacade facade = GetCopyFacade(shape, copy);
		CHECK(facade.heightScale >= 0.8f);
		CHECK(facade.heightScale <= 1.2f);
		CHECK_FALSE(facade.mirrored);
		scales.insert(facade.heightScale);
		const TerrainTarget t = EvaluateSectionTerrain(shape, 0.0f, copy.startDistance + 10.0f, kCentre, Identity);
		CHECK(t.height == Approx(kCentre + 10.0f * facade.heightScale).margin(1e-3f));
	}
	CHECK(scales.size() > 3);
}

TEST_CASE("The mass paints its slopes with the section's wall layer", "[section_mass]")
{
	// Heights 5 on the road: layers 0 (55) and 2 (200) are painted above the floor, so the mass uses layer 2.
	const SectionAsset asset = MakeAsset({ [](float x, float) { return std::fabs(x) <= 2.0f ? 5.0f : 0.0f; } });
	SectionPathShape shape;
	Prepare(kStraight, asset, Mass(30.0f, 60.0f), shape);
	CHECK(shape.massLayer == 2);

	// The rise is steep (about 70 deg): full mass paint.
	const TerrainTarget slope = EvaluateSectionTerrain(shape, 5.0f, 100.0f, 0.0f, Identity);
	CHECK(slope.splatWeight == Approx(1.0f).margin(1e-3f));
	CHECK(slope.splat[2] == 255);

	// Flat hold zone without noise: no paint.
	CHECK(EvaluateSectionTerrain(shape, 25.0f, 100.0f, 0.0f, Identity).splatWeight == 0.0f);

	// An explicit layer wins; the road itself keeps the section's paint.
	SectionPathParams explicitLayer = Mass(30.0f, 60.0f);
	explicitLayer.massPaintLayer = 1;
	SectionPathShape shape1;
	Prepare(kStraight, asset, explicitLayer, shape1);
	CHECK(shape1.massLayer == 1);
	CHECK(EvaluateSectionTerrain(shape1, 5.0f, 100.0f, 0.0f, Identity).splat[1] == 255);
	const TerrainTarget road = EvaluateSectionTerrain(shape1, 0.0f, 100.0f, 0.0f, Identity);
	CHECK(road.splat[2] > road.splat[0]);
}

TEST_CASE("Mass and facade variety are deterministic", "[section_mass]")
{
	SectionAsset asset = MakeAsset({ [](float x, float z) { return x + z * 0.1f; }, [](float x, float) { return x * 2.0f; } });
	SectionPathParams params = Mass(40.0f, 80.0f);
	params.massNoiseAmplitude = 12.0f;
	params.massDetailAmplitude = 2.0f;
	params.mirrorCopies = true;
	params.facadeHeightJitter = 0.3f;
	const PathSpline spline = Polyline({ { 0.0f, 0.0f }, { 30.0f, 90.0f }, { -20.0f, 180.0f }, { 10.0f, 260.0f } }, false, SplineInterpolation::CatmullRom);
	SectionPathShape a;
	SectionPathShape b;
	Prepare(spline, asset, params, a);
	Prepare(spline, asset, params, b);
	for (float x = -60.0f; x <= 120.0f; x += 7.0f)
	{
		for (float z = -40.0f; z <= 300.0f; z += 7.0f)
		{
			REQUIRE(Same(EvaluateSectionTerrain(a, x, z, 3.0f, Identity), EvaluateSectionTerrain(b, x, z, 3.0f, Identity)));
		}
	}
}

TEST_CASE("The nearest-segment grid answers far queries exactly", "[section_mass]")
{
	const SectionAsset asset = MakeAsset({ Zero });
	SectionPathParams params = Mass(30.0f, 120.0f);
	for (const bool closed : { false, true })
	{
		const PathSpline spline = Polyline({ { 0.0f, 0.0f }, { 90.0f, 20.0f }, { 120.0f, 110.0f }, { 40.0f, 80.0f }, { -10.0f, 140.0f } },
			closed, SplineInterpolation::CatmullRom);
		SectionPathShape shape;
		Prepare(spline, asset, params, shape);
		REQUIRE(shape.nearestGrid.columns > 0);
		const float reach = shape.massReach;
		REQUIRE(reach > 100.0f);

		for (float x = -150.0f; x <= 280.0f; x += 3.7f)
		{
			for (float z = -140.0f; z <= 290.0f; z += 4.3f)
			{
				float d0 = 0.0f, a0 = 0.0f, d1 = 0.0f, a1 = 0.0f;
				const bool full = ProjectToPath(*shape.path, x, z, reach, d0, a0);
				const bool fast = ProjectToSectionPath(shape, x, z, reach, d1, a1);
				REQUIRE(full == fast);
				if (full)
				{
					REQUIRE(d0 == d1);
					REQUIRE(a0 == a1);
				}
			}
		}
	}
}

TEST_CASE("Every sample a massive wall writes lies in its copy rects", "[section_mass]")
{
	const SectionAsset asset = MakeAsset({ [](float x, float) { return x > 0.0f ? x : 0.0f; } });
	SectionPathParams params = Mass(20.0f, 30.0f);
	params.massNoiseAmplitude = 6.0f;
	params.mirrorCopies = true;
	params.facadeHeightJitter = 0.2f;
	const PathSpline spline = Polyline({ { 40.0f, 40.0f }, { 45.0f, 60.0f }, { 40.0f, 90.0f } }, false, SplineInterpolation::CatmullRom);
	SectionPathShape shape;
	Prepare(spline, asset, params, shape);

	std::vector<WorldRect> rects;
	GetSectionCopyRects(shape, rects);
	REQUIRE(rects.size() == shape.layout.copies.size());

	FakeTerrain terrain(16);
	TerrainContribution contribution;
	Apply(terrain, GetInfluenceRect(shape), [&shape](float x, float z, float base) { return EvaluateSectionTerrain(shape, x, z, base, Identity); },
		String(), contribution);
	REQUIRE(!contribution.outer.empty());
	REQUIRE(!contribution.splat.empty());

	const auto inRects = [&rects](const double x, const double z)
	{
		for (const WorldRect& r : rects)
		{
			if (x >= r.minX && x <= r.maxX && z >= r.minZ && z <= r.maxZ)
			{
				return true;
			}
		}
		return false;
	};
	bool beyondSection = false;
	for (const auto& e : contribution.outer)
	{
		REQUIRE(inRects(e.x, e.z));
		beyondSection = beyondSection || e.x > 52;
	}
	for (const auto& e : contribution.inner)
	{
		REQUIRE(inRects(e.x + 0.5, e.z + 0.5));
	}
	for (const auto& e : contribution.splat)
	{
		REQUIRE(inRects(e.x, e.z));
	}
	CHECK(beyondSection);
}

TEST_CASE("Path parameters can be set by name", "[section_mass]")
{
	SectionPathParams params;
	String error;
	CHECK(SetSectionPathParam(params, "massEnabled", "1", error));
	CHECK(params.massEnabled);
	CHECK(SetSectionPathParam(params, "massHeight", "72.5", error));
	CHECK(params.massHeight == 72.5f);
	CHECK(SetSectionPathParam(params, "mirrorCopies", "true", error));
	CHECK(params.mirrorCopies);
	CHECK(SetSectionPathParam(params, "massPaintLayer", "2", error));
	CHECK(params.massPaintLayer == 2);
	CHECK(SetSectionPathParam(params, "heightMode", "smoothedGround", error));
	CHECK(params.heightMode == SectionHeightMode::SmoothedGround);
	CHECK(SetSectionPathParam(params, "seed", "123456789012", error));
	CHECK(params.seed == 123456789012ull);

	const SectionPathParams kept = params;
	CHECK_FALSE(SetSectionPathParam(params, "massHeight", "tall", error));
	CHECK_FALSE(error.empty());
	CHECK_FALSE(SetSectionPathParam(params, "noSuchField", "1", error));
	CHECK_FALSE(SetSectionPathParam(params, "massPaintLayer", "7", error));
	CHECK(params.massHeight == kept.massHeight);
	CHECK(params.massPaintLayer == kept.massPaintLayer);
}

TEST_CASE("Copies are counted per role", "[section_mass]")
{
	SectionLayout layout;
	layout.copies.resize(5);
	layout.copies[1].role = SectionVariantRole::InnerCorner;
	layout.copies[3].role = SectionVariantRole::OuterCorner;
	layout.copies[4].role = SectionVariantRole::InnerCorner;
	const SectionLayoutCounts counts = CountSectionCopies(layout);
	CHECK(counts.straight == 2);
	CHECK(counts.inner == 2);
	CHECK(counts.outer == 1);
}

TEST_CASE("Corner copies are never mirrored", "[section_mass]")
{
	const SectionAsset asset = MakeAsset({ Zero });
	SectionPathParams params;
	params.mirrorCopies = true;
	SectionPathShape shape;
	Prepare(kStraight, asset, params, shape);

	uint32 straightMirrored = 0;
	for (uint64 key = 1; key <= 64; ++key)
	{
		SectionCopy copy;
		copy.spanKey = key * 0x9E3779B97F4A7C15ull;
		copy.role = SectionVariantRole::Straight;
		straightMirrored += GetCopyFacade(shape, copy).mirrored ? 1 : 0;
		copy.role = SectionVariantRole::InnerCorner;
		CHECK_FALSE(GetCopyFacade(shape, copy).mirrored);
		copy.role = SectionVariantRole::OuterCorner;
		CHECK_FALSE(GetCopyFacade(shape, copy).mirrored);
	}
	CHECK(straightMirrored > 0);
}

TEST_CASE("Height jitter keeps the copy seams continuous", "[section_mass]")
{
	const SectionAsset asset = MakeAsset({ [](float, float) { return 10.0f; } });
	SectionPathParams params;
	params.facadeHeightJitter = 0.5f;
	params.joinBlend = 1.0f;
	SectionPathShape shape;
	Prepare(kStraight, asset, params, shape);

	// A 10 m high section scaled by up to +-50 % per copy: without easing a seam steps by several metres over the 2 m
	// cross-fade. Each copy's scale eases to 1 over a quarter of its length at both ends instead.
	float steepest = 0.0f;
	float previous = EvaluateSectionTerrain(shape, 0.0f, 1.0f, kCentre, Identity).height;
	for (float z = 1.25f; z <= 199.0f; z += 0.25f)
	{
		const float h = EvaluateSectionTerrain(shape, 0.0f, z, kCentre, Identity).height;
		steepest = std::max(steepest, std::fabs(h - previous) / 0.25f);
		previous = h;
	}
	CHECK(steepest < 2.0f);

	// The middle of every copy keeps its full scale.
	for (const SectionCopy& copy : shape.layout.copies)
	{
		const TerrainTarget t = EvaluateSectionTerrain(shape, 0.0f, copy.startDistance + 10.0f, kCentre, Identity);
		CHECK(t.height == Approx(kCentre + 10.0f * GetCopyFacade(shape, copy).heightScale).margin(1e-3f));
	}
}

TEST_CASE("Section paint fades out only where the mass really overrides it", "[section_mass]")
{
	// Flat painted road (height 0, layers 0 = 55 / 2 = 200 for |x| <= 2); the mass rises from the floor edge x = -3.
	const SectionAsset asset = MakeAsset({ Zero });
	SectionPathShape plain;
	Prepare(kStraight, asset, SectionPathParams(), plain);
	SectionPathParams params = Mass(30.0f, 60.0f);
	params.massPaintLayer = 1;
	SectionPathShape shape;
	Prepare(kStraight, asset, params, shape);

	// Just inside u > 0 the mass lifts the ground by centimetres: the road keeps (nearly all of) its paint.
	const TerrainTarget foot = EvaluateSectionTerrain(shape, -1.5f, 100.0f, 0.0f, Identity);
	const TerrainTarget footPlain = EvaluateSectionTerrain(plain, -1.5f, 100.0f, 0.0f, Identity);
	CHECK(foot.splatWeight > 0.9f * footPlain.splatWeight);
	CHECK(foot.splat[2] > foot.splat[0]);
	CHECK(foot.splat[1] < 10);

	// A steep mass rising right over the road overrides it by metres: the mass paint wins.
	SectionPathParams steep = params;
	steep.massRise = 0.1f;
	SectionPathShape over;
	Prepare(kStraight, asset, steep, over);
	const TerrainTarget deep = EvaluateSectionTerrain(over, 1.5f, 100.0f, 0.0f, Identity);
	CHECK(deep.splat[1] == 255);
	CHECK(deep.splatWeight == Approx(1.0f).margin(1e-3f));

	// The paint changes smoothly across the floor edge (no seam at u = 0).
	float previous = EvaluateSectionTerrain(shape, -3.2f, 100.0f, 0.0f, Identity).splatWeight;
	for (float x = -3.1f; x <= 2.0f; x += 0.1f)
	{
		const float w = EvaluateSectionTerrain(shape, x, 100.0f, 0.0f, Identity).splatWeight;
		CHECK(std::fabs(w - previous) < 0.3f);
		previous = w;
	}
}
