// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "fake_terrain_sampler.h"

#include "pcg/graph/evaluator.h"
#include "pcg/graph/node_registry.h"
#include "pcg/graph/point_prune.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <tuple>

using namespace mmo;
using namespace mmo::pcg::graph;

namespace
{
	/// Runs a node type's executor directly with the declared defaults filled in.
	NodeOutput Exec(const String& type, const AttributeSet& settings, const std::map<String, Collection>& inputs, const EvalContext& ctx, const uint64 seed = 1234)
	{
		const NodeTypeDef* def = NodeRegistry::Default().Find(type);
		REQUIRE(def != nullptr);
		AttributeSet effective = settings;
		for (const SettingDef& s : def->settings)
		{
			effective.emplace(s.name, s.defaultValue);
		}
		NodeOutput out = def->execute(ctx, seed, effective, inputs);
		for (const PinDef& pin : def->outputs)
		{
			out.pins[pin.name];
		}
		return out;
	}

	DataItem SplineItem(const std::vector<SplinePoint>& pts, const bool closed, const SplineInterpolation interp = SplineInterpolation::Linear)
	{
		DataItem item;
		item.kind = closed ? ItemKind::Area : ItemKind::Spline;
		item.spline.points = pts;
		item.spline.closed = closed;
		item.spline.interpolation = interp;
		return item;
	}

	DataItem AreaItem(const std::vector<SplinePoint>& pts)
	{
		return SplineItem(pts, true);
	}

	DataItem Square(const float x0, const float z0, const float size)
	{
		return AreaItem({ { x0, z0 }, { x0 + size, z0 }, { x0 + size, z0 + size }, { x0, z0 + size } });
	}

	size_t TotalPoints(const Collection& c)
	{
		size_t n = 0;
		for (const DataItem& item : c)
		{
			n += item.points.points.size();
		}
		return n;
	}

	bool AllFinite(const Collection& c)
	{
		for (const DataItem& item : c)
		{
			for (const Point& p : item.points.points)
			{
				if (!std::isfinite(p.position.x) || !std::isfinite(p.position.y) || !std::isfinite(p.position.z))
				{
					return false;
				}
			}
		}
		return true;
	}

	float DistanceToSegment(const float px, const float pz, const SplinePoint& a, const SplinePoint& b)
	{
		const float dx = b.x - a.x;
		const float dz = b.z - a.z;
		const float len2 = dx * dx + dz * dz;
		float t = len2 > 0 ? ((px - a.x) * dx + (pz - a.z) * dz) / len2 : 0.0f;
		t = std::clamp(t, 0.0f, 1.0f);
		const float cx = a.x + t * dx - px;
		const float cz = a.z + t * dz - pz;
		return std::sqrt(cx * cx + cz * cz);
	}

	/// Points of a collection ordered by their distance attribute.
	std::vector<std::pair<float, Point>> ByDistance(const DataItem& item)
	{
		std::vector<std::pair<float, Point>> result;
		for (size_t i = 0; i < item.points.points.size(); ++i)
		{
			const AttributeValue* d = item.points.GetAttribute(i, "distance");
			REQUIRE(d != nullptr);
			result.emplace_back(std::get<float>(*d), item.points.points[i]);
		}
		std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		return result;
	}

	/// Checks the (cellZ, cellX) output order of a points item.
	bool SortedByCell(const DataItem& item, const float cell)
	{
		for (size_t i = 1; i < item.points.points.size(); ++i)
		{
			const Vector3& a = item.points.points[i - 1].position;
			const Vector3& b = item.points.points[i].position;
			const auto ka = std::make_pair(std::floor(a.z / cell), std::floor(a.x / cell));
			const auto kb = std::make_pair(std::floor(b.z / cell), std::floor(b.x / cell));
			if (kb < ka)
			{
				return false;
			}
		}
		return true;
	}
}

TEST_CASE("Sampler node types are registered", "[graph_nodes_sampler]")
{
	for (const char* type : { "SampleSpline", "SampleArea", "SampleTerrainSurface" })
	{
		const NodeTypeDef* def = NodeRegistry::Default().Find(type);
		REQUIRE(def != nullptr);
		CHECK(def->category == "Sampler");
		REQUIRE(def->outputs.size() == 1);
		CHECK(def->outputs[0].name == "Out");
	}
	CHECK(NodeRegistry::Default().Find("SampleSpline")->inputs.at(0).required);
	CHECK(NodeRegistry::Default().Find("SampleArea")->inputs.at(0).required);
	CHECK_FALSE(NodeRegistry::Default().Find("SampleTerrainSurface")->inputs.at(0).required);
	CHECK(NodeRegistry::Default().Find("SampleSpline")->contextDeps == context_dep::PointLimit);
	CHECK(NodeRegistry::Default().Find("SampleArea")->contextDeps == (context_dep::Terrain | context_dep::PointLimit));
	CHECK(NodeRegistry::Default().Find("SampleTerrainSurface")->contextDeps == (context_dep::Terrain | context_dep::Region | context_dep::PointLimit));
}

TEST_CASE("Sample Spline on a straight spline: count, spacing, offset, alignment, attributes", "[graph_nodes_sampler]")
{
	DataItem road = SplineItem({ { 0, 0 }, { 10, 0 } }, false);
	road.tags = { "road" };
	road.sourceId = 42;
	road.attributes["width"] = 3.0f;
	const EvalContext ctx;

	const NodeOutput out = Exec("SampleSpline", { { "spacing", 2.0f } }, { { "In", { road } } }, ctx);
	REQUIRE(out.error.empty());
	const Collection& c = out.pins.at("Out");
	REQUIRE(c.size() == 1);
	CHECK(c[0].kind == ItemKind::Points);
	CHECK(c[0].tags == std::set<String>{ "road" });
	CHECK(c[0].sourceId == 42);
	const auto pts = ByDistance(c[0]);
	REQUIRE(pts.size() == 6);
	for (size_t i = 0; i < pts.size(); ++i)
	{
		CHECK(pts[i].first == Approx(2.0f * i));
		CHECK(pts[i].second.position.x == Approx(2.0f * i));
		CHECK(pts[i].second.position.z == Approx(0.0f).margin(1e-5));
		CHECK(pts[i].second.seed != 0);
		// alignToPath (default on): local +Z faces along the path (+X).
		const Vector3 forward = pts[i].second.rotation * Vector3(0, 0, 1);
		CHECK(forward.x == Approx(1.0f).margin(1e-3));
		CHECK(forward.z == Approx(0.0f).margin(1e-3));
		const AttributeValue* w = c[0].points.GetAttribute(i, "width");
		REQUIRE(w != nullptr);
		CHECK(std::get<float>(*w) == 3.0f);
	}
	// Path order: the output itself is sorted by distance.
	for (size_t i = 0; i < c[0].points.points.size(); ++i)
	{
		CHECK(std::get<float>(*c[0].points.GetAttribute(i, "distance")) == Approx(2.0f * i));
	}

	SECTION("seeds are unique per point")
	{
		std::set<uint64> seeds;
		for (const auto& p : pts)
		{
			seeds.insert(p.second.seed);
		}
		CHECK(seeds.size() == pts.size());
	}
	SECTION("signed lateral offset")
	{
		const NodeOutput left = Exec("SampleSpline", { { "spacing", 2.0f }, { "offset", 1.5f } }, { { "In", { road } } }, ctx);
		const NodeOutput right = Exec("SampleSpline", { { "spacing", 2.0f }, { "offset", -1.5f } }, { { "In", { road } } }, ctx);
		const auto l = ByDistance(left.pins.at("Out")[0]);
		const auto r = ByDistance(right.pins.at("Out")[0]);
		REQUIRE(l.size() == 6);
		REQUIRE(r.size() == 6);
		for (size_t i = 0; i < 6; ++i)
		{
			CHECK(std::fabs(l[i].second.position.z) == Approx(1.5f));
			CHECK(l[i].second.position.z == Approx(-r[i].second.position.z));
			CHECK(l[i].second.position.x == Approx(2.0f * i));
		}
	}
	SECTION("alignToPath off keeps the identity rotation")
	{
		const NodeOutput o = Exec("SampleSpline", { { "spacing", 2.0f }, { "alignToPath", false } }, { { "In", { road } } }, ctx);
		const Quaternion& q = o.pins.at("Out")[0].points.points[0].rotation;
		CHECK(q.w == Approx(1.0f));
	}
	SECTION("spacing that does not divide the length")
	{
		const NodeOutput o = Exec("SampleSpline", { { "spacing", 3.0f } }, { { "In", { road } } }, ctx);
		CHECK(TotalPoints(o.pins.at("Out")) == 4); // 0, 3, 6, 9
	}
}

TEST_CASE("Sample Spline on a curved spline", "[graph_nodes_sampler]")
{
	const DataItem curve = SplineItem({ { 0, 0 }, { 10, 10 }, { 20, 0 }, { 30, 10 } }, false, SplineInterpolation::CatmullRom);
	const SampledPath reference(curve.spline, 0.05f);
	const float spacing = 1.5f;
	const EvalContext ctx;

	const NodeOutput out = Exec("SampleSpline", { { "spacing", spacing } }, { { "In", { curve } } }, ctx);
	const auto pts = ByDistance(out.pins.at("Out").at(0));
	CHECK(pts.size() == static_cast<size_t>(std::floor(reference.GetLength() / spacing)) + 1);
	for (size_t i = 1; i < pts.size(); ++i)
	{
		CHECK(pts[i].first - pts[i - 1].first == Approx(spacing).margin(1e-3));
		const float chord = (pts[i].second.position - pts[i - 1].second.position).GetLength();
		CHECK(chord <= spacing + 0.01f);
		CHECK(chord >= spacing * 0.8f);
	}
	for (const auto& p : pts)
	{
		CHECK(reference.SignedDistance(p.second.position.x, p.second.position.z) < 0.05f);
	}

	SECTION("offset points stay at the offset distance from the curve")
	{
		const NodeOutput o = Exec("SampleSpline", { { "spacing", spacing }, { "offset", 0.5f } }, { { "In", { curve } } }, ctx);
		for (const Point& p : o.pins.at("Out").at(0).points.points)
		{
			CHECK(reference.SignedDistance(p.position.x, p.position.z) == Approx(0.5f).margin(0.05f));
		}
	}
}

TEST_CASE("Sample Spline closed splines and endsOnly", "[graph_nodes_sampler]")
{
	const EvalContext ctx;
	const DataItem loop = Square(0, 0, 10);
	const DataItem road = SplineItem({ { 0, 0 }, { 10, 0 }, { 10, 10 } }, false);

	const NodeOutput closed = Exec("SampleSpline", { { "spacing", 2.0f } }, { { "In", { loop } } }, ctx);
	CHECK(TotalPoints(closed.pins.at("Out")) == 20);

	const NodeOutput ends = Exec("SampleSpline", { { "spacing", 2.0f }, { "endsOnly", true } }, { { "In", { road, loop } } }, ctx);
	const Collection& c = ends.pins.at("Out");
	REQUIRE(c.size() == 1); // the closed loop has no ends
	const auto pts = ByDistance(c[0]);
	REQUIRE(pts.size() == 2);
	CHECK(pts[0].first == Approx(0.0f));
	CHECK(pts[0].second.position.x == Approx(0.0f));
	CHECK(pts[1].first == Approx(20.0f));
	CHECK(pts[1].second.position.x == Approx(10.0f));
	CHECK(pts[1].second.position.z == Approx(10.0f));
}

TEST_CASE("Sample Spline with empty or degenerate input outputs nothing", "[graph_nodes_sampler]")
{
	const EvalContext ctx;
	CHECK(Exec("SampleSpline", {}, {}, ctx).pins.at("Out").empty());
	CHECK(Exec("SampleSpline", {}, { { "In", {} } }, ctx).pins.at("Out").empty());
	const DataItem single = SplineItem({ { 5, 5 } }, false);
	const DataItem zero = SplineItem({ { 5, 5 }, { 5, 5 } }, false);
	const NodeOutput out = Exec("SampleSpline", {}, { { "In", { single, zero } } }, ctx);
	CHECK(out.error.empty());
	CHECK(out.pins.at("Out").empty());
	const NodeOutput ends = Exec("SampleSpline", { { "endsOnly", true } }, { { "In", { zero } } }, ctx);
	CHECK(ends.pins.at("Out").empty());
}

TEST_CASE("Sample Spline outputs path order per item, items in input order", "[graph_nodes_sampler]")
{
	const EvalContext ctx;
	// A curve that turns back on itself: cell order would differ from path order.
	DataItem back = SplineItem({ { 0, 10 }, { 10, 0 }, { 0, -10 } }, false);
	back.sourceId = 7;
	DataItem other = SplineItem({ { 50, 0 }, { 60, 0 } }, false);
	other.sourceId = 3;
	const NodeOutput out = Exec("SampleSpline", { { "spacing", 1.0f } }, { { "In", { back, other } } }, ctx);
	const Collection& c = out.pins.at("Out");
	REQUIRE(c.size() == 2);
	CHECK(c[0].sourceId == 7);
	CHECK(c[1].sourceId == 3);
	for (const DataItem& item : c)
	{
		for (size_t i = 1; i < item.points.points.size(); ++i)
		{
			CHECK(std::get<float>(*item.points.GetAttribute(i, "distance")) > std::get<float>(*item.points.GetAttribute(i - 1, "distance")));
		}
	}
	CHECK(c[0].points.points.front().position.z == Approx(10.0f));
}

TEST_CASE("Sample Spline seeds differ for coincident and self-intersecting splines", "[graph_nodes_sampler]")
{
	const EvalContext ctx;
	SECTION("two coincident generated splines")
	{
		const DataItem a = SplineItem({ { 0, 0 }, { 10, 0 } }, false);
		const NodeOutput out = Exec("SampleSpline", { { "spacing", 2.0f } }, { { "In", { a, a } } }, ctx);
		const Collection& c = out.pins.at("Out");
		REQUIRE(c.size() == 2);
		std::set<uint64> seeds;
		for (const DataItem& item : c)
		{
			for (const Point& p : item.points.points)
			{
				seeds.insert(p.seed);
			}
		}
		CHECK(seeds.size() == 12);
	}
	SECTION("a spline returning to its start")
	{
		DataItem loop = SplineItem({ { 0, 0 }, { 10, 0 }, { 0, 0 } }, false);
		loop.sourceId = 5;
		const NodeOutput out = Exec("SampleSpline", { { "spacing", 10.0f } }, { { "In", { loop } } }, ctx);
		const auto& pts = out.pins.at("Out").at(0).points.points;
		REQUIRE(pts.size() == 3);
		CHECK(pts[0].position.x == Approx(pts[2].position.x).margin(1e-4));
		CHECK(pts[0].seed != pts[2].seed);
	}
}

TEST_CASE("Sample Spline respects the point limit", "[graph_nodes_sampler]")
{
	EvalContext ctx;
	ctx.pointLimit = 10;
	const DataItem road = SplineItem({ { 0, 0 }, { 1000, 0 } }, false);
	const NodeOutput out = Exec("SampleSpline", { { "spacing", 1.0f } }, { { "In", { road, road } } }, ctx);
	CHECK(TotalPoints(out.pins.at("Out")) == 10);
	CHECK_FALSE(out.warnings.empty());
}

TEST_CASE("Sample Area stays inside, is deterministic and ordered by cell", "[graph_nodes_sampler]")
{
	const DataItem area = AreaItem({ { 0, 0 }, { 40, 0 }, { 40, 30 }, { 20, 45 }, { 0, 30 } });
	const SampledPath reference(area.spline, 0.25f);
	const EvalContext ctx;
	const AttributeSet settings{ { "cellSize", 2.0f } };

	const NodeOutput a = Exec("SampleArea", settings, { { "In", { area } } }, ctx);
	REQUIRE(a.error.empty());
	const Collection& c = a.pins.at("Out");
	REQUIRE(c.size() == 1);
	CHECK(c[0].kind == ItemKind::Points);
	CHECK(c[0].points.points.size() > 200);
	for (const Point& p : c[0].points.points)
	{
		CHECK(reference.Contains(p.position.x, p.position.z));
	}
	CHECK(SortedByCell(c[0], 2.0f));

	const NodeOutput b = Exec("SampleArea", settings, { { "In", { area } } }, ctx);
	CHECK(ContentHash(a.pins.at("Out")) == ContentHash(b.pins.at("Out")));

	const NodeOutput other = Exec("SampleArea", settings, { { "In", { area } } }, ctx, 999);
	CHECK(ContentHash(a.pins.at("Out")) != ContentHash(other.pins.at("Out")));

	SECTION("jitter 0 places points at cell centres")
	{
		const NodeOutput grid = Exec("SampleArea", { { "cellSize", 2.0f }, { "jitter", 0.0f } }, { { "In", { Square(0, 0, 10) } } }, ctx);
		const auto& pts = grid.pins.at("Out").at(0).points.points;
		REQUIRE(pts.size() == 25);
		CHECK(pts[0].position.x == Approx(1.0f));
		CHECK(pts[0].position.z == Approx(1.0f));
		CHECK(pts[6].position.x == Approx(3.0f));
		CHECK(pts[6].position.z == Approx(3.0f));
	}
}

TEST_CASE("Sample Area honours the minimum spacing", "[graph_nodes_sampler]")
{
	const EvalContext ctx;
	const float minSpacing = 1.5f;
	const NodeOutput out = Exec("SampleArea", { { "cellSize", 1.0f }, { "minSpacing", minSpacing } }, { { "In", { Square(0, 0, 40) } } }, ctx);
	const auto& pts = out.pins.at("Out").at(0).points.points;
	REQUIRE(pts.size() > 200);
	for (size_t i = 0; i < pts.size(); ++i)
	{
		for (size_t j = i + 1; j < pts.size(); ++j)
		{
			const float dx = pts[i].position.x - pts[j].position.x;
			const float dz = pts[i].position.z - pts[j].position.z;
			if (std::sqrt(dx * dx + dz * dz) < minSpacing * 0.999f)
			{
				FAIL("points " << i << " and " << j << " are closer than the minimum spacing");
			}
		}
	}
}

TEST_CASE("Sample Area with a minimum spacing far above the cell size stays fast and spaced", "[graph_nodes_sampler]")
{
	// Every candidate has about pi * 20^2 = 1250 conflicting neighbours: the prune must not store them per point.
	const EvalContext ctx;
	const float minSpacing = 20.0f;
	const auto start = std::chrono::steady_clock::now();
	const NodeOutput out = Exec("SampleArea", { { "cellSize", 1.0f }, { "minSpacing", minSpacing } }, { { "In", { Square(0, 0, 120) } } }, ctx);
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	CHECK(seconds < 20.0);
	REQUIRE(out.pins.at("Out").size() == 1);
	const auto& pts = out.pins.at("Out").at(0).points.points;
	REQUIRE(pts.size() >= 9);
	for (size_t i = 0; i < pts.size(); ++i)
	{
		for (size_t j = i + 1; j < pts.size(); ++j)
		{
			const float dx = pts[i].position.x - pts[j].position.x;
			const float dz = pts[i].position.z - pts[j].position.z;
			if (std::sqrt(dx * dx + dz * dz) < minSpacing * 0.999f)
			{
				FAIL("points " << i << " and " << j << " are closer than the minimum spacing");
			}
		}
	}
}

TEST_CASE("Sample Area is local: moving one vertex leaves far points unchanged", "[graph_nodes_sampler]")
{
	const float cellSize = 2.0f;
	const SplinePoint moved{ 30, 80 };
	const SplinePoint movedTo{ 32, 82 };
	const SplinePoint prev{ 60, 60 };
	const SplinePoint next{ 0, 60 };
	const DataItem before = AreaItem({ { 0, 0 }, { 60, 0 }, prev, moved, next });
	const DataItem after = AreaItem({ { 0, 0 }, { 60, 0 }, prev, movedTo, next });
	const EvalContext ctx;

	for (const float minSpacing : { 0.0f, 0.8f })
	{
		const AttributeSet settings{ { "cellSize", cellSize }, { "minSpacing", minSpacing } };
		const NodeOutput a = Exec("SampleArea", settings, { { "In", { before } } }, ctx);
		const NodeOutput b = Exec("SampleArea", settings, { { "In", { after } } }, ctx);

		const auto farPoints = [&](const NodeOutput& out)
		{
			std::set<std::tuple<uint64, float, float>> result;
			for (const Point& p : out.pins.at("Out").at(0).points.points)
			{
				const float x = p.position.x;
				const float z = p.position.z;
				const float d = std::min({ DistanceToSegment(x, z, prev, moved), DistanceToSegment(x, z, moved, next), DistanceToSegment(x, z, prev, movedTo), DistanceToSegment(x, z, movedTo, next) });
				if (d > 2.0f * cellSize)
				{
					result.emplace(p.seed, x, z);
				}
			}
			return result;
		};

		const auto fa = farPoints(a);
		const auto fb = farPoints(b);
		CHECK(fa.size() > 100);
		CHECK(fa == fb);
		CHECK(ContentHash(a.pins.at("Out")) != ContentHash(b.pins.at("Out")));
	}
}

TEST_CASE("Sample Area density attribute scales the acceptance", "[graph_nodes_sampler]")
{
	const EvalContext ctx;
	DataItem full = Square(0, 0, 100);
	full.attributes["dens"] = 1.0f;
	DataItem half = full;
	half.attributes["dens"] = 0.5f;
	const AttributeSet settings{ { "cellSize", 1.0f }, { "densityAttribute", String("dens") } };

	const size_t nFull = TotalPoints(Exec("SampleArea", settings, { { "In", { full } } }, ctx).pins.at("Out"));
	const size_t nHalf = TotalPoints(Exec("SampleArea", settings, { { "In", { half } } }, ctx).pins.at("Out"));
	REQUIRE(nFull > 9000);
	CHECK(static_cast<double>(nHalf) / static_cast<double>(nFull) == Approx(0.5).margin(0.05));

	SECTION("a missing or non-numeric density attribute warns and uses density 1")
	{
		DataItem bad = Square(0, 0, 10);
		bad.attributes["dens"] = String("high");
		const NodeOutput missing = Exec("SampleArea", settings, { { "In", { Square(0, 0, 10) } } }, ctx);
		const NodeOutput wrong = Exec("SampleArea", settings, { { "In", { bad } } }, ctx);
		CHECK(TotalPoints(missing.pins.at("Out")) == 100);
		CHECK_FALSE(missing.warnings.empty());
		CHECK(TotalPoints(wrong.pins.at("Out")) == 100);
		CHECK_FALSE(wrong.warnings.empty());
	}
}

TEST_CASE("Sample Area caps very dense requests at the point limit", "[graph_nodes_sampler]")
{
	EvalContext ctx;
	ctx.pointLimit = 1000;
	const NodeOutput out = Exec("SampleArea", { { "cellSize", 0.01f }, { "minSpacing", 0.0f } }, { { "In", { Square(-5000, -5000, 10000) } } }, ctx);
	CHECK(out.error.empty());
	CHECK(TotalPoints(out.pins.at("Out")) <= 1000);
	CHECK(TotalPoints(out.pins.at("Out")) > 0);
	CHECK_FALSE(out.warnings.empty());

	SECTION("a near-zero density over a huge area stops at the cell budget")
	{
		DataItem none = Square(-5000, -5000, 10000);
		none.attributes["d"] = 1.0e-12f;
		const NodeOutput o = Exec("SampleArea", { { "cellSize", 0.01f }, { "densityAttribute", String("d") } }, { { "In", { none } } }, ctx);
		CHECK(TotalPoints(o.pins.at("Out")) <= 1);
		CHECK_FALSE(o.warnings.empty());
	}
}

TEST_CASE("Sample Area applies the point limit after the prune", "[graph_nodes_sampler]")
{
	EvalContext ctx;
	const DataItem forest = Square(0, 0, 60);

	// Unpruned: 14400 candidates, far above the limit.
	ctx.pointLimit = 100000;
	const size_t unpruned = TotalPoints(Exec("SampleArea", { { "cellSize", 0.5f } }, { { "In", { forest } } }, ctx).pins.at("Out"));
	REQUIRE(unpruned > 10000);

	ctx.pointLimit = 1000;
	const NodeOutput out = Exec("SampleArea", { { "cellSize", 0.5f }, { "minSpacing", 4.0f } }, { { "In", { forest } } }, ctx);
	const auto& pts = out.pins.at("Out").at(0).points.points;
	CHECK(out.warnings.empty());
	REQUIRE(pts.size() > 100);
	REQUIRE(pts.size() < 1000);

	// Untruncated: the whole area is covered (points near all four borders).
	float maxX = 0, maxZ = 0;
	for (const Point& p : pts)
	{
		maxX = std::max(maxX, p.position.x);
		maxZ = std::max(maxZ, p.position.z);
	}
	CHECK(maxX > 55.0f);
	CHECK(maxZ > 55.0f);
}

TEST_CASE("Sample Area band-wise prune equals a prune of the whole item", "[graph_nodes_sampler]")
{
	EvalContext ctx;
	const DataItem area = AreaItem({ { 0, 0 }, { 50, 5 }, { 45, 70 }, { 5, 60 } });
	for (const float minSpacing : { 1.5f, 4.0f })
	{
		std::vector<Point> all = Exec("SampleArea", { { "cellSize", 0.5f } }, { { "In", { area } } }, ctx).pins.at("Out").at(0).points.points;
		PruneOptions options;
		options.minSpacing = minSpacing;
		// Sample Area keeps the lower seed; PruneByDistance keeps the higher one, so rank by inverted seeds.
		std::vector<Point> ranked = all;
		for (Point& p : ranked)
		{
			p.seed = ~p.seed;
		}
		const std::vector<uint8> keep = PruneByDistance(ranked, options);
		std::vector<std::tuple<uint64, float, float>> expected;
		for (size_t i = 0; i < all.size(); ++i)
		{
			if (keep[i] != 0)
			{
				expected.emplace_back(all[i].seed, all[i].position.x, all[i].position.z);
			}
		}

		const NodeOutput banded = Exec("SampleArea", { { "cellSize", 0.5f }, { "minSpacing", minSpacing } }, { { "In", { area } } }, ctx);
		std::vector<std::tuple<uint64, float, float>> actual;
		for (const Point& p : banded.pins.at("Out").at(0).points.points)
		{
			actual.emplace_back(p.seed, p.position.x, p.position.z);
		}
		CHECK(actual.size() == expected.size());
		CHECK(actual == expected);
	}
}

TEST_CASE("Sample Area takes y from the terrain", "[graph_nodes_sampler]")
{
	FakeTerrainSampler terrain;
	terrain.base = 3.0f;
	terrain.slopeX = 0.25f;
	terrain.slopeZ = 0.5f;
	EvalContext ctx;
	ctx.terrain = &terrain;

	const NodeOutput out = Exec("SampleArea", { { "cellSize", 1.0f } }, { { "In", { Square(0, 0, 10) } } }, ctx);
	CHECK(out.warnings.empty());
	const auto& pts = out.pins.at("Out").at(0).points.points;
	REQUIRE(pts.size() == 100);
	for (const Point& p : pts)
	{
		CHECK(p.position.y == Approx(3.0f + 0.25f * p.position.x + 0.5f * p.position.z));
	}

	SECTION("points where the terrain has no height stay at y 0 with a warning")
	{
		terrain.maxX = 5.0f;
		const NodeOutput o = Exec("SampleArea", { { "cellSize", 1.0f } }, { { "In", { Square(0, 0, 10) } } }, ctx);
		const auto& q = o.pins.at("Out").at(0).points.points;
		REQUIRE(q.size() == 100);
		CHECK_FALSE(o.warnings.empty());
		for (const Point& p : q)
		{
			if (p.position.x > 5.0f)
			{
				CHECK(p.position.y == 0.0f);
			}
			else
			{
				CHECK(p.position.y == Approx(3.0f + 0.25f * p.position.x + 0.5f * p.position.z));
			}
		}
	}
}

TEST_CASE("Sample Area with empty or degenerate input outputs nothing", "[graph_nodes_sampler]")
{
	const EvalContext ctx;
	CHECK(Exec("SampleArea", {}, {}, ctx).pins.at("Out").empty());
	CHECK(Exec("SampleArea", {}, { { "In", {} } }, ctx).pins.at("Out").empty());
	const DataItem line = AreaItem({ { 0, 0 }, { 10, 0 } });
	const DataItem flat = AreaItem({ { 0, 0 }, { 10, 0 }, { 20, 0 } });
	const NodeOutput out = Exec("SampleArea", {}, { { "In", { line, flat } } }, ctx);
	CHECK(out.error.empty());
	CHECK(out.pins.at("Out").empty());
	CHECK(AllFinite(out.pins.at("Out")));
}

TEST_CASE("PruneByDistance keeps a spaced, order independent subset", "[graph_nodes_sampler]")
{
	std::vector<Point> pts(3);
	pts[0].position = Vector3(0, 0, 0);
	pts[0].seed = 1;
	pts[1].position = Vector3(0.5f, 0, 0);
	pts[1].seed = 3;
	pts[2].position = Vector3(1.2f, 0, 0);
	pts[2].seed = 2;
	PruneOptions options;
	options.minSpacing = 1.0f;
	// 1 outranks 0 and 2 (its neighbours), so 0 and 2 go even though 0 and 2 do not conflict with each other.
	CHECK(PruneByDistance(pts, options) == std::vector<uint8>{ 0, 1, 0 });

	const std::vector<float> priorities{ 1.0f, 0.0f, 1.0f };
	CHECK(PruneByDistance(pts, options, &priorities) == std::vector<uint8>{ 1, 0, 1 });

	options.minSpacing = 0.0f;
	CHECK(PruneByDistance(pts, options) == std::vector<uint8>{ 1, 1, 1 });

	SECTION("3D distance")
	{
		std::vector<Point> stacked(2);
		stacked[0].position = Vector3(0, 0, 0);
		stacked[1].position = Vector3(0, 5, 0);
		stacked[1].seed = 1;
		PruneOptions o;
		o.minSpacing = 1.0f;
		CHECK(PruneByDistance(stacked, o) == std::vector<uint8>{ 0, 1 });
		o.planar = false;
		CHECK(PruneByDistance(stacked, o) == std::vector<uint8>{ 1, 1 });
	}
}

TEST_CASE("Sample Terrain Surface places points on the terrain", "[graph_nodes_sampler]")
{
	FakeTerrainSampler terrain;
	terrain.base = 5.0f;
	terrain.slopeX = 0.5f;
	terrain.slopeZ = -0.25f;
	EvalContext ctx;
	ctx.terrain = &terrain;

	const NodeOutput out = Exec("SampleTerrainSurface", { { "cellSize", 1.0f } }, { { "In", { Square(0, 0, 10) } } }, ctx);
	REQUIRE(out.error.empty());
	const Collection& c = out.pins.at("Out");
	REQUIRE(c.size() == 1);
	const auto& pts = c[0].points.points;
	REQUIRE(pts.size() == 100);
	for (const Point& p : pts)
	{
		float h = 0;
		REQUIRE(terrain.Height(p.position.x, p.position.z, h));
		CHECK(p.position.y == Approx(h));
		CHECK(p.rotation.w == Approx(1.0f));
	}
	CHECK(SortedByCell(c[0], 1.0f));
	CHECK(pts[0].position.x == Approx(0.5f));
	CHECK(pts[0].position.z == Approx(0.5f));

	SECTION("alignToNormal rotates up onto the terrain normal")
	{
		const NodeOutput o = Exec("SampleTerrainSurface", { { "cellSize", 1.0f }, { "alignToNormal", true } }, { { "In", { Square(0, 0, 4) } } }, ctx);
		const Vector3 n = terrain.Normal(0, 0);
		for (const Point& p : o.pins.at("Out").at(0).points.points)
		{
			const Vector3 up = p.rotation * Vector3(0, 1, 0);
			CHECK(up.x == Approx(n.x).margin(1e-4));
			CHECK(up.y == Approx(n.y).margin(1e-4));
			CHECK(up.z == Approx(n.z).margin(1e-4));
		}
	}
	SECTION("no input uses the region")
	{
		ctx.region = Square(0, 0, 6).spline;
		const NodeOutput o = Exec("SampleTerrainSurface", { { "cellSize", 1.0f } }, {}, ctx);
		CHECK(TotalPoints(o.pins.at("Out")) == 36);
	}
	SECTION("a linked but empty input outputs nothing")
	{
		ctx.region = Square(0, 0, 6).spline;
		const NodeOutput o = Exec("SampleTerrainSurface", { { "cellSize", 1.0f } }, { { "In", {} } }, ctx);
		CHECK(o.pins.at("Out").empty());
	}
	SECTION("cells without terrain are skipped")
	{
		terrain.maxX = 4.9f;
		const NodeOutput o = Exec("SampleTerrainSurface", { { "cellSize", 1.0f } }, { { "In", { Square(0, 0, 10) } } }, ctx);
		CHECK(TotalPoints(o.pins.at("Out")) == 50);
	}
	SECTION("point limit")
	{
		ctx.pointLimit = 30;
		const NodeOutput o = Exec("SampleTerrainSurface", { { "cellSize", 1.0f } }, { { "In", { Square(0, 0, 10) } } }, ctx);
		CHECK(TotalPoints(o.pins.at("Out")) == 30);
		CHECK_FALSE(o.warnings.empty());
	}
}

TEST_CASE("Sample Terrain Surface without terrain or area outputs nothing", "[graph_nodes_sampler]")
{
	EvalContext ctx;
	const NodeOutput noTerrain = Exec("SampleTerrainSurface", {}, { { "In", { Square(0, 0, 10) } } }, ctx);
	CHECK(noTerrain.error.empty());
	CHECK(noTerrain.pins.at("Out").empty());
	CHECK_FALSE(noTerrain.warnings.empty());

	FakeTerrainSampler terrain;
	ctx.terrain = &terrain;
	const NodeOutput noRegion = Exec("SampleTerrainSurface", {}, {}, ctx);
	CHECK(noRegion.pins.at("Out").empty());
}

TEST_CASE("Samplers re-run through the evaluator when the terrain changes", "[graph_nodes_sampler]")
{
	FakeTerrainSampler terrain;
	EvalContext ctx;
	ctx.terrain = &terrain;
	ctx.region = Square(0, 0, 8).spline;

	Graph g;
	NodeInstance region;
	region.id = 1;
	region.type = "GetRegion";
	NodeInstance surface;
	surface.id = 2;
	surface.type = "SampleTerrainSurface";
	g.nodes = { region, surface };
	g.links.push_back(Link{ 1, "Out", 2, "In" });

	GraphCache cache;
	const EvalResult a = Evaluate(g, NodeRegistry::Default(), ctx, &cache);
	REQUIRE(a.ok);
	CHECK(TotalPoints(a.nodes.at(2).output.pins.at("Out")) == 16);

	terrain.base = 20.0f;
	terrain.revision++;
	const EvalResult b = Evaluate(g, NodeRegistry::Default(), ctx, &cache);
	CHECK_FALSE(b.nodes.at(2).fromCache);
	CHECK(b.nodes.at(2).output.pins.at("Out").at(0).points.points.at(0).position.y == Approx(20.0f));
}

TEST_CASE("Sample Spline does not turn item attributes named like point properties into columns", "[graph_nodes_sampler]")
{
	const EvalContext ctx;
	DataItem spline = SplineItem({ { 0, 0 }, { 10, 0 } }, false);
	spline.attributes["density"] = 0.25f;
	spline.attributes["scale.x"] = 2.0f;
	spline.attributes["kind"] = String("road");
	const NodeOutput out = Exec("SampleSpline", { { "spacing", 5.0f } }, { { "In", { spline } } }, ctx);
	REQUIRE(out.pins.at("Out").size() == 1);
	const PointSet& set = out.pins.at("Out")[0].points;
	CHECK(set.columns.count("density") == 0);
	CHECK(set.columns.count("scale.x") == 0);
	CHECK(set.columns.count("kind") == 1);
	CHECK(set.points[0].density == 1.0f);
	CHECK(std::count_if(out.warnings.begin(), out.warnings.end(), [](const String& w) { return w.find("point properties") != String::npos; }) == 1);
}

TEST_CASE("Sample Area's prune keeps the lower seed, as Self Prune with priority seed", "[graph_nodes_sampler]")
{
	const EvalContext ctx;
	const float minSpacing = 2.5f;
	const Collection area{ Square(0, 0, 30) };
	const NodeOutput pruned = Exec("SampleArea", { { "cellSize", 1.0f }, { "minSpacing", minSpacing } }, { { "In", area } }, ctx);
	const NodeOutput all = Exec("SampleArea", { { "cellSize", 1.0f } }, { { "In", area } }, ctx);
	const NodeOutput selfPruned = Exec("SelfPrune", { { "minSpacing", minSpacing }, { "priority", String("seed") } }, { { "In", all.pins.at("Out") } }, ctx);
	REQUIRE(pruned.pins.at("Out").size() == 1);
	REQUIRE(selfPruned.pins.at("Out").size() == 1);
	const auto& a = pruned.pins.at("Out")[0].points.points;
	const auto& b = selfPruned.pins.at("Out")[0].points.points;
	REQUIRE(a.size() == b.size());
	REQUIRE(a.size() > 20);
	for (size_t i = 0; i < a.size(); ++i)
	{
		CHECK(a[i].seed == b[i].seed);
		CHECK(a[i].position.x == b[i].position.x);
		CHECK(a[i].position.z == b[i].position.z);
	}
}
