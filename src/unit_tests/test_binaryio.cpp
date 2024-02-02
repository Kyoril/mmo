// Copyright (C) 2019 - 2024, Robin Klimonow. All rights reserved.

#include "catch.hpp"
#include "container_source.h"
#include "memory_source.h"
#include "vector_sink.h"
#include "base/typedefs.h"

#include "math/aabb.h"

using namespace mmo;


TEST_CASE("Can serialize and deserialize full packed guid", "[binaryio]")
{
	const uint64 originalGuid = 0x123456789abcdef1;

	std::vector<char> buffer;
	io::VectorSink sink{ buffer };
	io::Writer writer{sink};
	writer << io::write_packed_guid(originalGuid);

	// Read back from buffer
	io::MemorySource source{ buffer };
	io::Reader reader{ source };
	uint64 deserializedGuid = 0;
	reader >> io::read_packed_guid(deserializedGuid);

	CHECK(reader);
	CHECK(originalGuid == deserializedGuid);
}

TEST_CASE("Can serialize and deserialize partial packed guid", "[binaryio]")
{
	const uint64 originalGuid = 0xF130000000000001;

	std::vector<char> buffer;
	io::VectorSink sink{ buffer };
	io::Writer writer{ sink };
	writer << io::write_packed_guid(originalGuid);

	// Read back from buffer
	io::MemorySource source{ buffer };
	io::Reader reader{ source };
	uint64 deserializedGuid = 0;
	reader >> io::read_packed_guid(deserializedGuid);

	CHECK(reader);
	CHECK(originalGuid == deserializedGuid);
}
