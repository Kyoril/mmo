// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "fake_terrain.h"
#include "pcg/section_asset.h"
#include "pcg/section_bake.h"
#include "pcg/section_path.h"
#include "binary_io/memory_source.h"
#include "binary_io/vector_sink.h"

#include <algorithm>
#include <cmath>
#include <set>

using namespace mmo;
using namespace mmo::pcg;

namespace
{
	constexpr uint32 kDefaultPaint = 0x000000FFu;
	const std::array<String, 4> kTextures = { "Tex/a.htex", "Tex/b.htex", "Tex/c.htex", "Tex/d.htex" };

	std::vector<char> Serialize(const SectionAsset& asset)
	{
		std::vector<char> bytes;
		io::VectorSink<char> sink(bytes);
		io::Writer writer(sink);
		REQUIRE(WriteSectionAsset(writer, asset));
		return bytes;
	}

	bool Deserialize(const std::vector<char>& bytes, SectionAsset& out)
	{
		io::MemorySource source(bytes);
		io::Reader reader(source);
		return ReadSectionAsset(reader, out);
	}

	SectionRunPiece Piece(const uint64 key, const String& asset, const float x, const float z, const float length, const float startOffset,
		const float above = 0.0f, const Quaternion& rotation = Quaternion::Identity)
	{
		SectionRunPiece piece;
		piece.placement.key = key;
		piece.placement.kind = PlacementKind::Entity;
		piece.placement.asset = asset;
		piece.placement.position = Vector3(x, 0.0f, z);
		piece.placement.rotation = rotation;
		piece.heightAboveTerrain = above;
		piece.length = length;
		piece.startOffset = startOffset;
		return piece;
	}

	/// A run of `count` pieces of `length` m (pivot in the middle) at lateral offset x, footprints from startZ on.
	SectionRun MakeRun(const String& name, const float x, const float startZ, const uint32 count, const float length = 4.0f,
		const float keepChance = 1.0f, const uint64 firstKey = 9000)
	{
		SectionRun run;
		run.name = name;
		run.keepChance = keepChance;
		run.x = x;
		run.startZ = startZ;
		run.endZ = startZ + length * static_cast<float>(count);
		for (uint32 i = 0; i < count; ++i)
		{
			run.pieces.push_back(Piece(firstKey + i, "Props/fence.hmsh", x, startZ + length * (static_cast<float>(i) + 0.5f), length, -0.5f * length));
		}
		return run;
	}

	/// Section L 20, halfWidth 3, sideFalloff 4, one flat variant with the given runs and no other props.
	SectionAsset MakeRunAsset(const std::vector<SectionRun>& runs, const std::vector<PropRule>& rules = {})
	{
		SectionAsset asset;
		asset.length = 20.0f;
		asset.halfWidth = 3.0f;
		asset.sideFalloff = 4.0f;
		asset.rules = rules;
		SectionVariant variant;
		variant.runs = runs;
		asset.variants.push_back(variant);
		return asset;
	}

	PathSpline Polyline(const std::vector<std::pair<float, float>>& points)
	{
		PathSpline spline;
		spline.interpolation = SplineInterpolation::Linear;
		for (const auto& point : points)
		{
			SplinePoint p;
			p.x = point.first;
			p.z = point.second;
			spline.points.push_back(p);
		}
		return spline;
	}

	std::vector<uint64> PointIds(const PathSpline& spline)
	{
		std::vector<uint64> ids;
		for (size_t i = 0; i < spline.points.size(); ++i)
		{
			ids.push_back(500 + i);
		}
		return ids;
	}

	SectionPathShape Prepare(const PathSpline& spline, const SectionAsset& asset, const SectionPathParams& params = SectionPathParams())
	{
		SectionPathShape shape;
		String error;
		REQUIRE(PrepareSectionPath(spline, PointIds(spline), asset, params, {}, shape, error));
		return shape;
	}

	std::vector<Placement> Build(const SectionPathShape& shape, const float groundHeight = 3.0f)
	{
		std::vector<Placement> out;
		BuildSectionPlacements(shape, [groundHeight](float, float) { return groundHeight; }, out);

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

	std::vector<Placement> Build(const PathSpline& spline, const SectionAsset& asset, const SectionPathParams& params = SectionPathParams())
	{
		return Build(Prepare(spline, asset, params));
	}

	/// A laid piece: where its footprint starts and ends (x/z), from its pivot, rotation and the authored footprint.
	struct LaidPiece
	{
		Vector3 start;
		Vector3 end;
		const Placement* placement = nullptr;
	};

	/// Pieces with the footprint (startOffset, length) chained in order: each next piece starts nearest to the
	/// previous piece's end. Starts from the piece whose start is nearest to `from`.
	std::vector<LaidPiece> Chain(const std::vector<Placement>& placements, const float startOffset, const float length, const Vector3& from)
	{
		std::vector<LaidPiece> pieces;
		for (const Placement& p : placements)
		{
			LaidPiece piece;
			const Vector3 dir = p.rotation * Vector3::UnitZ;
			piece.start = p.position + dir * startOffset;
			piece.end = piece.start + dir * length;
			piece.start.y = 0.0f;
			piece.end.y = 0.0f;
			piece.placement = &p;
			pieces.push_back(piece);
		}

		std::vector<LaidPiece> ordered;
		Vector3 cursor = from;
		while (!pieces.empty())
		{
			size_t best = 0;
			for (size_t i = 1; i < pieces.size(); ++i)
			{
				if ((pieces[i].start - cursor).GetLength() < (pieces[best].start - cursor).GetLength())
				{
					best = i;
				}
			}
			ordered.push_back(pieces[best]);
			cursor = pieces[best].end;
			pieces.erase(pieces.begin() + static_cast<std::ptrdiff_t>(best));
		}
		return ordered;
	}

	float Distance2D(const Vector3& a, const Vector3& b)
	{
		return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z));
	}

	void CheckEndToEnd(const std::vector<LaidPiece>& pieces, const float length)
	{
		for (size_t i = 0; i < pieces.size(); ++i)
		{
			CHECK(Distance2D(pieces[i].start, pieces[i].end) == Approx(length).margin(1e-3f));
			if (i + 1 < pieces.size())
			{
				CHECK(Distance2D(pieces[i].end, pieces[i + 1].start) < 1e-3f);
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Asset format
// ---------------------------------------------------------------------------------------------------------------

TEST_CASE("Section runs round trip through the asset", "[section_runs]")
{
	SectionAsset asset = MakeRunAsset({ MakeRun("Fence", -4.6f, 0.0f, 5), MakeRun("Wall", 4.0f, 2.0f, 2, 3.0f, 0.25f, 9100) });
	asset.name = "road";
	SectionPlacement rock;
	rock.placement.key = 7;
	rock.placement.asset = "Rocks/a.hmsh";
	asset.variants[0].placements.push_back(rock);
	SectionRunPiece& special = asset.variants[0].runs[1].pieces[1];
	special.placement.kind = PlacementKind::WorldModel;
	special.placement.kitPaths = { "Kits/a.hkit" };
	special.placement.rotation = Quaternion(Radian(1.5707963f), Vector3::UnitY);
	special.placement.scale = Vector3(1.0f, 2.0f, 1.0f);
	special.heightAboveTerrain = 0.75f;
	asset.variants[0].runs[0].pieces[0].placement.materialOverrides = { { 2, "Mat/m.hmat" } };

	const std::vector<char> bytes = Serialize(asset);
	SectionAsset loaded;
	REQUIRE(Deserialize(bytes, loaded));
	REQUIRE(loaded.variants.size() == 1);
	CHECK(loaded.variants[0].placements.size() == 1);
	REQUIRE(loaded.variants[0].runs.size() == 2);
	for (size_t r = 0; r < 2; ++r)
	{
		const SectionRun& a = asset.variants[0].runs[r];
		const SectionRun& b = loaded.variants[0].runs[r];
		CHECK(b.name == a.name);
		CHECK(b.keepChance == a.keepChance);
		CHECK(b.x == a.x);
		CHECK(b.startZ == a.startZ);
		CHECK(b.endZ == a.endZ);
		REQUIRE(b.pieces.size() == a.pieces.size());
		for (size_t p = 0; p < a.pieces.size(); ++p)
		{
			CHECK(b.pieces[p].placement.key == a.pieces[p].placement.key);
			CHECK(b.pieces[p].placement.kind == a.pieces[p].placement.kind);
			CHECK(b.pieces[p].placement.asset == a.pieces[p].placement.asset);
			CHECK((b.pieces[p].placement.rotation == a.pieces[p].placement.rotation));
			CHECK((b.pieces[p].placement.scale == a.pieces[p].placement.scale));
			CHECK(b.pieces[p].placement.kitPaths == a.pieces[p].placement.kitPaths);
			CHECK(b.pieces[p].placement.materialOverrides == a.pieces[p].placement.materialOverrides);
			CHECK(b.pieces[p].heightAboveTerrain == a.pieces[p].heightAboveTerrain);
			CHECK(b.pieces[p].length == a.pieces[p].length);
			CHECK(b.pieces[p].startOffset == a.pieces[p].startOffset);
		}
	}
	CHECK(Serialize(loaded) == bytes);

	for (size_t size = 0; size < bytes.size(); ++size)
	{
		SectionAsset out;
		CHECK_FALSE(Deserialize(std::vector<char>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size)), out));
	}
}

TEST_CASE("Version 1 section assets read with no runs", "[section_runs]")
{
	SectionAsset asset = MakeRunAsset({});
	SectionPlacement rock;
	rock.placement.key = 7;
	rock.placement.asset = "Rocks/a.hmsh";
	asset.variants[0].placements.push_back(rock);

	// A version 1 file is the version 3 file without the trailer (corner angle + one role byte for the only
	// variant) and without the trailing run count of that variant.
	std::vector<char> bytes = Serialize(asset);
	REQUIRE(bytes[4] == 3);
	bytes[4] = 1;
	bytes.resize(bytes.size() - 5 - 4);

	SectionAsset loaded;
	REQUIRE(Deserialize(bytes, loaded));
	REQUIRE(loaded.variants.size() == 1);
	CHECK(loaded.variants[0].placements.size() == 1);
	CHECK(loaded.variants[0].runs.empty());

	bytes[4] = 4;
	CHECK_FALSE(Deserialize(bytes, loaded));
}

TEST_CASE("A run is continuous when it covers the section within half a metre", "[section_runs]")
{
	SectionRun run;
	run.startZ = 0.5f;
	run.endZ = 19.5f;
	CHECK(IsContinuousRun(run, 20.0f));
	run.startZ = 0.6f;
	CHECK_FALSE(IsContinuousRun(run, 20.0f));
	run.startZ = -0.3f;
	run.endZ = 19.4f;
	CHECK_FALSE(IsContinuousRun(run, 20.0f));
	run.endZ = 20.4f;
	CHECK(IsContinuousRun(run, 20.0f));
}

// ---------------------------------------------------------------------------------------------------------------
// Bake and definition
// ---------------------------------------------------------------------------------------------------------------

namespace
{
	SectionDefinition RunDefinition()
	{
		SectionDefinition def;
		def.name = "road";
		def.length = 20.0f;
		def.halfWidth = 4.0f;
		def.sideFalloff = 4.0f;
		def.variants = { { 10.0f }, { 60.0f } };
		PropRule fenceRule;
		fenceRule.asset = "Props/fence.hmsh";
		fenceRule.keepChance = 0.0f;
		def.rules = { fenceRule };
		SectionRunDef run;
		run.name = "Fence";
		run.keepChance = 0.75f;
		run.members = { 5, 6, 7, 99 };
		def.runs = { run };
		return def;
	}

	Placement WorldPlacement(const uint64 key, const float x, const float y, const float z, const PlacementKind kind = PlacementKind::Entity,
		const String& asset = "Props/fence.hmsh")
	{
		Placement p;
		p.key = key;
		p.kind = kind;
		p.asset = asset;
		p.position = Vector3(x, y, z);
		return p;
	}

	std::vector<SectionPieceExtent> Extents(const std::vector<uint64>& keys, const float length = 4.0f, const float startOffset = -2.0f)
	{
		std::vector<SectionPieceExtent> out;
		for (const uint64 key : keys)
		{
			out.push_back(SectionPieceExtent{ key, length, startOffset });
		}
		return out;
	}
}

TEST_CASE("BakeSection turns run members into runs", "[section_runs]")
{
	FakeTerrain terrain(10);
	std::fill(terrain.m_data.outer.begin(), terrain.m_data.outer.end(), 0.0f);
	std::fill(terrain.m_data.inner.begin(), terrain.m_data.inner.end(), 0.0f);
	std::fill(terrain.m_data.splat.begin(), terrain.m_data.splat.end(), kDefaultPaint);

	const std::vector<Placement> world = {
		WorldPlacement(5, 10.0f - 4.6f, 0.2f, 9.0f),
		WorldPlacement(6, 10.0f - 4.6f, 0.2f, 1.0f),
		WorldPlacement(7, 10.0f - 4.4f, 0.2f, 5.0f),
		// Foliage shares the id space only by accident: it never joins a run.
		WorldPlacement(6, 12.0f, 0.0f, 3.0f, PlacementKind::Foliage, "Foliage/bush"),
		WorldPlacement(8, 12.0f, 0.0f, 3.0f, PlacementKind::Entity, "Rocks/a.hmsh"),
	};

	SectionAsset asset;
	String error;
	size_t missing = 0;
	REQUIRE(BakeSection(RunDefinition(), terrain, world, Extents({ 5, 6, 7 }), kDefaultPaint, kTextures, 1, asset, error, nullptr, &missing));
	CHECK(missing == 1);
	REQUIRE(asset.variants.size() == 2);
	CHECK(asset.variants[1].runs.empty());

	const SectionVariant& v = asset.variants[0];
	REQUIRE(v.placements.size() == 2);
	CHECK(v.placements[0].placement.kind == PlacementKind::Foliage);
	CHECK(v.placements[1].placement.key == 8);

	REQUIRE(v.runs.size() == 1);
	const SectionRun& run = v.runs[0];
	CHECK(run.name == "Fence");
	CHECK(run.keepChance == 0.75f);
	CHECK(run.x == Approx((-4.6f - 4.6f - 4.4f) / 3.0f));
	CHECK(run.startZ == Approx(-1.0f));
	CHECK(run.endZ == Approx(11.0f));
	REQUIRE(run.pieces.size() == 3);
	CHECK(run.pieces[0].placement.key == 6);
	CHECK(run.pieces[1].placement.key == 7);
	CHECK(run.pieces[2].placement.key == 5);
	CHECK(run.pieces[1].placement.position.x == Approx(-4.4f));
	CHECK(run.pieces[1].placement.position.z == Approx(5.0f));
	CHECK(run.pieces[1].heightAboveTerrain == Approx(0.2f));
	CHECK(run.pieces[1].length == 4.0f);
	CHECK(run.pieces[1].startOffset == -2.0f);
	CHECK_FALSE(IsContinuousRun(run, 20.0f));
}

TEST_CASE("BakeSection refuses broken runs", "[section_runs]")
{
	FakeTerrain terrain(10);
	const std::vector<Placement> world = {
		WorldPlacement(5, 6.0f, 0.0f, 9.0f),
		WorldPlacement(6, 6.0f, 0.0f, 1.0f),
		WorldPlacement(7, 56.0f, 0.0f, 5.0f),
		WorldPlacement(8, 30.0f, 0.0f, 5.0f),
	};
	const auto bake = [&](const SectionDefinition& def, const std::vector<SectionPieceExtent>& extents)
	{
		SectionAsset asset;
		String error;
		const bool ok = BakeSection(def, terrain, world, extents, kDefaultPaint, kTextures, 1, asset, error);
		CHECK((ok || !error.empty()));
		return ok;
	};

	SectionDefinition def = RunDefinition();
	def.runs[0].members = { 5, 6 };
	CHECK(bake(def, Extents({ 5, 6 })));

	SECTION("a member without an extent")
	{
		CHECK_FALSE(bake(def, Extents({ 5 })));
		CHECK_FALSE(bake(def, Extents({ 5, 6 }, 0.01f)));
	}
	SECTION("members in different variants")
	{
		def.runs[0].members = { 5, 7 };
		CHECK_FALSE(bake(def, Extents({ 5, 7 })));
	}
	SECTION("a member outside every variant")
	{
		def.runs[0].members = { 5, 8 };
		CHECK_FALSE(bake(def, Extents({ 5, 8 })));
	}
	SECTION("a member in two runs")
	{
		SectionRunDef other;
		other.name = "Other";
		other.members = { 6 };
		def.runs.push_back(other);
		CHECK_FALSE(bake(def, Extents({ 5, 6 })));
	}
	SECTION("two runs of one variant with the same name")
	{
		def.runs[0].members = { 5 };
		SectionRunDef other;
		other.name = "Fence";
		other.members = { 6 };
		def.runs.push_back(other);
		CHECK_FALSE(bake(def, Extents({ 5, 6 })));
	}
	SECTION("a run without a name")
	{
		def.runs[0].name.clear();
		CHECK_FALSE(bake(def, Extents({ 5, 6 })));
	}
}

TEST_CASE("Section definition runs round trip", "[section_runs]")
{
	SectionDefinition def = RunDefinition();
	def.runs[0].members = { 6829427359930646530ull, 5, 6829427359930646529ull, 5 };
	SectionRunDef second;
	second.name = "Wall";
	second.keepChance = 1.7f;
	def.runs.push_back(second);

	const String json = WriteSectionDefinition(def);
	SectionDefinition parsed;
	REQUIRE(ParseSectionDefinition(json, parsed));
	REQUIRE(parsed.runs.size() == 2);
	CHECK(parsed.runs[0].name == "Fence");
	CHECK(parsed.runs[0].keepChance == 0.75f);
	CHECK(parsed.runs[0].members == std::vector<uint64>{ 5, 6829427359930646529ull, 6829427359930646530ull });
	CHECK(parsed.runs[1].name == "Wall");
	CHECK(parsed.runs[1].keepChance == 1.0f);
	CHECK(parsed.runs[1].members.empty());

	// Writing the parsed definition is stable.
	const String canonical = WriteSectionDefinition(parsed);
	SectionDefinition reparsed;
	REQUIRE(ParseSectionDefinition(canonical, reparsed));
	CHECK(WriteSectionDefinition(reparsed) == canonical);
	CHECK(CanonicalSectionDefinitionText(json) == canonical);

	// Without runs the document has no "runs" key, so existing definitions keep their text.
	def.runs.clear();
	CHECK(WriteSectionDefinition(def).find("\"runs\"") == String::npos);
}

// ---------------------------------------------------------------------------------------------------------------
// Laying runs along a path
// ---------------------------------------------------------------------------------------------------------------

TEST_CASE("A continuous run is laid end to end over consecutive copies and spans", "[section_runs]")
{
	// Spans of 40 and 20 m: three copies of 20 m, the run continues across the span boundary.
	const std::vector<Placement> out = Build(Polyline({ { 0.0f, 0.0f }, { 0.0f, 40.0f }, { 0.0f, 60.0f } }), MakeRunAsset({ MakeRun("Fence", -4.6f, 0.0f, 5) }));
	REQUIRE(out.size() == 15);

	const std::vector<LaidPiece> pieces = Chain(out, -2.0f, 4.0f, Vector3(-4.6f, 0.0f, 0.0f));
	CheckEndToEnd(pieces, 4.0f);
	CHECK(Distance2D(pieces.front().start, Vector3(-4.6f, 0.0f, 0.0f)) < 1e-3f);
	CHECK(Distance2D(pieces.back().end, Vector3(-4.6f, 0.0f, 60.0f)) < 1e-3f);
	for (const LaidPiece& piece : pieces)
	{
		CHECK(piece.placement->position.x == Approx(-4.6f).margin(1e-4f));
		CHECK(piece.placement->position.y == Approx(3.0f));
		const Vector3 forward = piece.placement->rotation * Vector3::UnitZ;
		CHECK(forward.x == Approx(0.0f).margin(1e-4f));
		CHECK(forward.z == Approx(1.0f).margin(1e-4f));
		CHECK(piece.placement->asset == "Props/fence.hmsh");
	}
}

TEST_CASE("A continuous run keeps its piece length on stretched copies", "[section_runs]")
{
	// One 45 m span: two copies stretched to 22.5 m (a 50 m span rounds to three squashed copies); the pieces keep
	// 4 m and fill the chain: 11 pieces end at 44 m, a 12th would end 3 m (more than half a piece) past it.
	const std::vector<Placement> out = Build(Polyline({ { 10.0f, 0.0f }, { 10.0f, 45.0f } }), MakeRunAsset({ MakeRun("Fence", -4.6f, 0.0f, 5) }));
	const std::vector<LaidPiece> pieces = Chain(out, -2.0f, 4.0f, Vector3(5.4f, 0.0f, 0.0f));
	REQUIRE(pieces.size() == 11);
	CheckEndToEnd(pieces, 4.0f);
	CHECK(std::fabs(pieces.back().end.z - 45.0f) <= 2.0f + 1e-3f);
}

TEST_CASE("A run follows a curved path piece by piece", "[section_runs]")
{
	// Quarter circle of radius 40 around (100, 100), counter-clockwise from angle 0: left (x < 0) is inside.
	std::vector<std::pair<float, float>> points;
	constexpr uint32 kSegments = 64;
	for (uint32 i = 0; i <= kSegments; ++i)
	{
		const float angle = 1.5707963f * static_cast<float>(i) / static_cast<float>(kSegments);
		points.emplace_back(100.0f + 40.0f * std::cos(angle), 100.0f + 40.0f * std::sin(angle));
	}
	const PathSpline spline = Polyline(points);
	const std::vector<Placement> out = Build(spline, MakeRunAsset({ MakeRun("Fence", -4.6f, 0.0f, 5) }));
	REQUIRE(out.size() > 10);

	const std::vector<LaidPiece> pieces = Chain(out, -2.0f, 4.0f, Vector3(135.4f, 0.0f, 100.0f));
	REQUIRE(pieces.size() == out.size());
	CheckEndToEnd(pieces, 4.0f);
	for (size_t i = 0; i < pieces.size(); ++i)
	{
		// Starts lie on the offset line (radius 40 - 4.6, within the polyline's sagitta).
		const float radius = Distance2D(pieces[i].start, Vector3(100.0f, 0.0f, 100.0f));
		CHECK(radius == Approx(35.4f).margin(0.1f));

		// Yaw = chord direction: the piece points from its start to the next piece's start.
		if (i + 1 < pieces.size())
		{
			const Vector3 forward = pieces[i].placement->rotation * Vector3::UnitZ;
			const float chordX = pieces[i + 1].start.x - pieces[i].start.x;
			const float chordZ = pieces[i + 1].start.z - pieces[i].start.z;
			const float chord = std::sqrt(chordX * chordX + chordZ * chordZ);
			CHECK(forward.x == Approx(chordX / chord).margin(1e-3f));
			CHECK(forward.z == Approx(chordZ / chord).margin(1e-3f));
			CHECK(forward.y == Approx(0.0f).margin(1e-4f));
		}
	}
}

TEST_CASE("A run's authored rotation is kept relative to the run direction", "[section_runs]")
{
	SectionRun run = MakeRun("Fence", 4.0f, 0.0f, 5);
	for (SectionRunPiece& piece : run.pieces)
	{
		piece.placement.rotation = Quaternion(Radian(1.5707963f), Vector3::UnitY);
		piece.heightAboveTerrain = 0.25f;
	}
	// Path along +X: the run direction +Z maps to +X, and the piece rotation is that yaw times the authored rotation.
	const std::vector<Placement> out = Build(Polyline({ { 0.0f, 0.0f }, { 60.0f, 0.0f } }), MakeRunAsset({ run }));
	REQUIRE(out.size() == 15);
	const Quaternion expected = Quaternion(Radian(1.5707963f), Vector3::UnitY) * Quaternion(Radian(1.5707963f), Vector3::UnitY);
	for (const Placement& p : out)
	{
		const Vector3 a = p.rotation * Vector3::UnitX;
		const Vector3 b = expected * Vector3::UnitX;
		CHECK(a.x == Approx(b.x).margin(1e-4f));
		CHECK(a.z == Approx(b.z).margin(1e-4f));
		CHECK(p.position.y == Approx(3.25f));
		// Right of a path along +X is -Z.
		CHECK(p.position.z == Approx(-4.0f).margin(1e-4f));
	}
}

TEST_CASE("A run is kept or dropped as a whole chain", "[section_runs]")
{
	const PathSpline spline = Polyline({ { 0.0f, 0.0f }, { 0.0f, 40.0f }, { 0.0f, 60.0f } });
	uint32 kept = 0;
	uint32 dropped = 0;
	for (uint64 seed = 1; seed <= 40; ++seed)
	{
		SectionPathParams params;
		params.seed = seed;
		const std::vector<Placement> out = Build(spline, MakeRunAsset({ MakeRun("Fence", -4.6f, 0.0f, 5, 4.0f, 0.5f) }), params);
		REQUIRE((out.empty() || out.size() == 15));
		(out.empty() ? dropped : kept)++;
	}
	CHECK(kept > 0);
	CHECK(dropped > 0);

	SectionPathParams none;
	none.propDensity = 0.0f;
	CHECK(Build(spline, MakeRunAsset({ MakeRun("Fence", -4.6f, 0.0f, 5) }), none).empty());
}

TEST_CASE("A non-continuous run stays inside its copy and is rolled per copy", "[section_runs]")
{
	// Two 48 m spans: four copies of 24 m (stretch 1.2). The run covers local z 4..16 = arc 4.8..19.2 of each copy.
	const PathSpline spline = Polyline({ { 0.0f, 0.0f }, { 0.0f, 48.0f }, { 0.0f, 96.0f } });
	uint32 kept = 0;
	uint32 dropped = 0;
	for (uint64 seed = 1; seed <= 20; ++seed)
	{
		SectionPathParams params;
		params.seed = seed;
		const std::vector<Placement> out = Build(spline, MakeRunAsset({ MakeRun("Fence", 4.0f, 4.0f, 3, 4.0f, 0.5f) }), params);
		uint32 perCopy[4] = { 0, 0, 0, 0 };
		for (const Placement& p : out)
		{
			const float start = p.position.z - 2.0f;
			const float end = p.position.z + 2.0f;
			const uint32 copy = static_cast<uint32>(std::floor(start / 24.0f));
			REQUIRE(copy < 4);
			CHECK(start >= copy * 24.0f + 4.8f - 1e-3f);
			CHECK(end <= copy * 24.0f + 24.0f);
			++perCopy[copy];
		}
		for (const uint32 count : perCopy)
		{
			// 14.4 m of run: 4 pieces of 4 m (the last one ends 1.6 m past the run end, within half a piece).
			REQUIRE((count == 0 || count == 4));
			(count == 0 ? dropped : kept)++;
		}
	}
	CHECK(kept > 0);
	CHECK(dropped > 0);
}

TEST_CASE("Runs ignore prop rules and are deterministic", "[section_runs]")
{
	PropRule rule;
	rule.asset = "Props/fence.hmsh";
	rule.keepChance = 0.0f;
	rule.positionJitter = 3.0f;
	const SectionAsset asset = MakeRunAsset({ MakeRun("Fence", -4.6f, 0.0f, 5) }, { rule });
	const PathSpline spline = Polyline({ { 0.0f, 0.0f }, { 3.0f, 40.0f }, { -5.0f, 80.0f } });

	const std::vector<Placement> a = Build(spline, asset);
	const std::vector<Placement> b = Build(spline, asset);
	REQUIRE_FALSE(a.empty());
	REQUIRE(a.size() == b.size());
	for (size_t i = 0; i < a.size(); ++i)
	{
		CHECK(a[i].key == b[i].key);
		CHECK((a[i].position == b[i].position));
		CHECK((a[i].rotation == b[i].rotation));
	}
}

TEST_CASE("Non-continuous run keys survive changes to other spans", "[section_runs]")
{
	const SectionAsset asset = MakeRunAsset({ MakeRun("Fence", 4.0f, 4.0f, 3) });
	const std::vector<Placement> before = Build(Polyline({ { 0.0f, 0.0f }, { 0.0f, 40.0f }, { 0.0f, 80.0f } }), asset);
	const std::vector<Placement> after = Build(Polyline({ { 0.0f, 0.0f }, { 0.0f, 40.0f }, { 10.0f, 85.0f } }), asset);

	std::set<uint64> afterKeys;
	for (const Placement& p : after)
	{
		afterKeys.insert(p.key);
	}
	uint32 firstSpan = 0;
	for (const Placement& p : before)
	{
		if (p.position.z < 40.0f)
		{
			++firstSpan;
			CHECK(afterKeys.count(p.key) == 1);
		}
	}
	CHECK(firstSpan == 6);
}
