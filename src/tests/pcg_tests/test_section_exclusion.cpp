// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/section_asset.h"
#include "pcg/section_path.h"

#include <algorithm>
#include <cmath>
#include <map>

using namespace mmo;
using namespace mmo::pcg;

namespace
{
	/// A run of `count` pieces of 4 m (pivot in the middle) at lateral offset x, footprints from startZ on.
	SectionRun MakeFenceRun(const float x, const float startZ, const uint32 count)
	{
		constexpr float length = 4.0f;
		SectionRun run;
		run.name = "Fence";
		run.keepChance = 1.0f;
		run.x = x;
		run.startZ = startZ;
		run.endZ = startZ + length * static_cast<float>(count);
		for (uint32 i = 0; i < count; ++i)
		{
			SectionRunPiece piece;
			piece.placement.key = 9000 + i;
			piece.placement.kind = PlacementKind::Entity;
			piece.placement.asset = "Props/fence.hmsh";
			piece.placement.position = Vector3(x, 0.0f, startZ + length * (static_cast<float>(i) + 0.5f));
			piece.length = length;
			piece.startOffset = -0.5f * length;
			run.pieces.push_back(piece);
		}
		return run;
	}

	SectionPlacement MakeProp(const uint64 key, const float x, const float z)
	{
		SectionPlacement p;
		p.placement.key = key;
		p.placement.kind = PlacementKind::Entity;
		p.placement.asset = "rock";
		p.placement.position = Vector3(x, 0.0f, z);
		return p;
	}

	/// Section L 20, halfWidth 3, sideFalloff 4, one flat variant with the given runs and props.
	SectionAsset MakeAsset(const std::vector<SectionRun>& runs, const std::vector<SectionPlacement>& props = {}, const std::vector<PropRule>& rules = {})
	{
		SectionAsset asset;
		asset.length = 20.0f;
		asset.halfWidth = 3.0f;
		asset.sideFalloff = 4.0f;
		asset.rules = rules;
		SectionVariant variant;
		variant.runs = runs;
		variant.placements = props;
		asset.variants.push_back(variant);
		return asset;
	}

	PathSpline MakePath(const std::vector<std::pair<float, float>>& points, const bool closed = false)
	{
		PathSpline spline;
		spline.interpolation = SplineInterpolation::Linear;
		spline.closed = closed;
		for (const auto& point : points)
		{
			SplinePoint p;
			p.x = point.first;
			p.z = point.second;
			spline.points.push_back(p);
		}
		return spline;
	}

	SectionPathShape Prepare(const PathSpline& spline, const SectionAsset& asset)
	{
		std::vector<uint64> ids;
		for (size_t i = 0; i < spline.points.size(); ++i)
		{
			ids.push_back(500 + i);
		}
		SectionPathShape shape;
		String error;
		REQUIRE(PrepareSectionPath(spline, ids, asset, SectionPathParams(), {}, shape, error));
		return shape;
	}

	std::vector<Placement> Build(const SectionPathShape& shape, const PlacementExcludeFunction& excluded)
	{
		std::vector<Placement> out;
		BuildSectionPlacements(shape, [](float, float) { return 3.0f; }, out, excluded);
		return out;
	}

	bool Same(const Placement& a, const Placement& b)
	{
		return a.key == b.key && a.asset == b.asset && a.position == b.position && a.rotation == b.rotation && a.scale == b.scale;
	}
}

TEST_CASE("an exclusion removes run pieces and leaves the others identical", "[section_exclusion]")
{
	const SectionPathShape shape = Prepare(MakePath({ { 0.0f, 0.0f }, { 0.0f, 40.0f }, { 0.0f, 60.0f } }), MakeAsset({ MakeFenceRun(-4.6f, 0.0f, 5) }));
	const std::vector<Placement> all = Build(shape, {});
	REQUIRE(all.size() == 15);

	const auto inGate = [](const float, const float z) { return z >= 18.0f && z <= 30.0f; };
	const std::vector<Placement> cut = Build(shape, inGate);

	// The pivot is the middle of the 4 m footprint on this straight run, so its z is the footprint midpoint.
	std::vector<Placement> expected;
	for (const Placement& p : all)
	{
		if (!inGate(p.position.x, p.position.z))
		{
			expected.push_back(p);
		}
	}
	REQUIRE(expected.size() < all.size());
	REQUIRE(expected.size() > 0);
	REQUIRE(cut.size() == expected.size());
	for (size_t i = 0; i < cut.size(); ++i)
	{
		CHECK(Same(cut[i], expected[i]));
	}
}

TEST_CASE("an exclusion removes single placements by their jittered position", "[section_exclusion]")
{
	PropRule rule;
	rule.asset = "rock";
	rule.positionJitter = 2.0f;
	std::vector<SectionPlacement> props;
	for (uint64 i = 0; i < 8; ++i)
	{
		props.push_back(MakeProp(100 + i, 1.0f, 1.0f + 2.0f * static_cast<float>(i)));
	}
	const SectionPathShape shape = Prepare(MakePath({ { 0.0f, 0.0f }, { 0.0f, 60.0f } }), MakeAsset({}, props, { rule }));
	const std::vector<Placement> all = Build(shape, {});
	REQUIRE(all.size() == 24);

	// Jittered x differs from the authored 1 m; exclude by the final x only.
	const auto right = [](const float x, const float) { return x > 1.0f; };
	const std::vector<Placement> cut = Build(shape, right);
	std::vector<Placement> expected;
	for (const Placement& p : all)
	{
		if (!(p.position.x > 1.0f))
		{
			expected.push_back(p);
		}
	}
	REQUIRE(expected.size() > 0);
	REQUIRE(expected.size() < all.size());
	REQUIRE(cut.size() == expected.size());
	for (size_t i = 0; i < cut.size(); ++i)
	{
		CHECK(Same(cut[i], expected[i]));
	}
}

TEST_CASE("an exclusion covering everything gives no placements", "[section_exclusion]")
{
	const SectionPathShape shape = Prepare(MakePath({ { 0.0f, 0.0f }, { 0.0f, 40.0f } }), MakeAsset({ MakeFenceRun(-4.6f, 0.0f, 5) }, { MakeProp(100, 1.0f, 5.0f) }));
	REQUIRE_FALSE(Build(shape, {}).empty());
	CHECK(Build(shape, [](float, float) { return true; }).empty());
}

TEST_CASE("an empty exclude function changes nothing", "[section_exclusion]")
{
	PropRule rule;
	rule.asset = "rock";
	rule.positionJitter = 1.0f;
	rule.yawJitterDeg = 30.0f;
	rule.scaleJitter = 0.2f;
	const SectionAsset asset = MakeAsset({ MakeFenceRun(-4.6f, 0.0f, 5) }, { MakeProp(100, 2.0f, 4.0f), MakeProp(101, -2.0f, 12.0f) }, { rule });
	const SectionPathShape shape = Prepare(MakePath({ { 0.0f, 0.0f }, { 40.0f, 0.0f }, { 40.0f, 40.0f }, { 0.0f, 40.0f } }, true), asset);

	std::vector<Placement> without;
	BuildSectionPlacements(shape, [](float, float) { return 3.0f; }, without);
	const std::vector<Placement> with = Build(shape, PlacementExcludeFunction());
	REQUIRE_FALSE(without.empty());
	REQUIRE(with.size() == without.size());
	for (size_t i = 0; i < with.size(); ++i)
	{
		CHECK(Same(with[i], without[i]));
	}
}
