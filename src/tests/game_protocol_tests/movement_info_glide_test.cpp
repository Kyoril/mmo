// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// The Gliding movement flag: it is only valid together with Falling, so the jump velocity is
// always serialized while gliding and survives a round trip.

#include "catch.hpp"

#include "game/movement_info.h"
#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"

#include <vector>

using namespace mmo;

TEST_CASE("Gliding movement info round-trips with jump velocity", "[movement]")
{
	MovementInfo info;
	info.movementFlags = movement_flags::Falling | movement_flags::Gliding | movement_flags::Forward;
	info.jumpVelocity = Vector3(3.0f, -2.5f, 1.0f);

	std::vector<char> buffer;
	{
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		writer << info;
	}

	MovementInfo read;
	io::MemorySource source{ buffer.data(), buffer.data() + buffer.size() };
	io::Reader reader{ source };
	REQUIRE(static_cast<bool>(reader >> read));

	CHECK(read.movementFlags == info.movementFlags);
	CHECK(read.IsGliding());
	CHECK(read.IsChangingPosition());
	CHECK(read.jumpVelocity.x == Approx(3.0f));
	CHECK(read.jumpVelocity.y == Approx(-2.5f));
	CHECK(read.jumpVelocity.z == Approx(1.0f));
}
