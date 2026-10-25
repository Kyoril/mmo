// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/oasis_record.h"
#include "pcg/section_path_record.h"
#include "binary_io/memory_source.h"
#include "binary_io/vector_sink.h"

using namespace mmo;
using namespace mmo::pcg;

namespace
{
	SectionPathRecord MakePopulated()
	{
		SectionPathRecord r;
		r.id = 7;
		r.name = "Haven Road";
		r.sectionAsset = "Sections/Road.hsec";
		r.params.seed = 0xABCDEF0123456789ull;
		r.params.sequentialVariants = true;
		r.params.joinBlend = 3.5f;
		r.params.propDensity = 0.75f;
		r.params.heightMode = SectionHeightMode::FollowGround;
		r.spline.closed = false;
		r.spline.interpolation = SplineInterpolation::CatmullRom;
		SplinePoint a;
		a.x = 0.0f; a.z = 0.0f; a.y = 1.0f; a.width = 2.0f;
		SplinePoint b;
		b.x = 20.0f; b.z = 5.0f; b.y = 3.0f; b.autoTangents = false;
		b.inX = -2.0f; b.inY = 0.5f; b.inZ = -1.0f; b.outX = 2.0f; b.outY = -0.5f; b.outZ = 1.0f;
		SplinePoint c;
		c.x = 40.0f; c.z = -8.0f; c.y = 2.0f;
		r.spline.points = { a, b, c };
		r.pointIds = { 1, 5, 9 };
		r.nextPointId = 10;

		r.content.influence = WorldRect{ -5.0f, -6.0f, 45.0f, 20.0f };
		r.content.terrain.outer = { { 1, 2, 3.0f, 4.0f } };
		r.content.terrain.materials = { { 1, 1, String("Old.hmat"), String("Road.hmat") } };
		r.content.terrain.touchedOuter = IntRect{ -1, -2, 3, 4 };

		OwnedPlacement op;
		op.key = 11;
		op.uniqueId = 111;
		op.kind = PlacementKind::Entity;
		op.written.key = 11;
		op.written.asset = "Lamp.hmsh";
		op.written.position = Vector3(1.0f, 2.0f, 3.0f);
		op.written.kitPaths = { "Kits/Lamp.hkit" };
		r.content.placements.push_back(op);
		r.builtRects = { WorldRect{ -5.0f, -6.0f, 22.0f, 14.0f }, WorldRect{ 15.0f, -12.0f, 45.0f, 13.0f } };
		return r;
	}

	std::vector<char> Serialize(const SectionPathRecord& record)
	{
		std::vector<char> buffer;
		io::VectorSink<char> sink(buffer);
		io::Writer writer(sink);
		REQUIRE(WriteSectionPathRecord(writer, record));
		return buffer;
	}

	bool Deserialize(const std::vector<char>& bytes, SectionPathRecord& out)
	{
		io::MemorySource source(bytes);
		io::Reader reader(source);
		return ReadSectionPathRecord(reader, out);
	}

	RecordKind Peek(const std::vector<char>& bytes)
	{
		io::MemorySource source(bytes);
		io::Reader reader(source);
		return PeekRecordKind(reader);
	}
}

TEST_CASE("SectionPathRecord round trip is byte identical", "[section_path_record]")
{
	const SectionPathRecord original = MakePopulated();
	const std::vector<char> bytes = Serialize(original);
	REQUIRE(Serialize(original) == bytes);

	SectionPathRecord loaded;
	REQUIRE(Deserialize(bytes, loaded));
	CHECK(loaded.id == 7);
	CHECK(loaded.name == "Haven Road");
	CHECK(loaded.sectionAsset == "Sections/Road.hsec");
	CHECK(loaded.params.seed == original.params.seed);
	CHECK(loaded.params.sequentialVariants);
	CHECK(loaded.params.heightMode == SectionHeightMode::FollowGround);
	CHECK_FALSE(loaded.spline.closed);
	CHECK(loaded.spline.interpolation == SplineInterpolation::CatmullRom);
	REQUIRE(loaded.spline.points.size() == 3);
	CHECK(loaded.spline.points[1].inY == 0.5f);
	CHECK_FALSE(loaded.spline.points[1].autoTangents);
	CHECK(loaded.pointIds == original.pointIds);
	CHECK(loaded.nextPointId == 10);
	CHECK(loaded.content.placements.size() == 1);
	CHECK(loaded.content.placements[0].written.kitPaths.size() == 1);
	CHECK(loaded.content.terrain.materials[0].written == "Road.hmat");
	REQUIRE(loaded.builtRects.size() == 2);
	CHECK(loaded.builtRects[0].minX == -5.0f);
	CHECK(loaded.builtRects[0].maxZ == 14.0f);
	CHECK(loaded.builtRects[1].minZ == -12.0f);
	CHECK(loaded.builtRects[1].maxX == 45.0f);

	CHECK(Serialize(loaded) == bytes);
}

TEST_CASE("SectionPathRecord round trip keeps a closed loop and the loop settings", "[section_path_record]")
{
	SectionPathRecord original = MakePopulated();
	original.spline.closed = true;
	original.params.heightMode = SectionHeightMode::SmoothedGround;
	original.params.floorInside = false;
	original.params.groundSmoothing = 35.5f;
	const std::vector<char> bytes = Serialize(original);

	SectionPathRecord loaded;
	REQUIRE(Deserialize(bytes, loaded));
	CHECK(loaded.spline.closed);
	CHECK(loaded.spline.interpolation == SplineInterpolation::CatmullRom);
	CHECK(loaded.params.heightMode == SectionHeightMode::SmoothedGround);
	CHECK_FALSE(loaded.params.floorInside);
	CHECK(loaded.params.groundSmoothing == 35.5f);
	CHECK(loaded.builtRects.size() == 2);
	CHECK(Serialize(loaded) == bytes);

	// An unknown height mode is refused (the mode byte follows id, name, asset, seed, sequential, blend, density).
	const size_t modeOffset = 8 + 8 + (4 + original.name.size()) + (4 + original.sectionAsset.size()) + 8 + 1 + 4 + 4;
	REQUIRE(bytes[modeOffset] == static_cast<char>(SectionHeightMode::SmoothedGround));
	std::vector<char> badMode = bytes;
	badMode[modeOffset] = 3;
	CHECK_FALSE(Deserialize(badMode, loaded));
}

TEST_CASE("SectionPathRecord reads versions 1 and 2 as open paths with default loop settings", "[section_path_record]")
{
	// Version 3 appends closed, floorInside and groundSmoothing (6 bytes) after the rect list of version 2, which
	// version 1 does not have.
	const SectionPathRecord original = MakePopulated();
	std::vector<char> bytes = Serialize(original);
	REQUIRE(bytes[4] == 3);
	const size_t loopBytes = 1 + 1 + 4;
	const size_t rectBytes = 4 + 16 * original.builtRects.size();
	REQUIRE(bytes.size() > loopBytes + rectBytes + 8);

	std::vector<char> v2(bytes.begin(), bytes.end() - static_cast<std::ptrdiff_t>(loopBytes));
	v2[4] = 2;
	SectionPathRecord loaded;
	REQUIRE(Deserialize(v2, loaded));
	CHECK_FALSE(loaded.spline.closed);
	CHECK(loaded.params.floorInside);
	CHECK(loaded.params.groundSmoothing == SectionPathParams().groundSmoothing);
	CHECK(loaded.builtRects.size() == 2);

	// Version 2 does not know SmoothedGround.
	std::vector<char> v2Smoothed = v2;
	const size_t modeOffset = 8 + 8 + (4 + original.name.size()) + (4 + original.sectionAsset.size()) + 8 + 1 + 4 + 4;
	v2Smoothed[modeOffset] = static_cast<char>(SectionHeightMode::SmoothedGround);
	CHECK_FALSE(Deserialize(v2Smoothed, loaded));

	// A version 1 file is the version 2 layout without the trailing rect list.
	std::vector<char> v1(v2.begin(), v2.end() - static_cast<std::ptrdiff_t>(rectBytes));
	v1[4] = 1;
	REQUIRE(Deserialize(v1, loaded));
	CHECK(loaded.builtRects.empty());
	CHECK_FALSE(loaded.spline.closed);
	CHECK(loaded.pointIds == original.pointIds);
	CHECK(loaded.content.placements.size() == 1);

	// Rewriting upgrades to version 3 with no rects.
	const std::vector<char> rewritten = Serialize(loaded);
	CHECK(rewritten[4] == 3);
	CHECK(rewritten.size() == v1.size() + 4 + loopBytes);

	// Unknown future versions are refused.
	std::vector<char> v4 = bytes;
	v4[4] = 4;
	CHECK_FALSE(Deserialize(v4, loaded));
}

TEST_CASE("SectionPathRecord rejects mismatched point ids and corrupt data", "[section_path_record]")
{
	SectionPathRecord bad = MakePopulated();
	bad.pointIds.pop_back();
	std::vector<char> buffer;
	io::VectorSink<char> sink(buffer);
	io::Writer writer(sink);
	CHECK_FALSE(WriteSectionPathRecord(writer, bad));

	const std::vector<char> bytes = Serialize(MakePopulated());
	SectionPathRecord out;
	for (const size_t cut : { size_t(0), size_t(3), size_t(20), bytes.size() / 2, bytes.size() - 1 })
	{
		const std::vector<char> truncated(bytes.begin(), bytes.begin() + cut);
		CHECK_FALSE(Deserialize(truncated, out));
	}
}

TEST_CASE("PeekRecordKind identifies the record type", "[section_path_record]")
{
	CHECK(Peek(Serialize(MakePopulated())) == RecordKind::SectionPath);

	std::vector<char> oasisBytes;
	{
		io::VectorSink<char> sink(oasisBytes);
		io::Writer writer(sink);
		REQUIRE(WriteOasisRecord(writer, OasisRecord()));
	}
	CHECK(Peek(oasisBytes) == RecordKind::Oasis);

	CHECK(Peek({ 'x', 'y', 'z', 'w', 'q' }) == RecordKind::Unknown);
	CHECK(Peek({ 'P', 'C' }) == RecordKind::Unknown);
	CHECK(Peek({}) == RecordKind::Unknown);

	// The reader is not advanced.
	const std::vector<char> bytes = Serialize(MakePopulated());
	io::MemorySource source(bytes);
	io::Reader reader(source);
	PeekRecordKind(reader);
	SectionPathRecord loaded;
	CHECK(ReadSectionPathRecord(reader, loaded));
}

TEST_CASE("SectionPathRecord read raises a stale next point id", "[section_path_record]")
{
	SectionPathRecord original = MakePopulated();
	original.nextPointId = 3;

	SectionPathRecord loaded;
	REQUIRE(Deserialize(Serialize(original), loaded));
	CHECK(loaded.nextPointId == 10);

	// A next id above every point id is kept as is.
	original.nextPointId = 42;
	REQUIRE(Deserialize(Serialize(original), loaded));
	CHECK(loaded.nextPointId == 42);
}
