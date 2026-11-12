// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/section_path.h"

#include <algorithm>
#include <cmath>
#include <set>

using namespace mmo;
using namespace mmo::pcg;

namespace
{
	constexpr float kWallHeight = 10.0f;
	constexpr float kRunPieceLength = 3.0f;

	/// One wall variant: L 20, floor (height 0) for x <= 0, wall (kWallHeight) for x >= 1. One prop on the floor side.
	SectionVariant MakeWallVariant(const SectionVariantRole role, const uint64 propKey, const bool withRun)
	{
		SectionVariant v;
		v.role = role;
		v.heights.spacing = 1.0f;
		v.heights.originX = -8;
		v.heights.originZ = 0;
		v.heights.width = 17;
		v.heights.depth = 21;
		for (uint32 j = 0; j < 21; ++j)
		{
			for (uint32 i = 0; i < 17; ++i)
			{
				const int32 x = -8 + static_cast<int32>(i);
				v.heights.values.push_back(x >= 1 ? kWallHeight : 0.0f);
			}
		}

		SectionPlacement prop;
		prop.placement.key = propKey;
		prop.placement.asset = "Props/rock.hmsh";
		prop.placement.position = Vector3(-5.0f, 0.0f, 10.0f);
		v.placements.push_back(prop);

		if (withRun)
		{
			SectionRun run;
			run.name = "fence";
			run.x = -2.0f;
			run.startZ = 0.0f;
			run.endZ = 20.0f;
			SectionRunPiece piece;
			piece.placement.key = 900;
			piece.placement.asset = "Props/fence.hmsh";
			piece.length = kRunPieceLength;
			piece.startOffset = 0.0f;
			run.pieces.push_back(piece);
			v.runs.push_back(run);
		}
		return v;
	}

	SectionAsset MakeWall(const bool inner, const bool outer, const bool straightRun = false)
	{
		SectionAsset asset;
		asset.name = "wall";
		asset.length = 20.0f;
		asset.halfWidth = 3.0f;
		asset.sideFalloff = 4.0f;
		asset.cornerAngleDeg = 35.0f;
		asset.variants.push_back(MakeWallVariant(SectionVariantRole::Straight, 100, straightRun));
		if (inner)
		{
			asset.variants.push_back(MakeWallVariant(SectionVariantRole::InnerCorner, 101, false));
		}
		if (outer)
		{
			asset.variants.push_back(MakeWallVariant(SectionVariantRole::OuterCorner, 102, false));
		}
		return asset;
	}

	PathSpline Loop(const std::vector<std::pair<float, float>>& points)
	{
		PathSpline spline;
		spline.interpolation = SplineInterpolation::Linear;
		spline.closed = true;
		for (const auto& p : points)
		{
			SplinePoint sp;
			sp.x = p.first;
			sp.z = p.second;
			spline.points.push_back(sp);
		}
		return spline;
	}

	std::vector<uint64> Ids(const PathSpline& spline)
	{
		std::vector<uint64> ids;
		for (size_t i = 0; i < spline.points.size(); ++i)
		{
			ids.push_back(1000 + 7 * i);
		}
		return ids;
	}

	void Prepare(const PathSpline& spline, const SectionAsset& asset, const SectionPathParams& params, SectionPathShape& shape)
	{
		String error;
		REQUIRE(PrepareSectionPath(spline, Ids(spline), asset, params, {}, shape, error));
	}

	size_t CountRole(const SectionLayout& layout, const SectionVariantRole role)
	{
		return static_cast<size_t>(std::count_if(layout.copies.begin(), layout.copies.end(),
			[role](const SectionCopy& c) { return c.role == role; }));
	}

	const std::vector<std::pair<float, float>> kSquareCcw = { { 0.0f, 0.0f }, { 100.0f, 0.0f }, { 100.0f, 100.0f }, { 0.0f, 100.0f } };
	const std::vector<std::pair<float, float>> kSquareCw = { { 0.0f, 0.0f }, { 0.0f, 100.0f }, { 100.0f, 100.0f }, { 100.0f, 0.0f } };
}

TEST_CASE("A square loop gets an inner corner at every point and its floor inside, either winding", "[section_loops]")
{
	const SectionAsset asset = MakeWall(true, true);
	for (const auto* points : { &kSquareCcw, &kSquareCw })
	{
		SectionPathShape shape;
		Prepare(Loop(*points), asset, SectionPathParams(), shape);

		REQUIRE(shape.layout.pointRoles.size() == 4);
		for (const SectionVariantRole role : shape.layout.pointRoles)
		{
			CHECK(role == SectionVariantRole::InnerCorner);
		}
		CHECK(CountRole(shape.layout, SectionVariantRole::InnerCorner) == 4);
		CHECK(CountRole(shape.layout, SectionVariantRole::OuterCorner) == 0);

		// 100 m sides less two half corners = 80 m = 4 straight copies of 20 m per side.
		CHECK(CountRole(shape.layout, SectionVariantRole::Straight) == 16);
		for (const SectionCopy& copy : shape.layout.copies)
		{
			CHECK(copy.stretchedLength == Approx(20.0f).margin(1e-3f));
			CHECK(shape.asset.variants[GetVariant(shape, copy)].role == copy.role);
		}

		// The floor (-X side) faces the interior: low just inside each side, the wall just outside.
		const float probes[4][4] = {
			{ 50.0f, 2.0f, 50.0f, -2.0f },
			{ 98.0f, 50.0f, 102.0f, 50.0f },
			{ 50.0f, 98.0f, 50.0f, 102.0f },
			{ 2.0f, 50.0f, -2.0f, 50.0f } };
		for (const auto& p : probes)
		{
			const TerrainTarget inside = EvaluateSectionTerrain(shape, p[0], p[1], 0.0f, {});
			const TerrainTarget outside = EvaluateSectionTerrain(shape, p[2], p[3], 0.0f, {});
			CHECK(inside.heightWeight == Approx(1.0f));
			CHECK(outside.heightWeight == Approx(1.0f));
			CHECK(inside.height == Approx(0.0f).margin(1e-4f));
			CHECK(outside.height == Approx(kWallHeight).margin(1e-4f));
		}

		// Corners are covered too, including the one at the loop start.
		const TerrainTarget corner = EvaluateSectionTerrain(shape, -2.0f, -2.0f, 0.0f, {});
		CHECK(corner.heightWeight > 0.0f);
		CHECK(corner.height == Approx(kWallHeight).margin(1e-3f));
	}
}

TEST_CASE("Without floorInside a clockwise loop is laid in point order", "[section_loops]")
{
	SectionPathParams params;
	params.floorInside = false;
	SectionPathShape shape;
	Prepare(Loop(kSquareCw), MakeWall(true, true), params, shape);

	CHECK_FALSE(shape.reversed);
	const TerrainTarget inside = EvaluateSectionTerrain(shape, 50.0f, 2.0f, 0.0f, {});
	CHECK(inside.height == Approx(kWallHeight).margin(1e-4f));

	// Seen from the floor side (outside) every point is now a reflex vertex.
	for (const SectionVariantRole role : shape.layout.pointRoles)
	{
		CHECK(role == SectionVariantRole::OuterCorner);
	}
}

TEST_CASE("An L-shaped loop gets exactly one outer corner, at the reflex vertex", "[section_loops]")
{
	const std::vector<std::pair<float, float>> lShape = { { 0.0f, 0.0f }, { 100.0f, 0.0f }, { 100.0f, 50.0f }, { 50.0f, 50.0f },
		{ 50.0f, 100.0f }, { 0.0f, 100.0f } };
	for (int reverse = 0; reverse < 2; ++reverse)
	{
		std::vector<std::pair<float, float>> points = lShape;
		if (reverse == 1)
		{
			std::reverse(points.begin(), points.end());
		}

		SectionPathShape shape;
		Prepare(Loop(points), MakeWall(true, true), SectionPathParams(), shape);
		CHECK(CountRole(shape.layout, SectionVariantRole::OuterCorner) == 1);
		CHECK(CountRole(shape.layout, SectionVariantRole::InnerCorner) == 5);

		// pointRoles is in the caller's point order, whatever the laying direction.
		const size_t reflex = reverse == 1 ? 2 : 3;
		REQUIRE(shape.layout.pointRoles.size() == 6);
		for (size_t i = 0; i < 6; ++i)
		{
			CHECK(shape.layout.pointRoles[i] == (i == reflex ? SectionVariantRole::OuterCorner : SectionVariantRole::InnerCorner));
		}
	}
}

TEST_CASE("Two sharp points closer than a corner piece get only one corner", "[section_loops]")
{
	// A 100 x 12 m rectangle: the two points of each short side are 12 m apart, the section is 20 m long.
	SectionPathShape shape;
	Prepare(Loop({ { 0.0f, 0.0f }, { 100.0f, 0.0f }, { 100.0f, 12.0f }, { 0.0f, 12.0f } }), MakeWall(true, true), SectionPathParams(), shape);

	const std::vector<SectionVariantRole>& roles = shape.layout.pointRoles;
	REQUIRE(roles.size() == 4);
	CHECK(CountRole(shape.layout, SectionVariantRole::InnerCorner) == 2);
	CHECK(((roles[1] != SectionVariantRole::Straight) != (roles[2] != SectionVariantRole::Straight)));
	CHECK(((roles[3] != SectionVariantRole::Straight) != (roles[0] != SectionVariantRole::Straight)));

	// The 2 m left of each short side next to its corner is no sliver copy: the corner piece is stretched over it
	// (22 m), the short sides get no straight copy, the long sides 4 (between two corners) and 5 copies.
	const std::vector<SectionCopy>& copies = shape.layout.copies;
	CHECK(CountRole(shape.layout, SectionVariantRole::Straight) == 9);
	for (const SectionCopy& copy : copies)
	{
		CHECK(copy.stretchedLength >= 10.0f);
		if (copy.role != SectionVariantRole::Straight)
		{
			CHECK(copy.stretchedLength == Approx(22.0f).margin(1e-3f));
		}
	}

	// Copies tile the loop without overlap.
	for (size_t i = 0; i + 1 < copies.size(); ++i)
	{
		CHECK(copies[i].stretchedLength > 0.0f);
		CHECK(copies[i].startDistance + copies[i].stretchedLength == Approx(copies[i + 1].startDistance).margin(1e-3f));
	}
	CHECK(copies.back().startDistance + copies.back().stretchedLength
		== Approx(copies.front().startDistance + shape.path->GetLength()).margin(1e-3f));
}

TEST_CASE("A short straight remainder before a corner on an open path stretches the corner", "[section_loops]")
{
	// 14 m to a sharp left turn, then 100 m: the corner piece takes [4, 24]; the 4 m before it would be a sliver.
	PathSpline spline = Loop({ { 0.0f, 0.0f }, { 14.0f, 0.0f }, { 14.0f, 100.0f } });
	spline.closed = false;
	SectionPathShape shape;
	Prepare(spline, MakeWall(true, true), SectionPathParams(), shape);

	const std::vector<SectionCopy>& copies = shape.layout.copies;
	REQUIRE(shape.layout.pointRoles.size() == 3);
	CHECK(shape.layout.pointRoles[1] == SectionVariantRole::InnerCorner);
	REQUIRE(!copies.empty());
	CHECK(copies.front().role == SectionVariantRole::InnerCorner);
	CHECK(copies.front().span == 1);
	CHECK(copies.front().startDistance == Approx(0.0f).margin(1e-3f));
	CHECK(copies.front().stretchedLength == Approx(24.0f).margin(1e-3f));

	// The 90 m after the corner: 5 copies of 18 m, tiling to the path end.
	CHECK(CountRole(shape.layout, SectionVariantRole::Straight) == 5);
	for (size_t i = 0; i + 1 < copies.size(); ++i)
	{
		CHECK(copies[i].startDistance + copies[i].stretchedLength == Approx(copies[i + 1].startDistance).margin(1e-3f));
	}
	CHECK(copies.back().startDistance + copies.back().stretchedLength == Approx(shape.path->GetLength()).margin(1e-3f));

	// The stretched corner covers the path start.
	float localZ = 0.0f;
	const SectionCopy* first = FindCopy(shape.layout, 1.0f, 20.0f, localZ);
	REQUIRE(first != nullptr);
	CHECK(first->role == SectionVariantRole::InnerCorner);
	CHECK(localZ == Approx(20.0f / 24.0f).margin(1e-3f));
}

TEST_CASE("A corner's key does not depend on the remainder it absorbs", "[section_loops]")
{
	// Moving the first point changes the remainder before the corner; the corner keeps its key.
	const SectionAsset asset = MakeWall(true, true);
	uint64 keys[2] = {};
	const float starts[2] = { 0.0f, 3.0f };
	for (int k = 0; k < 2; ++k)
	{
		PathSpline spline = Loop({ { starts[k], 0.0f }, { 14.0f, 0.0f }, { 14.0f, 100.0f } });
		spline.closed = false;
		SectionPathShape shape;
		Prepare(spline, asset, SectionPathParams(), shape);
		REQUIRE(!shape.layout.copies.empty());
		CHECK(shape.layout.copies.front().role == SectionVariantRole::InnerCorner);
		keys[k] = shape.layout.copies.front().spanKey;
	}
	CHECK(keys[0] == keys[1]);
}

TEST_CASE("A loop without corner variants lays by bending only", "[section_loops]")
{
	SectionPathShape shape;
	Prepare(Loop(kSquareCcw), MakeWall(false, false), SectionPathParams(), shape);

	CHECK(shape.layout.copies.size() == 20);
	CHECK(CountRole(shape.layout, SectionVariantRole::Straight) == 20);
	for (const SectionVariantRole role : shape.layout.pointRoles)
	{
		CHECK(role == SectionVariantRole::Straight);
	}
	CHECK(shape.layout.copies.front().startDistance == 0.0f);

	const TerrainTarget corner = EvaluateSectionTerrain(shape, -2.0f, -2.0f, 0.0f, {});
	CHECK(corner.heightWeight > 0.0f);

	std::vector<Placement> props;
	BuildSectionPlacements(shape, {}, props);
	CHECK(props.size() == 20);
}

TEST_CASE("A section without straight variants is refused", "[section_loops]")
{
	SectionAsset asset = MakeWall(true, false);
	asset.variants.erase(asset.variants.begin());
	SectionPathShape shape;
	String error;
	const PathSpline spline = Loop(kSquareCcw);
	CHECK_FALSE(PrepareSectionPath(spline, Ids(spline), asset, SectionPathParams(), {}, shape, error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("Section loops are deterministic", "[section_loops]")
{
	const std::vector<std::pair<float, float>> points = { { 0.0f, 0.0f }, { 100.0f, 0.0f }, { 100.0f, 50.0f }, { 50.0f, 50.0f },
		{ 50.0f, 100.0f }, { 0.0f, 100.0f } };
	SectionPathShape a;
	SectionPathShape b;
	Prepare(Loop(points), MakeWall(true, true, true), SectionPathParams(), a);
	Prepare(Loop(points), MakeWall(true, true, true), SectionPathParams(), b);

	REQUIRE(a.layout.copies.size() == b.layout.copies.size());
	for (size_t i = 0; i < a.layout.copies.size(); ++i)
	{
		CHECK(a.layout.copies[i].spanKey == b.layout.copies[i].spanKey);
		CHECK(a.layout.copies[i].startDistance == b.layout.copies[i].startDistance);
		CHECK(a.layout.copies[i].role == b.layout.copies[i].role);
	}

	std::vector<Placement> pa;
	std::vector<Placement> pb;
	BuildSectionPlacements(a, {}, pa);
	BuildSectionPlacements(b, {}, pb);
	REQUIRE(pa.size() == pb.size());
	std::set<uint64> keys;
	for (size_t i = 0; i < pa.size(); ++i)
	{
		CHECK(pa[i].key == pb[i].key);
		CHECK(pa[i].position == pb[i].position);
		keys.insert(pa[i].key);
	}
	CHECK(keys.size() == pa.size());
}

TEST_CASE("Moving one loop point changes only its corner and its two spans", "[section_loops]")
{
	const SectionAsset asset = MakeWall(true, true);
	SectionPathShape before;
	Prepare(Loop(kSquareCcw), asset, SectionPathParams(), before);

	std::vector<std::pair<float, float>> moved = kSquareCcw;
	moved[2] = { 108.0f, 96.0f };
	SectionPathShape after;
	Prepare(Loop(moved), asset, SectionPathParams(), after);

	REQUIRE(after.layout.pointRoles.size() == 4);
	for (const SectionVariantRole role : after.layout.pointRoles)
	{
		CHECK(role == SectionVariantRole::InnerCorner);
	}

	// Spans 1 and 2 touch point 2; the corner at point 2 is its own.
	const auto touched = [](const SectionCopy& c)
	{
		return c.role == SectionVariantRole::Straight ? (c.span == 1 || c.span == 2) : c.span == 2;
	};

	size_t compared = 0;
	for (const SectionCopy& b : before.layout.copies)
	{
		if (touched(b))
		{
			continue;
		}

		const auto it = std::find_if(after.layout.copies.begin(), after.layout.copies.end(),
			[&b](const SectionCopy& a) { return a.spanKey == b.spanKey && a.copy == b.copy && a.role == b.role; });
		REQUIRE(it != after.layout.copies.end());
		CHECK(it->span == b.span);
		CHECK(it->stretchedLength == Approx(b.stretchedLength).margin(1e-4f));
		CHECK(GetVariant(after, *it) == GetVariant(before, b));
		++compared;
	}
	CHECK(compared == 3 + 8);

	// The props of the untouched copies keep their keys.
	std::vector<Placement> propsBefore;
	std::vector<Placement> propsAfter;
	BuildSectionPlacements(before, {}, propsBefore);
	BuildSectionPlacements(after, {}, propsAfter);
	std::set<uint64> afterKeys;
	for (const Placement& p : propsAfter)
	{
		afterKeys.insert(p.key);
	}
	for (const SectionCopy& b : before.layout.copies)
	{
		if (touched(b))
		{
			continue;
		}
		const uint32 variant = GetVariant(before, b);
		const SectionPlacement& source = before.asset.variants[variant].placements.front();
		CHECK(afterKeys.count(SectionPlacementKey(b, variant, source.placement.key, source.placement.kind)) == 1);
	}
}

TEST_CASE("Continuous prop runs wrap around a closed loop without a seam", "[section_loops]")
{
	// Only the reflex vertex gets a corner (no inner corner variant); the corner has no run, so the fence is one chain
	// from the corner around the loop start back to the corner.
	const std::vector<std::pair<float, float>> points = { { 0.0f, 0.0f }, { 100.0f, 0.0f }, { 100.0f, 50.0f }, { 50.0f, 50.0f },
		{ 50.0f, 100.0f }, { 0.0f, 100.0f } };
	SectionPathShape shape;
	Prepare(Loop(points), MakeWall(false, true, true), SectionPathParams(), shape);
	REQUIRE(CountRole(shape.layout, SectionVariantRole::OuterCorner) == 1);

	std::vector<Placement> props;
	BuildSectionPlacements(shape, {}, props);
	std::vector<Vector3> fence;
	std::vector<Vector3> ends;
	for (const Placement& p : props)
	{
		if (p.asset == "Props/fence.hmsh")
		{
			fence.push_back(p.position);
			ends.push_back(p.position + (p.rotation * Vector3::UnitZ) * kRunPieceLength);
		}
	}
	REQUIRE(fence.size() > 50);

	// Pieces are laid end to end (pivot = footprint start). One chain: exactly one piece (the last one, before the
	// corner) is not followed by another, also across the loop start.
	size_t chainEnds = 0;
	for (const Vector3& end : ends)
	{
		const bool followed = std::any_of(fence.begin(), fence.end(),
			[&end](const Vector3& p) { return std::fabs(p.x - end.x) < 1e-2f && std::fabs(p.z - end.z) < 1e-2f; });
		chainEnds += followed ? 0 : 1;
	}
	CHECK(chainEnds == 1);

	// The fence passes the loop start (the floor-side line runs 2 m inside the corner at (0, 0)).
	const bool nearStart = std::any_of(fence.begin(), fence.end(),
		[](const Vector3& p) { return std::fabs(p.x - 2.0f) < kRunPieceLength && std::fabs(p.z - 2.0f) < kRunPieceLength; });
	CHECK(nearStart);
}
