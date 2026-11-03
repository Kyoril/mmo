// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/section_path.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

using namespace mmo;
using namespace mmo::pcg;

namespace
{
	/// Baked placements carry the section object's unique id as key; every prop gets a fresh one.
	uint64 g_nextPropKey = 1000;

	SectionPlacement Prop(const String& asset, const float x, const float z, const float above, const PlacementKind kind = PlacementKind::Entity)
	{
		SectionPlacement p;
		p.placement.key = g_nextPropKey++;
		p.placement.kind = kind;
		p.placement.asset = asset;
		p.placement.position = Vector3(x, 123.0f, z);
		p.heightAboveTerrain = above;
		return p;
	}

	/// Section L 20, halfWidth 3, sideFalloff 4 with one flat variant holding the given placements.
	SectionAsset MakeAsset(const std::vector<SectionPlacement>& placements, const std::vector<PropRule>& rules = {})
	{
		SectionAsset asset;
		asset.length = 20.0f;
		asset.halfWidth = 3.0f;
		asset.sideFalloff = 4.0f;
		asset.rules = rules;
		std::sort(asset.rules.begin(), asset.rules.end(), [](const PropRule& a, const PropRule& b) { return a.asset < b.asset; });
		SectionVariant v;
		v.placements = placements;
		asset.variants.push_back(v);
		return asset;
	}

	PathSpline Polyline(const std::vector<std::pair<float, float>>& points, const float y0 = 0.0f, const float y1 = 0.0f)
	{
		PathSpline spline;
		spline.interpolation = SplineInterpolation::Linear;
		for (size_t i = 0; i < points.size(); ++i)
		{
			SplinePoint p;
			p.x = points[i].first;
			p.z = points[i].second;
			p.y = y0 + (y1 - y0) * static_cast<float>(i) / static_cast<float>(points.size() - 1);
			spline.points.push_back(p);
		}
		return spline;
	}

	std::vector<Placement> Build(const PathSpline& spline, const SectionAsset& asset, const SectionPathParams& params = SectionPathParams(),
		const float groundHeight = 3.0f)
	{
		std::vector<uint64> ids;
		for (size_t i = 0; i < spline.points.size(); ++i)
		{
			ids.push_back(500 + i);
		}
		SectionPathShape shape;
		String error;
		REQUIRE(PrepareSectionPath(spline, ids, asset, params, {}, shape, error));
		std::vector<Placement> out;
		BuildSectionPlacements(shape, [groundHeight](float, float) { return groundHeight; }, out);

		// Sorted by key, keys unique, every prop inside the influence.
		const WorldRect r = GetInfluenceRect(shape);
		for (size_t i = 0; i < out.size(); ++i)
		{
			if (i > 0)
			{
				REQUIRE(out[i - 1].key < out[i].key);
			}
			REQUIRE(out[i].position.x >= r.minX);
			REQUIRE(out[i].position.x <= r.maxX);
			REQUIRE(out[i].position.z >= r.minZ);
			REQUIRE(out[i].position.z <= r.maxZ);
		}
		return out;
	}

	const Placement* Near(const std::vector<Placement>& placements, const float x, const float z)
	{
		for (const Placement& p : placements)
		{
			if (std::fabs(p.position.x - x) < 0.01f && std::fabs(p.position.z - z) < 0.01f)
			{
				return &p;
			}
		}
		return nullptr;
	}

	void CheckDirection(const Vector3& v, const float x, const float y, const float z)
	{
		CHECK(v.x == Approx(x).margin(1e-4f));
		CHECK(v.y == Approx(y).margin(1e-4f));
		CHECK(v.z == Approx(z).margin(1e-4f));
	}
}

TEST_CASE("A prop lands at its local offset along a straight spline", "[section_path_place]")
{
	const float x0 = 100.0f;
	const float z0 = 200.0f;
	const std::vector<Placement> out = Build(Polyline({ { x0, z0 }, { x0, z0 + 100.0f } }, 50.0f, 50.0f), MakeAsset({ Prop("rock", 2.0f, 10.0f, 0.5f) }));
	REQUIRE(out.size() == 5);

	const Placement* p = Near(out, x0 + 2.0f, z0 + 10.0f);
	REQUIRE(p != nullptr);
	CHECK(p->position.y == Approx(3.5f));
	CHECK(p->asset == "rock");
	CheckDirection(p->rotation * Vector3::UnitZ, 0.0f, 0.0f, 1.0f);
	CheckDirection(p->rotation * Vector3::UnitX, 1.0f, 0.0f, 0.0f);
}

TEST_CASE("A prop turns with the spline", "[section_path_place]")
{
	const float x0 = -40.0f;
	const float z0 = 10.0f;
	const std::vector<Placement> out = Build(Polyline({ { x0, z0 }, { x0 + 100.0f, z0 } }), MakeAsset({ Prop("rock", 2.0f, 10.0f, 0.5f) }));
	const Placement* p = Near(out, x0 + 10.0f, z0 - 2.0f);
	REQUIRE(p != nullptr);
	CHECK(p->position.y == Approx(3.5f));
	CheckDirection(p->rotation * Vector3::UnitZ, 1.0f, 0.0f, 0.0f);
}

TEST_CASE("Props stay upright on a rising spline", "[section_path_place]")
{
	SectionPlacement tilted = Prop("rock", -1.0f, 5.0f, 0.0f);
	const std::vector<Placement> out = Build(Polyline({ { 0.0f, 0.0f }, { 0.0f, 100.0f } }, 0.0f, 30.0f), MakeAsset({ tilted }));
	REQUIRE_FALSE(out.empty());
	for (const Placement& p : out)
	{
		CheckDirection(p.rotation * Vector3::UnitY, 0.0f, 1.0f, 0.0f);
		CHECK(p.position.y == Approx(3.0f));
	}
}

TEST_CASE("Section placements are deterministic", "[section_path_place]")
{
	PropRule rule;
	rule.asset = "rock";
	rule.keepChance = 0.7f;
	rule.positionJitter = 1.5f;
	rule.yawJitterDeg = 40.0f;
	rule.scaleJitter = 0.3f;
	rule.swapGroup = { "rock_a", "rock_b", "rock_c" };
	const SectionAsset asset = MakeAsset({ Prop("rock", 2.0f, 4.0f, 0.0f), Prop("rock", -2.0f, 12.0f, 0.2f), Prop("bush", 3.0f, 18.0f, 0.0f) }, { rule });
	const PathSpline spline = Polyline({ { 0.0f, 0.0f }, { 30.0f, 60.0f }, { 0.0f, 140.0f } });

	const std::vector<Placement> a = Build(spline, asset);
	const std::vector<Placement> b = Build(spline, asset);
	REQUIRE(a.size() == b.size());
	REQUIRE_FALSE(a.empty());
	for (size_t i = 0; i < a.size(); ++i)
	{
		CHECK(a[i].key == b[i].key);
		CHECK(a[i].asset == b[i].asset);
		CHECK(a[i].position.x == b[i].position.x);
		CHECK(a[i].position.y == b[i].position.y);
		CHECK(a[i].position.z == b[i].position.z);
		CHECK(a[i].rotation.w == b[i].rotation.w);
		CHECK(a[i].rotation.y == b[i].rotation.y);
		CHECK(a[i].scale.x == b[i].scale.x);
	}
}

TEST_CASE("Keep chance and prop density thin out the props", "[section_path_place]")
{
	PropRule never;
	never.asset = "gone";
	never.keepChance = 0.0f;

	std::vector<SectionPlacement> props;
	for (int i = 0; i < 10; ++i)
	{
		props.push_back(Prop("stay", -2.0f, 1.0f + 2.0f * static_cast<float>(i), 0.0f));
	}
	props.push_back(Prop("gone", 2.0f, 10.0f, 0.0f));
	const SectionAsset asset = MakeAsset(props, { never });
	const PathSpline spline = Polyline({ { 0.0f, 0.0f }, { 0.0f, 400.0f } });

	const std::vector<Placement> all = Build(spline, asset);
	CHECK(all.size() == 200);
	for (const Placement& p : all)
	{
		CHECK(p.asset == "stay");
	}

	SectionPathParams half;
	half.propDensity = 0.5f;
	const std::vector<Placement> thinned = Build(spline, asset, half);
	CHECK(thinned.size() >= 85);
	CHECK(thinned.size() <= 115);
}

TEST_CASE("Swap groups pick the original or a listed asset", "[section_path_place]")
{
	PropRule rule;
	rule.asset = "lantern";
	rule.swapGroup = { "lantern_a", "lantern_b" };
	std::vector<SectionPlacement> props;
	for (int i = 0; i < 8; ++i)
	{
		SectionPlacement p = Prop("lantern", 2.5f, 1.0f + 2.0f * static_cast<float>(i), 0.0f);
		p.placement.materialOverrides.push_back({ 1, "mat_x" });
		props.push_back(p);
	}
	const std::vector<Placement> out = Build(Polyline({ { 0.0f, 0.0f }, { 0.0f, 200.0f } }), MakeAsset(props, { rule }));
	REQUIRE(out.size() == 80);
	std::map<String, size_t> seen;
	for (const Placement& p : out)
	{
		CHECK((p.asset == "lantern" || p.asset == "lantern_a" || p.asset == "lantern_b"));
		++seen[p.asset];

		// Overrides belong to the original asset only.
		CHECK(p.materialOverrides.empty() == (p.asset != "lantern"));
	}
	REQUIRE(seen.size() == 3);
	for (const auto& entry : seen)
	{
		CHECK(entry.second >= 10);
	}
}

TEST_CASE("A swap takes the kind from the new asset", "[section_path_place]")
{
	PropRule entityRule;
	entityRule.asset = "crate";
	entityRule.swapGroup = { "Models/Tower.HWMO" };
	PropRule modelRule;
	modelRule.asset = "hut.hwmo";
	modelRule.swapGroup = { "rock" };
	PropRule foliageRule;
	foliageRule.asset = "bush";
	foliageRule.swapGroup = { "bush_b", "tree.hwmo" };

	std::vector<SectionPlacement> props;
	for (int i = 0; i < 6; ++i)
	{
		const float z = 1.0f + 3.0f * static_cast<float>(i);
		props.push_back(Prop("crate", -2.5f, z, 0.0f));
		SectionPlacement hut = Prop("hut.hwmo", 0.0f, z, 0.0f, PlacementKind::WorldModel);
		hut.placement.kitPaths.push_back("Kits/hut.hkit");
		props.push_back(hut);
		props.push_back(Prop("bush", 2.5f, z, 0.0f, PlacementKind::Foliage));
	}
	const std::vector<Placement> out = Build(Polyline({ { 0.0f, 0.0f }, { 0.0f, 200.0f } }), MakeAsset(props, { entityRule, modelRule, foliageRule }));
	REQUIRE(out.size() == 180);

	std::set<String> seen;
	for (const Placement& p : out)
	{
		seen.insert(p.asset);
		if (p.asset == "crate" || p.asset == "rock")
		{
			CHECK(p.kind == PlacementKind::Entity);
		}
		else if (p.asset == "Models/Tower.HWMO" || p.asset == "hut.hwmo")
		{
			CHECK(p.kind == PlacementKind::WorldModel);
		}
		else
		{
			// Foliage stays foliage and never becomes a world model.
			CHECK((p.asset == "bush" || p.asset == "bush_b"));
			CHECK(p.kind == PlacementKind::Foliage);
		}
		CHECK(p.kitPaths.empty() == (p.asset != "hut.hwmo"));
	}
	CHECK(seen == std::set<String>{ "crate", "Models/Tower.HWMO", "hut.hwmo", "rock", "bush", "bush_b" });
}

TEST_CASE("Prop keys follow the baked placement, not its index", "[section_path_place]")
{
	PropRule rule;
	rule.asset = "rock";
	rule.keepChance = 0.8f;
	rule.positionJitter = 1.0f;
	rule.yawJitterDeg = 30.0f;
	rule.scaleJitter = 0.2f;
	rule.swapGroup = { "rock_b" };

	const SectionPlacement a = Prop("rock", 2.0f, 4.0f, 0.0f);
	const SectionPlacement b = Prop("rock", -2.0f, 12.0f, 0.0f);
	const SectionPlacement added = Prop("bush", 1.0f, 8.0f, 0.0f);
	const PathSpline spline = Polyline({ { 0.0f, 0.0f }, { 20.0f, 60.0f }, { 0.0f, 140.0f } });

	// Inserting a prop in front of the others shifts their indices but not their keys or variation.
	const std::vector<Placement> before = Build(spline, MakeAsset({ a, b }));
	const std::vector<Placement> after = Build(spline, MakeAsset({ added, a, b }));
	REQUIRE_FALSE(before.empty());

	std::map<uint64, const Placement*> byKey;
	for (const Placement& p : after)
	{
		byKey[p.key] = &p;
	}
	for (const Placement& p : before)
	{
		const auto it = byKey.find(p.key);
		REQUIRE(it != byKey.end());
		CHECK(it->second->asset == p.asset);
		CHECK(it->second->position.x == p.position.x);
		CHECK(it->second->position.z == p.position.z);
		CHECK(it->second->rotation.w == p.rotation.w);
		CHECK(it->second->scale.x == p.scale.x);
	}

	size_t bushes = 0;
	for (const Placement& p : after)
	{
		bushes += p.asset == "bush" ? 1 : 0;
	}
	CHECK(after.size() == before.size() + bushes);
	CHECK(bushes > 0);
}

TEST_CASE("Moving a control point keeps the props of untouched spans", "[section_path_place]")
{
	PropRule rule;
	rule.asset = "rock";
	rule.keepChance = 0.8f;
	rule.positionJitter = 1.0f;
	rule.yawJitterDeg = 30.0f;
	rule.scaleJitter = 0.2f;

	// Props baked at the copy ends, jittered far enough to cross into the neighbouring span.
	PropRule edgeRule;
	edgeRule.asset = "edge";
	edgeRule.positionJitter = 1.5f;
	std::vector<SectionPlacement> props = { Prop("rock", 2.0f, 6.0f, 0.0f), Prop("rock", -2.0f, 14.0f, 0.0f) };
	for (int i = 0; i < 4; ++i)
	{
		const float offset = 0.1f * static_cast<float>(i);
		props.push_back(Prop("edge", 2.0f - static_cast<float>(i), 19.9f - offset, 0.0f));
		props.push_back(Prop("edge", -2.0f + static_cast<float>(i), 0.1f + offset, 0.0f));
	}
	const SectionAsset asset = MakeAsset(props, { rule, edgeRule });

	const PathSpline beforeSpline = Polyline({ { 0.0f, 0.0f }, { 0.0f, 40.0f }, { 0.0f, 80.0f }, { 0.0f, 120.0f }, { 0.0f, 160.0f } });
	const PathSpline afterSpline = Polyline({ { 0.0f, 0.0f }, { 0.0f, 40.0f }, { 15.0f, 85.0f }, { 0.0f, 120.0f }, { 0.0f, 160.0f } });
	const std::vector<Placement> before = Build(beforeSpline, asset);
	const std::vector<Placement> after = Build(afterSpline, asset);

	// Keys of the props of span 0 and span 3, which do not touch the moved point.
	SectionPathShape shape;
	String error;
	REQUIRE(PrepareSectionPath(beforeSpline, { 500, 501, 502, 503, 504 }, asset, SectionPathParams(), {}, shape, error));
	std::set<uint64> untouched;
	for (const SectionCopy& copy : shape.layout.copies)
	{
		if (copy.span == 0 || copy.span == 3)
		{
			for (size_t i = 0; i < props.size(); ++i)
			{
				untouched.insert(SectionPlacementKey(copy, GetVariant(shape, copy), props[i].placement.key, props[i].placement.kind));
			}
		}
	}

	std::map<uint64, const Placement*> byKey;
	for (const Placement& p : after)
	{
		byKey[p.key] = &p;
	}

	// Jitter never moves a prop out of its span's arc range (straight path: z = arc distance).
	for (const Placement& p : before)
	{
		if (untouched.count(p.key) != 0)
		{
			const bool inSpan0 = p.position.z >= -1e-3f && p.position.z <= 40.0f + 1e-3f;
			const bool inSpan3 = p.position.z >= 120.0f - 1e-3f && p.position.z <= 160.0f + 1e-3f;
			CHECK((inSpan0 || inSpan3));
		}
	}

	size_t checked = 0;
	for (const Placement& p : before)
	{
		if (untouched.count(p.key) == 0)
		{
			continue;
		}
		const auto it = byKey.find(p.key);
		REQUIRE(it != byKey.end());
		const Placement& q = *it->second;
		CHECK(q.asset == p.asset);
		CHECK(q.position.x == Approx(p.position.x).margin(1e-3f));
		CHECK(q.position.y == Approx(p.position.y).margin(1e-3f));
		CHECK(q.position.z == Approx(p.position.z).margin(1e-3f));
		CHECK(q.rotation.w == Approx(p.rotation.w).margin(1e-4f));
		CHECK(q.rotation.y == Approx(p.rotation.y).margin(1e-4f));
		CHECK(q.scale.x == Approx(p.scale.x).margin(1e-5f));
		++checked;
	}
	CHECK(checked >= 20);
}

TEST_CASE("Without a ground the props sit on the centre line height of the height mode", "[section_path_place]")
{
	const SectionAsset asset = MakeAsset({ Prop("rock", 2.0f, 10.0f, 0.5f) });
	const PathSpline spline = Polyline({ { 0.0f, 0.0f }, { 0.0f, 100.0f } }, 40.0f, 40.0f);

	SectionPathShape followSpline;
	String error;
	REQUIRE(PrepareSectionPath(spline, { 1, 2 }, asset, SectionPathParams(), {}, followSpline, error));
	std::vector<Placement> out;
	BuildSectionPlacements(followSpline, {}, out);
	REQUIRE_FALSE(out.empty());
	for (const Placement& p : out)
	{
		CHECK(p.position.y == Approx(40.5f));
	}

	SectionPathParams params;
	params.heightMode = SectionHeightMode::FollowGround;
	SectionPathShape ground;
	REQUIRE(PrepareSectionPath(spline, { 1, 2 }, asset, params, [](float, const float z) { return z * 0.1f; }, ground, error));
	BuildSectionPlacements(ground, {}, out);
	const Placement* p = Near(out, 2.0f, 10.0f);
	REQUIRE(p != nullptr);
	CHECK(p->position.y == Approx(1.5f).margin(1e-3f));
}
