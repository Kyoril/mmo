// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/world_text_layout.h"

using namespace mmo;

// The text floating above a creature shows its name and, below it, the localized subtitle.
// In the french locale that subtitle carries accented characters, which UTF-8 encodes in two
// bytes each. Laying the text out byte by byte handed those bytes to the font as separate
// (and, as signed chars, wildly out-of-range) codepoints, so both were skipped as missing
// glyphs and the character disappeared from the name plate.
TEST_CASE("World text lays out an accented character as a single codepoint", "[world_text_layout]")
{
	// "<Maitre batisseur>" with the circumflexes of the french creature subtitle.
	const std::string subtitle = "<Ma\xC3\xAEtre b\xC3\xA2tisseur>";

	const std::vector<uint32> codepoints = BuildWorldTextCodepoints(subtitle);

	REQUIRE(codepoints.size() == 18);
	CHECK(codepoints[3] == 0xEE);
	CHECK(codepoints[9] == 0xE2);

	// A raw UTF-8 byte would sign-extend into a codepoint far beyond any font's range and
	// would be dropped as a missing glyph.
	for (const uint32 codepoint : codepoints)
	{
		CHECK(codepoint <= 0x10FFFF);
	}
}

TEST_CASE("World text keeps ASCII and line breaks unchanged", "[world_text_layout]")
{
	const std::string nameplate = "Thalric Stonehammer\n<Master Builder>";

	const std::vector<uint32> codepoints = BuildWorldTextCodepoints(nameplate);

	REQUIRE(codepoints.size() == nameplate.length());
	CHECK(codepoints[19] == static_cast<uint32>('\n'));
	CHECK(codepoints.front() == static_cast<uint32>('T'));
	CHECK(codepoints.back() == static_cast<uint32>('>'));
}

TEST_CASE("World text lays out two byte cyrillic characters as single codepoints", "[world_text_layout]")
{
	// "<Мастер>" - the russian subtitle of the same creature starts with these characters.
	const std::string subtitle = "<\xD0\x9C\xD0\xB0\xD1\x81\xD1\x82\xD0\xB5\xD1\x80>";

	const std::vector<uint32> codepoints = BuildWorldTextCodepoints(subtitle);

	REQUIRE(codepoints.size() == 8);
	CHECK(codepoints[1] == 0x41C);
	CHECK(codepoints[6] == 0x440);
}
