// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game/gm_flags.h"
#include "game/gm_level.h"
#include "game_server/character_data.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "binary_io/reader.h"
#include "binary_io/memory_source.h"

#include "catch.hpp"

using namespace mmo;

TEST_CASE("SanitizeGmFlags clears flags without a gm level", "[gm_flags]")
{
	const uint8 both = gm_flags::Invisible | gm_flags::Godmode;
	CHECK(SanitizeGmFlags(both, gm_level::Player) == gm_flags::None);
	CHECK(SanitizeGmFlags(both, gm_level::Gm) == both);
	CHECK(SanitizeGmFlags(both, gm_level::Operator) == both);
}

TEST_CASE("SanitizeGmFlags drops unknown bits", "[gm_flags]")
{
	CHECK(SanitizeGmFlags(0xFF, gm_level::Gm) == (gm_flags::Invisible | gm_flags::Godmode));
}

TEST_CASE("CharacterData round-trips gmFlags", "[gm_flags]")
{
	CharacterData out;
	out.name = "Observer";
	out.isGameMaster = true;
	out.gmFlags = gm_flags::Invisible | gm_flags::Godmode;

	std::vector<char> buffer;
	io::VectorSink sink{ buffer };
	io::Writer writer{ sink };
	writer << out;

	io::MemorySource source{ buffer.data(), buffer.data() + buffer.size() };
	io::Reader reader{ source };
	CharacterData in;
	REQUIRE(reader >> in);
	CHECK(in.gmFlags == out.gmFlags);
	CHECK(in.isGameMaster);
	CHECK(in.name == "Observer");
}
