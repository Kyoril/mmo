// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/graph_instance_record.h"
#include "pcg/graph/json_util.h"
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
	wrongVersion[4] = 3;
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

TEST_CASE("GraphInstanceRecord keeps borderFade and defaults it for older records", "[graph_instance_record]")
{
	GraphInstanceRecord original = MakePopulated();
	CHECK(original.borderFade == 8.0f);
	original.borderFade = 3.5f;
	const std::vector<char> bytes = Serialize(original);

	GraphInstanceRecord loaded;
	REQUIRE(Deserialize(bytes, loaded));
	CHECK(loaded.borderFade == 3.5f);
	CHECK(Serialize(loaded) == bytes);

	// Layout: magic, version, id, length-prefixed JSON, the rest.
	uint32 length = 0;
	std::memcpy(&length, bytes.data() + 16, sizeof(length));
	graph::jsonutil::Json blob = graph::jsonutil::Json::parse(String(bytes.begin() + 20, bytes.begin() + 20 + length), nullptr, false);
	REQUIRE(blob.is_object());
	REQUIRE(blob.contains("borderFade"));

	const auto splice = [&](const graph::jsonutil::Json& replacement)
	{
		const String text = graph::jsonutil::Dump(replacement);
		std::vector<char> result(bytes.begin(), bytes.begin() + 16);
		const uint32 newLength = static_cast<uint32>(text.size());
		result.insert(result.end(), reinterpret_cast<const char*>(&newLength), reinterpret_cast<const char*>(&newLength) + sizeof(newLength));
		result.insert(result.end(), text.begin(), text.end());
		result.insert(result.end(), bytes.begin() + 20 + length, bytes.end());
		return result;
	};

	// Written before the field existed: the default.
	graph::jsonutil::Json old = blob;
	old.erase("borderFade");
	GraphInstanceRecord legacy;
	legacy.borderFade = 1.0f;
	REQUIRE(Deserialize(splice(old), legacy));
	CHECK(legacy.borderFade == 8.0f);
	CHECK(legacy.name == original.name);

	graph::jsonutil::Json zero = blob;
	zero["borderFade"] = 0;
	REQUIRE(Deserialize(splice(zero), loaded));
	CHECK(loaded.borderFade == 0.0f);

	graph::jsonutil::Json negative = blob;
	negative["borderFade"] = -1.0;
	CHECK_FALSE(Deserialize(splice(negative), loaded));

	graph::jsonutil::Json text = blob;
	text["borderFade"] = "wide";
	CHECK_FALSE(Deserialize(splice(text), loaded));
}

namespace
{
	terrain_io::water_flow::FlowPath MakeFlowPath(const float x0, const float z0, const size_t count)
	{
		terrain_io::water_flow::FlowPath path;
		for (size_t i = 0; i < count; ++i)
		{
			terrain_io::water_flow::FlowPoint p;
			p.x = x0 + static_cast<float>(i) * 12.5f;
			p.z = z0 - static_cast<float>(i) * 3.25f;
			p.halfWidth = 4.0f + static_cast<float>(i) * 0.5f;
			path.push_back(p);
		}
		return path;
	}

	bool SamePaths(const std::vector<terrain_io::water_flow::FlowPath>& a, const std::vector<terrain_io::water_flow::FlowPath>& b)
	{
		if (a.size() != b.size())
		{
			return false;
		}
		for (size_t i = 0; i < a.size(); ++i)
		{
			if (a[i].size() != b[i].size())
			{
				return false;
			}
			for (size_t j = 0; j < a[i].size(); ++j)
			{
				if (a[i][j].x != b[i][j].x || a[i][j].z != b[i][j].z || a[i][j].halfWidth != b[i][j].halfWidth)
				{
					return false;
				}
			}
		}
		return true;
	}
}

TEST_CASE("GraphInstanceRecord round-trips flow paths", "[graph_instance_record]")
{
	GraphInstanceRecord original = MakePopulated();
	original.flowPaths.push_back(MakeFlowPath(10.0f, 20.0f, 3));
	original.flowPaths.push_back(MakeFlowPath(-40.0f, 5.5f, 5));
	const std::vector<char> bytes = Serialize(original);
	CHECK(bytes[4] == 2);

	GraphInstanceRecord loaded;
	REQUIRE(Deserialize(bytes, loaded));
	CHECK(SamePaths(loaded.flowPaths, original.flowPaths));
	CHECK(loaded.content.placements.size() == 1);
	CHECK(Serialize(loaded) == bytes);

	// Truncated inside the paths.
	const std::vector<char> truncated(bytes.begin(), bytes.end() - 4);
	CHECK_FALSE(Deserialize(truncated, loaded));
}

TEST_CASE("GraphInstanceRecord writes deterministic bytes with flow paths", "[graph_instance_record]")
{
	GraphInstanceRecord a = MakePopulated();
	a.flowPaths.push_back(MakeFlowPath(1.0f, 2.0f, 4));
	GraphInstanceRecord b = MakePopulated();
	b.flowPaths.push_back(MakeFlowPath(1.0f, 2.0f, 4));
	const std::vector<char> bytesA = Serialize(a);
	CHECK(bytesA == Serialize(b));
	CHECK(bytesA == Serialize(a));

	// Without paths the record still writes version 2, ending in a zero path count.
	const std::vector<char> empty = Serialize(MakePopulated());
	REQUIRE(empty.size() >= 8);
	CHECK(empty[4] == 2);
	uint32 trailing = 0xFFFFFFFFu;
	std::memcpy(&trailing, empty.data() + empty.size() - 4, sizeof(trailing));
	CHECK(trailing == 0);
	// Layout: one path count already present, then per path a point count and three floats per point.
	CHECK(bytesA.size() == empty.size() + 4 + 4 * 3 * sizeof(float));
}

TEST_CASE("GraphInstanceRecord version 1 reads with no flow paths", "[graph_instance_record]")
{
	// A version 1 blob is version 2 without the trailing path count.
	const std::vector<char> v2 = Serialize(MakePopulated());
	std::vector<char> v1(v2.begin(), v2.end() - 4);
	v1[4] = 1;

	GraphInstanceRecord loaded;
	loaded.flowPaths.push_back(MakeFlowPath(0.0f, 0.0f, 2));
	REQUIRE(Deserialize(v1, loaded));
	CHECK(loaded.flowPaths.empty());
	CHECK(loaded.name == "Forest \"North\"");
	CHECK(loaded.content.placements.size() == 1);
	// Written again, it upgrades to version 2.
	CHECK(Serialize(loaded) == v2);
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

TEST_CASE("GraphInstanceRecord round-trips seamHash and reads a missing key as 0", "[graph_instance_record]")
{
	GraphInstanceRecord original = MakePopulated();
	CHECK(original.seamHash == 0);
	const std::vector<char> plain = Serialize(original);

	original.seamHash = 0xfedcba9876543210ull;
	const std::vector<char> bytes = Serialize(original);
	GraphInstanceRecord loaded;
	REQUIRE(Deserialize(bytes, loaded));
	CHECK(loaded.seamHash == 0xfedcba9876543210ull);
	CHECK(Serialize(loaded) == bytes);

	// Without seams the key is not written, so older bytes stay identical and read as 0.
	loaded.seamHash = 5;
	REQUIRE(Deserialize(plain, loaded));
	CHECK(loaded.seamHash == 0);
	CHECK(Serialize(loaded) == plain);
}
