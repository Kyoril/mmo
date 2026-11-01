// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/graph_instance_record.h"
#include "pcg/section_path_record.h"
#include "binary_io/memory_source.h"
#include "binary_io/vector_sink.h"

#include <cstring>

using namespace mmo;
using namespace mmo::pcg;

namespace
{
	PathSpline Square(const float minX, const float minZ, const float maxX, const float maxZ)
	{
		PathSpline spline;
		spline.closed = true;
		spline.interpolation = SplineInterpolation::Linear;
		for (const auto& corner : { std::pair<float, float>(minX, minZ), std::pair<float, float>(maxX, minZ),
			std::pair<float, float>(maxX, maxZ), std::pair<float, float>(minX, maxZ) })
		{
			SplinePoint p;
			p.x = corner.first;
			p.z = corner.second;
			spline.points.push_back(p);
		}
		return spline;
	}

	PathSpline Polygon(const std::vector<std::pair<float, float>>& corners)
	{
		PathSpline spline;
		spline.closed = true;
		spline.interpolation = SplineInterpolation::Linear;
		for (const auto& corner : corners)
		{
			SplinePoint p;
			p.x = corner.first;
			p.z = corner.second;
			spline.points.push_back(p);
		}
		return spline;
	}

	GraphInstanceRecord MakePopulated()
	{
		GraphInstanceRecord r;
		r.id = 42;
		r.name = "Forest \"North\"";
		r.graphPath = "Editor/Pcg/Biomes/FalwynForest/Graphs/Forest.hpcgg";
		r.area = Square(0.0f, 0.0f, 100.0f, 80.0f);
		r.area.interpolation = SplineInterpolation::CatmullRom;
		r.pointIds = { 1, 4, 5, 9 };
		r.nextPointId = 10;
		r.seed = 0xABCDEF0123456789ull;
		r.pointLimit = 12345;
		r.parameterOverrides["density"] = 0.25f;
		r.parameterOverrides["count"] = int64(-77);
		r.parameterOverrides["enabled"] = true;
		r.parameterOverrides["offset"] = Vector3(1.0f, -2.0f, 3.5f);
		r.parameterOverrides["label"] = String("oak");

		r.content.influence = WorldRect{ -5.0f, -6.0f, 105.0f, 90.0f };
		OwnedPlacement op;
		op.key = 11;
		op.uniqueId = 111;
		op.kind = PlacementKind::Entity;
		op.written.key = 11;
		op.written.asset = "Tree.hmsh";
		op.written.position = Vector3(1.0f, 2.0f, 3.0f);
		op.written.kitPaths = { "Kits/Tree.hkit" };
		r.content.placements.push_back(op);
		return r;
	}

	std::vector<char> Serialize(const GraphInstanceRecord& record)
	{
		std::vector<char> buffer;
		io::VectorSink<char> sink(buffer);
		io::Writer writer(sink);
		REQUIRE(WriteGraphInstanceRecord(writer, record));
		return buffer;
	}

	bool Deserialize(const std::vector<char>& bytes, GraphInstanceRecord& out)
	{
		io::MemorySource source(bytes);
		io::Reader reader(source);
		return ReadGraphInstanceRecord(reader, out);
	}
}

TEST_CASE("GraphInstanceRecord round trip is byte identical", "[graph_instance_record]")
{
	const GraphInstanceRecord original = MakePopulated();
	const std::vector<char> bytes = Serialize(original);
	REQUIRE(Serialize(original) == bytes);

	GraphInstanceRecord loaded;
	REQUIRE(Deserialize(bytes, loaded));
	CHECK(loaded.id == 42);
	CHECK(loaded.name == original.name);
	CHECK(loaded.graphPath == original.graphPath);
	CHECK(loaded.seed == original.seed);
	CHECK(loaded.pointLimit == 12345);
	CHECK(loaded.area.closed);
	CHECK(loaded.area.interpolation == SplineInterpolation::CatmullRom);
	REQUIRE(loaded.area.points.size() == 4);
	CHECK(loaded.area.points[2].x == 100.0f);
	CHECK(loaded.area.points[2].z == 80.0f);
	CHECK(loaded.pointIds == original.pointIds);
	CHECK(loaded.nextPointId == 10);

	REQUIRE(loaded.parameterOverrides.size() == 5);
	CHECK(std::get<float>(loaded.parameterOverrides.at("density")) == 0.25f);
	CHECK(std::get<int64>(loaded.parameterOverrides.at("count")) == -77);
	CHECK(std::get<bool>(loaded.parameterOverrides.at("enabled")));
	const Vector3 offset = std::get<Vector3>(loaded.parameterOverrides.at("offset"));
	CHECK(offset.x == 1.0f);
	CHECK(offset.y == -2.0f);
	CHECK(offset.z == 3.5f);
	CHECK(std::get<String>(loaded.parameterOverrides.at("label")) == "oak");

	CHECK(loaded.content.influence.minX == -5.0f);
	REQUIRE(loaded.content.placements.size() == 1);
	CHECK(loaded.content.placements[0].written.asset == "Tree.hmsh");
	CHECK(loaded.content.placements[0].written.kitPaths.size() == 1);

	CHECK(Serialize(loaded) == bytes);
}

TEST_CASE("GraphInstanceRecord write refuses mismatched point ids", "[graph_instance_record]")
{
	GraphInstanceRecord bad = MakePopulated();
	bad.pointIds.pop_back();
	std::vector<char> buffer;
	io::VectorSink<char> sink(buffer);
	io::Writer writer(sink);
	CHECK_FALSE(WriteGraphInstanceRecord(writer, bad));
}

TEST_CASE("GraphInstanceRecord read raises nextPointId above the used ids", "[graph_instance_record]")
{
	GraphInstanceRecord r = MakePopulated();
	r.nextPointId = 2;
	GraphInstanceRecord loaded;
	REQUIRE(Deserialize(Serialize(r), loaded));
	CHECK(loaded.nextPointId == 10);
}

TEST_CASE("PeekRecordKind recognises graph instance records", "[graph_instance_record]")
{
	const std::vector<char> bytes = Serialize(MakePopulated());
	io::MemorySource source(bytes);
	io::Reader reader(source);
	CHECK(PeekRecordKind(reader) == RecordKind::GraphInstance);
	// The position is restored, so the record still reads.
	GraphInstanceRecord out;
	CHECK(ReadGraphInstanceRecord(reader, out));
}

TEST_CASE("GraphInstanceRecord rejects truncated data and a wrong version", "[graph_instance_record]")
{
	const std::vector<char> bytes = Serialize(MakePopulated());
	GraphInstanceRecord out;
	for (const size_t cut : { size_t(0), size_t(3), size_t(10), size_t(20), bytes.size() / 2, bytes.size() - 1 })
	{
		const std::vector<char> truncated(bytes.begin(), bytes.begin() + cut);
		CHECK_FALSE(Deserialize(truncated, out));
	}

	std::vector<char> wrongVersion = bytes;
	wrongVersion[4] = 2;
	CHECK_FALSE(Deserialize(wrongVersion, out));

	std::vector<char> wrongMagic = bytes;
	wrongMagic[0] = 'X';
	CHECK_FALSE(Deserialize(wrongMagic, out));
}

TEST_CASE("GraphInstanceRecord ignores unknown JSON keys and rejects broken JSON", "[graph_instance_record]")
{
	const GraphInstanceRecord original = MakePopulated();
	const std::vector<char> bytes = Serialize(original);

	// Layout: magic, version, id, length-prefixed JSON, the rest.
	uint32 length = 0;
	std::memcpy(&length, bytes.data() + 16, sizeof(length));
	const String json(bytes.begin() + 20, bytes.begin() + 20 + length);

	const auto splice = [&](const String& replacement)
	{
		std::vector<char> result(bytes.begin(), bytes.begin() + 16);
		const uint32 newLength = static_cast<uint32>(replacement.size());
		result.insert(result.end(), reinterpret_cast<const char*>(&newLength), reinterpret_cast<const char*>(&newLength) + sizeof(newLength));
		result.insert(result.end(), replacement.begin(), replacement.end());
		result.insert(result.end(), bytes.begin() + 20 + length, bytes.end());
		return result;
	};

	REQUIRE(json.front() == '{');
	GraphInstanceRecord loaded;
	REQUIRE(Deserialize(splice("{\"futureKey\": [1, 2, 3]," + json.substr(1)), loaded));
	CHECK(loaded.name == original.name);
	CHECK(loaded.parameterOverrides.size() == 5);
	CHECK(loaded.content.placements.size() == 1);

	CHECK_FALSE(Deserialize(splice("{not json"), loaded));
	CHECK_FALSE(Deserialize(splice("[]"), loaded));
	CHECK_FALSE(Deserialize(splice("{}"), loaded));
}

TEST_CASE("AreasOverlap separates contact from shared interior", "[graph_instance_record]")
{
	const PathSpline base = Square(0.0f, 0.0f, 10.0f, 10.0f);

	SECTION("disjoint squares")
	{
		CHECK_FALSE(AreasOverlap(base, Square(20.0f, 0.0f, 30.0f, 10.0f)));
	}
	SECTION("squares sharing an edge")
	{
		CHECK_FALSE(AreasOverlap(base, Square(10.0f, 0.0f, 20.0f, 10.0f)));
		CHECK_FALSE(AreasOverlap(base, Square(10.0f, 2.0f, 20.0f, 7.0f)));
	}
	SECTION("squares sharing a corner")
	{
		CHECK_FALSE(AreasOverlap(base, Square(10.0f, 10.0f, 20.0f, 20.0f)));
	}
	SECTION("one square inside the other")
	{
		CHECK(AreasOverlap(base, Square(2.0f, 2.0f, 4.0f, 4.0f)));
		CHECK(AreasOverlap(Square(2.0f, 2.0f, 4.0f, 4.0f), base));
	}
	SECTION("identical squares")
	{
		CHECK(AreasOverlap(base, base));
	}
	SECTION("partial overlap")
	{
		CHECK(AreasOverlap(base, Square(5.0f, 5.0f, 15.0f, 15.0f)));
	}
	SECTION("inner square touching the outer edge")
	{
		CHECK(AreasOverlap(base, Square(0.0f, 2.0f, 4.0f, 4.0f)));
	}
	SECTION("non-convex L-shape against a square in its notch")
	{
		const PathSpline ell = Polygon({ { 0.0f, 0.0f }, { 20.0f, 0.0f }, { 20.0f, 10.0f }, { 10.0f, 10.0f },
			{ 10.0f, 20.0f }, { 0.0f, 20.0f } });
		CHECK_FALSE(AreasOverlap(ell, Square(12.0f, 12.0f, 18.0f, 18.0f)));
		CHECK_FALSE(AreasOverlap(Square(12.0f, 12.0f, 18.0f, 18.0f), ell));
		CHECK_FALSE(AreasOverlap(ell, Square(10.0f, 10.0f, 20.0f, 20.0f)));
		CHECK(AreasOverlap(ell, Square(8.0f, 8.0f, 14.0f, 14.0f)));
	}
	SECTION("collinear partial overlap")
	{
		CHECK(AreasOverlap(Square(0.0f, 0.0f, 10.0f, 10.0f), Square(5.0f, 0.0f, 15.0f, 10.0f)));
	}
	SECTION("slanted edge contact")
	{
		const PathSpline lower = Polygon({ { 0.0f, 0.0f }, { 10.0f, 0.0f }, { 0.0f, 10.0f } });
		const PathSpline upper = Polygon({ { 10.0f, 0.0f }, { 10.0f, 10.0f }, { 0.0f, 10.0f } });
		CHECK_FALSE(AreasOverlap(lower, upper));
	}
	SECTION("slanted shared edge at large coordinates")
	{
		const float o = 20000.0f;
		const PathSpline lower = Polygon({ { o, o }, { o + 10.0f, o }, { o, o + 10.0f } });
		const PathSpline upper = Polygon({ { o + 10.0f, o }, { o + 10.0f, o + 10.0f }, { o, o + 10.0f } });
		CHECK_FALSE(AreasOverlap(lower, upper));
		CHECK_FALSE(AreasOverlap(upper, lower));
		CHECK(AreasOverlap(lower, Polygon({ { o + 9.5f, o }, { o + 10.0f, o }, { o + 10.0f, o + 10.0f }, { o, o + 10.0f }, { o, o + 9.5f } })));
		CHECK(AreasOverlap(lower, Polygon({ { o + 4.5f, o + 4.5f }, { o + 10.0f, o + 10.0f }, { o, o + 10.0f } })));
	}
	SECTION("degenerate areas never overlap")
	{
		PathSpline two = base;
		two.points.resize(2);
		CHECK_FALSE(AreasOverlap(two, base));
		CHECK_FALSE(AreasOverlap(base, PathSpline()));
	}
}
