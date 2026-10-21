// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "frame_ui/utf8_utils.h"

using namespace mmo;

TEST_CASE("utf8::byte_index points past whole multi-byte characters", "[utf8]")
{
	// "Rü" + "lpsi": 'ü' is the two bytes 0xC3 0xBC.
	const std::string text = "R\xC3\xBClpsi";

	CHECK(utf8::byte_index(text, 0) == 0);
	CHECK(utf8::byte_index(text, 1) == 1);
	CHECK(utf8::byte_index(text, 2) == 3);
	CHECK(utf8::byte_index(text, 3) == 4);
	CHECK(utf8::byte_index(text, 6) == text.size());
	CHECK(utf8::byte_index(text, 100) == text.size());
}

TEST_CASE("utf8::byte_index handles three- and four-byte characters", "[utf8]")
{
	// U+20AC (3 bytes) followed by U+1F600 (4 bytes) followed by 'a'.
	const std::string text = "\xE2\x82\xAC\xF0\x9F\x98\x80" "a";

	CHECK(utf8::byte_index(text, 1) == 3);
	CHECK(utf8::byte_index(text, 2) == 7);
	CHECK(utf8::byte_index(text, 3) == 8);
}

TEST_CASE("utf8::byte_index and char_index round-trip", "[utf8]")
{
	const std::string text = "\xC3\x84pfel\xC3\x9F\xC3\xBC";
	const std::size_t length = utf8::length(text);
	REQUIRE(length == 7);

	for (std::size_t i = 0; i <= length; ++i)
	{
		CHECK(utf8::char_index(text, utf8::byte_index(text, i)) == i);
	}
}
