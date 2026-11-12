// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/oasis.h"

#include <algorithm>
#include <cmath>
#include <map>

using namespace mmo;
using namespace mmo::pcg;

namespace
{
	PathSpline MakeCircle(float radius, uint32 count = 16)
	{
		PathSpline spline;
		spline.closed = true;
		spline.interpolation = SplineInterpolation::CatmullRom;
		for (uint32 i = 0; i < count; ++i)
		{
			const float a = 2.0f * 3.14159265f * static_cast<float>(i) / static_cast<float>(count);
			spline.points.push_back(SplinePoint{ radius * std::cos(a), radius * std::sin(a), 0.0f });
		}
		return spline;
	}

	OasisAssets MakeAssets()
	{
		OasisAssets a;
		a.cliffs = { "cliff_a", "cliff_b" };
		a.shelves = { "shelf_a", "shelf_b" };
		a.clumps = { "clump_a", "clump_b" };
		a.palms = { "palm_a", "palm_b" };
		a.bushes = { "bush_a", "bush_b" };
		a.waterPlants = { "reed_a", "reed_b" };
		return a;
	}

	float Flat(float, float)
	{
		return 20.0f;
	}

	float ZeroSlope(float, float)
	{
		return 0.0f;
	}

	/// Ground matching the oasis: the evaluated terrain height where the oasis has weight.
	std::function<float(float, float)> OasisGround(const OasisShape& shape)
	{
		return [&shape](float x, float z)
		{
			const TerrainTarget t = EvaluateTerrain(shape, x, z, 20.0f);
			return t.heightWeight > 0.0f ? 20.0f + (t.height - 20.0f) * t.heightWeight : 20.0f;
		};
	}

	void Build(const PathSpline& loop, uint64 seed, const OasisAssets& assets, std::vector<Placement>& out,
		const std::function<float(float, float)>& ground = Flat)
	{
		OasisShape shape;
		REQUIRE(PrepareOasis(loop, seed, OasisParams(), Flat, shape) == OasisError::None);
		BuildPlacements(shape, assets, ground, ZeroSlope, out);
	}

	bool Same(const Placement& a, const Placement& b)
	{
		return a.key == b.key && a.asset == b.asset && a.kind == b.kind && a.collides == b.collides
			&& a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z
			&& a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y && a.rotation.z == b.rotation.z
			&& a.rotation.w == b.rotation.w && a.scale.x == b.scale.x;
	}

	bool IsRock(const Placement& p)
	{
		return p.kind == PlacementKind::Entity;
	}
}

TEST_CASE("Oasis placements are deterministic", "[oasis_placement]")
{
	std::vector<Placement> a, b;
	Build(MakeCircle(40.0f), 11, MakeAssets(), a);
	Build(MakeCircle(40.0f), 11, MakeAssets(), b);

	REQUIRE_FALSE(a.empty());
	REQUIRE(a.size() == b.size());
	for (size_t i = 0; i < a.size(); ++i)
	{
		CHECK(Same(a[i], b[i]));
	}
}

TEST_CASE("Oasis placements change with the seed", "[oasis_placement]")
{
	std::vector<Placement> a, b;
	Build(MakeCircle(40.0f), 11, MakeAssets(), a);
	Build(MakeCircle(40.0f), 12, MakeAssets(), b);

	bool differs = a.size() != b.size();
	for (size_t i = 0; !differs && i < a.size(); ++i)
	{
		differs = !Same(a[i], b[i]);
	}
	CHECK(differs);
}

TEST_CASE("Oasis output is sorted with unique keys", "[oasis_placement]")
{
	std::vector<Placement> out;
	Build(MakeCircle(40.0f), 11, MakeAssets(), out);

	REQUIRE(out.size() > 2);
	for (size_t i = 1; i < out.size(); ++i)
	{
		CHECK(out[i - 1].key < out[i].key);
	}
}

TEST_CASE("Oasis rim rocks surround the loop", "[oasis_placement]")
{
	OasisShape shape;
	REQUIRE(PrepareOasis(MakeCircle(40.0f), 11, OasisParams(), Flat, shape) == OasisError::None);
	std::vector<Placement> out;
	BuildPlacements(shape, MakeAssets(), Flat, ZeroSlope, out);

	const float expected = std::floor(shape.path->GetLength() / shape.params.rockSpacing);
	const auto rocks = static_cast<float>(std::count_if(out.begin(), out.end(), IsRock));
	CHECK(std::abs(rocks - expected) <= 1.0f);

	for (const Placement& p : out)
	{
		if (!IsRock(p))
		{
			continue;
		}
		const float d = shape.path->SignedDistance(p.position.x, p.position.z);
		CHECK(d >= 0.5f - 0.001f);
		CHECK(d <= 3.5f + 0.001f);
		CHECK(p.collides);
	}
}

TEST_CASE("Oasis rim rocks sink by family", "[oasis_placement]")
{
	OasisParams params;
	params.rockSink = 6.0f;
	params.shelfSink = 2.0f;
	params.clumpSink = 0.25f;
	OasisShape shape;
	REQUIRE(PrepareOasis(MakeCircle(40.0f), 11, params, Flat, shape) == OasisError::None);
	std::vector<Placement> out;
	BuildPlacements(shape, MakeAssets(), Flat, ZeroSlope, out);

	uint32 seen[3] = { 0, 0, 0 };
	for (const Placement& p : out)
	{
		if (!IsRock(p))
		{
			continue;
		}

		const char family = p.asset[2]; // cl(i)ff, sh(e)lf, cl(u)mp
		if (family == 'i')
		{
			CHECK(p.position.y == Approx(20.0f - 6.0f));
			++seen[0];
		}
		else if (family == 'e')
		{
			CHECK(p.position.y == Approx(20.0f - 2.0f));
			++seen[1];
		}
		else
		{
			CHECK(p.position.y == Approx(20.0f - 0.25f));
			++seen[2];
		}
	}
	CHECK(seen[0] > 0);
	CHECK(seen[1] > 0);
	CHECK(seen[2] > 0);
}

TEST_CASE("Oasis vegetation stays dry and inside", "[oasis_placement]")
{
	OasisShape shape;
	REQUIRE(PrepareOasis(MakeCircle(40.0f), 11, OasisParams(), Flat, shape) == OasisError::None);
	const OasisAssets assets = MakeAssets();
	std::vector<Placement> out;
	BuildPlacements(shape, assets, OasisGround(shape), ZeroSlope, out);

	uint32 palms = 0, bushes = 0;
	for (const Placement& p : out)
	{
		if (IsRock(p))
		{
			continue;
		}
		const float d = shape.path->SignedDistance(p.position.x, p.position.z);
		CHECK(d < 0.0f);

		const bool isPalm = p.asset.rfind("palm", 0) == 0;
		const bool isBush = p.asset.rfind("bush", 0) == 0;
		if (isPalm)
		{
			++palms;
			CHECK(p.collides);
		}
		if (isBush)
		{
			++bushes;
			CHECK_FALSE(p.collides);
		}
		if (isPalm || isBush)
		{
			CHECK(d > -shape.params.waterInset);
			CHECK(p.position.y >= GetWaterLevel(shape));
		}
		CHECK(p.kind == PlacementKind::Foliage);
	}
	CHECK(palms > 0);
	CHECK(bushes > 0);
}

TEST_CASE("Oasis vegetation respects the slope limit", "[oasis_placement]")
{
	OasisShape shape;
	REQUIRE(PrepareOasis(MakeCircle(40.0f), 11, OasisParams(), Flat, shape) == OasisError::None);
	std::vector<Placement> out;
	BuildPlacements(shape, MakeAssets(), Flat, [](float, float) { return 45.0f; }, out);

	for (const Placement& p : out)
	{
		CHECK(IsRock(p));
	}
}

TEST_CASE("Oasis placements are local to the control point that moved", "[oasis_placement]")
{
	const PathSpline before = MakeCircle(40.0f);
	PathSpline after = before;
	after.points[4].x *= 1.0f + 5.0f / 40.0f;
	after.points[4].z *= 1.0f + 5.0f / 40.0f;

	OasisShape shapeBefore, shapeAfter;
	REQUIRE(PrepareOasis(before, 11, OasisParams(), Flat, shapeBefore) == OasisError::None);
	REQUIRE(PrepareOasis(after, 11, OasisParams(), Flat, shapeAfter) == OasisError::None);

	std::vector<Placement> a, b;
	BuildPlacements(shapeBefore, MakeAssets(), Flat, ZeroSlope, a);
	BuildPlacements(shapeAfter, MakeAssets(), Flat, ZeroSlope, b);

	std::map<uint64, const Placement*> byKey;
	for (const Placement& p : b)
	{
		byKey[p.key] = &p;
	}

	const OasisParams& params = shapeBefore.params;
	const float radius = params.rimWidth + params.outerFalloff + params.palmCell;
	const SplinePoint& oldPoint = before.points[4];
	const SplinePoint& newPoint = after.points[4];

	uint32 checkedVegetation = 0;
	uint32 checkedRocks = 0;
	const SampledPath& path = *shapeBefore.path;
	for (const Placement& p : a)
	{
		if (IsRock(p))
		{
			// Rocks are checked below by arc distance.
			continue;
		}

		const float dOld = std::hypot(p.position.x - oldPoint.x, p.position.z - oldPoint.z);
		const float dNew = std::hypot(p.position.x - newPoint.x, p.position.z - newPoint.z);
		if (dOld <= radius || dNew <= radius)
		{
			continue;
		}
		const auto it = byKey.find(p.key);
		REQUIRE(it != byKey.end());
		CHECK(Same(p, *it->second));
		++checkedVegetation;
	}

	// Rim rocks by arc distance: steps whose distance lies before the start of control segment 2.
	const float rockSpacing = params.rockSpacing;
	for (int64 i = 0; static_cast<float>(i) * rockSpacing < path.GetLength(); ++i)
	{
		const float distance = static_cast<float>(i) * rockSpacing;
		if (path.SegmentAtDistance(distance) >= 2)
		{
			break;
		}
		const SplineSample s = path.SampleAtDistance(distance);
		// Find the rock of this step: nearest placement within the rim offset band.
		const Placement* match = nullptr;
		for (const Placement& p : a)
		{
			if (IsRock(p) && std::hypot(p.position.x - s.x, p.position.z - s.z) <= 3.6f)
			{
				match = &p;
				break;
			}
		}
		REQUIRE(match != nullptr);
		const auto it = byKey.find(match->key);
		REQUIRE(it != byKey.end());
		CHECK(Same(*match, *it->second));
		++checkedRocks;
	}

	CHECK(checkedVegetation > 0);
	CHECK(checkedRocks > 0);
}

TEST_CASE("Oasis water plants grow on the land side of the pool edge", "[oasis_placement]")
{
	OasisShape shape;
	REQUIRE(PrepareOasis(MakeCircle(40.0f), 11, OasisParams(), Flat, shape) == OasisError::None);
	std::vector<Placement> out;
	BuildPlacements(shape, MakeAssets(), Flat, ZeroSlope, out);

	const float edge = -shape.params.waterInset;
	uint32 plants = 0;
	for (const Placement& p : out)
	{
		if (p.asset.rfind("reed", 0) != 0)
		{
			continue;
		}
		++plants;
		const float d = shape.path->SignedDistance(p.position.x, p.position.z);
		CHECK(d >= edge);
		CHECK(d <= edge + shape.params.waterPlantBand);
		CHECK(p.position.y >= GetWaterLevel(shape));
		CHECK(p.kind == PlacementKind::Foliage);
		CHECK_FALSE(p.collides);
	}
	CHECK(plants > 0);
}

TEST_CASE("Oasis places nothing under water", "[oasis_placement]")
{
	OasisShape shape;
	REQUIRE(PrepareOasis(MakeCircle(40.0f), 11, OasisParams(), Flat, shape) == OasisError::None);
	const float waterLevel = GetWaterLevel(shape);

	// Two trench rings (d around -5, and across the water plant band at d around -8.5) sink below the water level.
	const auto trenchGround = [&shape](float x, float z)
	{
		const float d = shape.path->SignedDistance(x, z);
		return ((d > -6.0f && d < -4.0f) || (d > -9.5f && d < -7.5f)) ? 10.0f : 20.0f;
	};

	std::vector<Placement> dry, trench;
	BuildPlacements(shape, MakeAssets(), Flat, ZeroSlope, dry);
	BuildPlacements(shape, MakeAssets(), trenchGround, ZeroSlope, trench);

	uint32 inTrenchWithoutIt = 0;
	for (const Placement& p : dry)
	{
		const float d = shape.path->SignedDistance(p.position.x, p.position.z);
		if (!IsRock(p) && d > -6.0f && d < -4.0f)
		{
			++inTrenchWithoutIt;
		}
	}
	REQUIRE(inTrenchWithoutIt > 0);

	uint32 plantsInRingWithoutIt = 0;
	for (const Placement& p : dry)
	{
		const float d = shape.path->SignedDistance(p.position.x, p.position.z);
		if (p.asset.rfind("reed", 0) == 0 && d > -9.5f && d < -7.5f)
		{
			++plantsInRingWithoutIt;
		}
	}
	REQUIRE(plantsInRingWithoutIt > 0);

	uint32 foliage = 0;
	for (const Placement& p : trench)
	{
		if (IsRock(p))
		{
			continue;
		}
		++foliage;
		CHECK(trenchGround(p.position.x, p.position.z) >= waterLevel);
		CHECK(p.position.y >= waterLevel);
	}
	CHECK(foliage > 0);
}
