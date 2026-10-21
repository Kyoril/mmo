// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "realm_server/character_name.h"

using namespace mmo;

TEST_CASE("Character names accept ASCII letters", "[character_name]")
{
	CHECK(IsValidCharacterName("Bob"));
	CHECK(IsValidCharacterName("Abcdefghijkl"));
}

TEST_CASE("Character names accept German umlauts and sharp s", "[character_name]")
{
	CHECK(IsValidCharacterName("R\xC3\xBClpsi"));			// Rülpsi
	CHECK(IsValidCharacterName("\xC3\x9C" "bel"));			// Übel
	CHECK(IsValidCharacterName("\xC3\x84\xC3\x96\xC3\x9C"));	// ÄÖÜ
	CHECK(IsValidCharacterName("Gr\xC3\xB6\xC3\x9F" "e"));	// Größe
	CHECK(IsValidCharacterName("B\xC3\xA4r"));				// Bär
}

TEST_CASE("Character name length counts characters, not bytes", "[character_name]")
{
	// 12 umlauts = 24 bytes, but only 12 characters.
	std::string twelve;
	for (int i = 0; i < 12; ++i)
	{
		twelve += "\xC3\xBC";
	}
	CHECK(IsValidCharacterName(twelve));
	CHECK_FALSE(IsValidCharacterName(twelve + "a"));

	// Two umlauts are four bytes but only two characters: too short.
	CHECK_FALSE(IsValidCharacterName("\xC3\xA4\xC3\xB6"));
	CHECK_FALSE(IsValidCharacterName("Ab"));
	CHECK_FALSE(IsValidCharacterName(""));
	CHECK_FALSE(IsValidCharacterName("Abcdefghijklm"));
}

TEST_CASE("Character names reject non-letters and malformed UTF-8", "[character_name]")
{
	CHECK_FALSE(IsValidCharacterName("Bob Bob"));
	CHECK_FALSE(IsValidCharacterName("Bob1"));
	CHECK_FALSE(IsValidCharacterName("Bob-Bob"));

	// Orphaned continuation byte, as left behind by the old text field backspace bug.
	CHECK_FALSE(IsValidCharacterName("\xBCRulpsi"));
	// Truncated two-byte sequence.
	CHECK_FALSE(IsValidCharacterName("Rulpsi\xC3"));
	// Latin-1 encoded 'ü' instead of UTF-8.
	CHECK_FALSE(IsValidCharacterName("R\xFClpsi"));
	// Other non-ASCII letters (é, Cyrillic а) stay rejected.
	CHECK_FALSE(IsValidCharacterName("Ren\xC3\xA9" "e"));
	CHECK_FALSE(IsValidCharacterName("B\xD0\xB0r"));
}
