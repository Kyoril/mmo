// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game/pet_defines.h"
#include "game/spell.h"

#include "catch.hpp"

using namespace mmo;

TEST_CASE("Pet names accept 1 to 12 letters", "[pets]")
{
	CHECK(IsValidPetName("Shade"));
	CHECK(IsValidPetName("S"));
	CHECK(IsValidPetName("Twelvecharss"));
	CHECK_FALSE(IsValidPetName(""));
	CHECK_FALSE(IsValidPetName("ThirteenChars"));
	CHECK_FALSE(IsValidPetName("Sh4de"));
	CHECK_FALSE(IsValidPetName("Sha de"));
}

TEST_CASE("Pet names are normalized to a capitalized first letter", "[pets]")
{
	CHECK(NormalizePetName("sHADE") == "Shade");
	CHECK(NormalizePetName("shade") == "Shade");
	CHECK(NormalizePetName("") == "");
}

TEST_CASE("Pet spell effect ids are appended", "[pets]")
{
	CHECK(static_cast<int>(spell_effects::Count_) == 62);
	CHECK(MaxStoredPets == 5);
}

TEST_CASE("Stored pet names are clamped to the realm's column size at a character boundary", "[pets]")
{
	CHECK(MaxStoredPetNameBytes == 32);
	CHECK(ClampStoredPetName("Forest Boar") == "Forest Boar");
	CHECK(ClampStoredPetName("") == "");

	const String exact(MaxStoredPetNameBytes, 'a');
	CHECK(ClampStoredPetName(exact) == exact);
	CHECK(ClampStoredPetName(exact + "b") == exact);

	// 31 ASCII bytes followed by a two byte character: the character does not fit and is dropped
	// whole instead of leaving its lead byte behind.
	const String prefix(31, 'a');
	CHECK(ClampStoredPetName(prefix + "\xC3\xA9") == prefix);

	// 30 ASCII bytes and a two byte character fit exactly.
	const String fits = String(30, 'a') + "\xC3\xA9";
	CHECK(ClampStoredPetName(fits + "x") == fits);

	// A three byte character straddling the limit.
	const String straddle = String(31, 'a') + "\xE2\x82\xAC";
	CHECK(ClampStoredPetName(straddle) == prefix);
}
