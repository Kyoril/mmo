// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game/bug_report.h"
#include "game/bug_report_compression.h"
#include "game/subsystem.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "binary_io/reader.h"
#include "binary_io/memory_source.h"

#include <random>

using namespace mmo;

namespace
{
	std::vector<char> Serialize(const game::BugReportPayload& payload)
	{
		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		writer << payload;
		return buffer;
	}

	bool Deserialize(const std::vector<char>& buffer, game::BugReportPayload& out)
	{
		io::MemorySource source{ buffer.data(), buffer.data() + buffer.size() };
		io::Reader reader{ source };
		return static_cast<bool>(reader >> out);
	}
}

TEST_CASE("BugReportPayload round-trips every field", "[bug_report]")
{
	game::BugReportPayload payload;
	payload.subjectType = game::bug_report_subject::Item;
	payload.subjectId = 42;
	payload.subjectGuid = 0x1122334455667788ull;
	payload.subjectName = "Runenverzierte Kupferrute";
	payload.comment = "Sell price is wrong.\nSecond line.";
	const std::string json = R"({"version":"0.1.0.1"})";
	payload.clientData = game::CompressBugReportData(json);
	payload.uncompressedSize = static_cast<uint32>(json.size());

	game::BugReportPayload read;
	REQUIRE(Deserialize(Serialize(payload), read));
	CHECK(read.subjectType == payload.subjectType);
	CHECK(read.subjectId == payload.subjectId);
	CHECK(read.subjectGuid == payload.subjectGuid);
	CHECK(read.subjectName == payload.subjectName);
	CHECK(read.comment == payload.comment);
	CHECK(read.uncompressedSize == payload.uncompressedSize);
	CHECK(read.clientData == payload.clientData);
}

TEST_CASE("BugReportPayload rejects an oversized comment instead of truncating it", "[bug_report]")
{
	game::BugReportPayload payload;
	payload.comment.assign(game::bug_report_limits::MaxCommentBytes + 1, 'x');

	game::BugReportPayload read;
	CHECK_FALSE(Deserialize(Serialize(payload), read));
}

TEST_CASE("BugReportPayload rejects oversized client data", "[bug_report]")
{
	game::BugReportPayload payload;
	payload.clientData.assign(game::bug_report_limits::MaxCompressedBytes + 1, 'x');
	payload.uncompressedSize = 10;

	game::BugReportPayload read;
	CHECK_FALSE(Deserialize(Serialize(payload), read));
}

TEST_CASE("BugReportPayload rejects a claimed size above the inflate limit", "[bug_report]")
{
	game::BugReportPayload payload;
	payload.uncompressedSize = game::bug_report_limits::MaxUncompressedBytes + 1;

	game::BugReportPayload read;
	CHECK_FALSE(Deserialize(Serialize(payload), read));
}

TEST_CASE("BugReportPayload fails on a truncated buffer", "[bug_report]")
{
	game::BugReportPayload payload;
	payload.comment = "hello";
	auto buffer = Serialize(payload);
	buffer.resize(buffer.size() - 3);

	game::BugReportPayload read;
	CHECK_FALSE(Deserialize(buffer, read));
}

TEST_CASE("Bug report data compresses and inflates", "[bug_report]")
{
	const std::string json(5000, 'a');
	const auto compressed = game::CompressBugReportData(json);
	REQUIRE_FALSE(compressed.empty());
	CHECK(compressed.size() < json.size());

	std::string out;
	REQUIRE(game::InflateBugReportData(compressed, static_cast<uint32>(json.size()), 256 * 1024, out));
	CHECK(out == json);
}

TEST_CASE("Inflating rejects a decompression bomb", "[bug_report]")
{
	// 10 MB of zeros compresses to a few KB.
	const std::string bomb(10 * 1024 * 1024, '\0');
	const auto compressed = game::CompressBugReportData(bomb);
	REQUIRE(compressed.size() < game::bug_report_limits::MaxCompressedBytes);

	std::string out;
	// Lying about the size: claims to be small, inflates to far more.
	CHECK_FALSE(game::InflateBugReportData(compressed, 1024, game::bug_report_limits::MaxUncompressedBytes, out));
	// Honest claim above the limit.
	CHECK_FALSE(game::InflateBugReportData(compressed, static_cast<uint32>(bomb.size()), game::bug_report_limits::MaxUncompressedBytes, out));
	CHECK(out.empty());
}

TEST_CASE("Inflating rejects a claimed size larger than the real data", "[bug_report]")
{
	const std::string json = "{}";
	const auto compressed = game::CompressBugReportData(json);

	std::string out;
	CHECK_FALSE(game::InflateBugReportData(compressed, 100, 1024, out));
}

TEST_CASE("Inflating rejects garbage", "[bug_report]")
{
	std::vector<char> garbage(64);
	std::mt19937 rng(7);
	for (auto& c : garbage)
	{
		c = static_cast<char>(rng());
	}

	std::string out;
	CHECK_FALSE(game::InflateBugReportData(garbage, 100, 1024, out));
}

TEST_CASE("Subsystem table maps names, ids and owners", "[subsystem]")
{
	game::Subsystem found;
	REQUIRE(game::FindSubsystemByName("BUG_REPORT", found));
	CHECK(found == game::subsystem::BugReport);
	CHECK(std::string(game::GetSubsystemName(game::subsystem::BugReport)) == "BUG_REPORT");
	CHECK(game::GetSubsystemOwner(game::subsystem::BugReport) == game::subsystem_owner::World);

	CHECK_FALSE(game::FindSubsystemByName("NOPE", found));
	CHECK_FALSE(game::FindSubsystemByName(nullptr, found));
	CHECK(std::string(game::GetSubsystemName(200)).empty());

	for (uint8 i = 0; i < game::subsystem::Count_; ++i)
	{
		CHECK(game::SubsystemTable[i].id == i);
	}
}
