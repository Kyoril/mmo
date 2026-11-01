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
