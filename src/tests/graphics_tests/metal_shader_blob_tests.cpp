// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <vector>

#include "msl_cross/metal_shader_blob.h"

using namespace mmo;

TEST_CASE("library blob round-trips", "[metal_shader_blob]")
{
	const std::vector<uint8> metallib = { 'M', 'T', 'L', 'B', 0, 1, 2, 3, 250 };
	const std::vector<uint8> blob = metal_shader_blob::PackLibrary(metal_shader_blob::Stage::Pixel, metallib, 8, 4, 2);

	metal_shader_blob::Header header {};
	metal_shader_blob::Payload payload = metal_shader_blob::Payload::Source;
	std::span<const uint8> bytes;
	REQUIRE(metal_shader_blob::Unpack(blob.data(), blob.size(), header, payload, bytes));
	CHECK(payload == metal_shader_blob::Payload::Library);
	CHECK(header.stage == metal_shader_blob::Stage::Pixel);
	CHECK(header.threadgroupSize[0] == 8);
	CHECK(header.threadgroupSize[1] == 4);
	CHECK(header.threadgroupSize[2] == 2);
	CHECK(std::vector<uint8>(bytes.begin(), bytes.end()) == metallib);

	// A library is not source
	std::string_view source;
	CHECK_FALSE(metal_shader_blob::Unpack(blob.data(), blob.size(), header, source));

	// Truncated payload
	CHECK_FALSE(metal_shader_blob::Unpack(blob.data(), blob.size() - 1, header, payload, bytes));
}

TEST_CASE("source blob still unpacks as source", "[metal_shader_blob]")
{
	const std::vector<uint8> blob = metal_shader_blob::Pack(metal_shader_blob::Stage::Vertex, "vertex main0() {}");

	metal_shader_blob::Header header {};
	std::string_view source;
	REQUIRE(metal_shader_blob::Unpack(blob.data(), blob.size(), header, source));
	CHECK(source == "vertex main0() {}");
	CHECK(header.stage == metal_shader_blob::Stage::Vertex);

	metal_shader_blob::Payload payload = metal_shader_blob::Payload::Library;
	std::span<const uint8> bytes;
	REQUIRE(metal_shader_blob::Unpack(blob.data(), blob.size(), header, payload, bytes));
	CHECK(payload == metal_shader_blob::Payload::Source);
	CHECK(bytes.size() == source.size());
}
