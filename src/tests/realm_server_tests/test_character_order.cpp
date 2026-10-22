// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "realm_server/character_order.h"

using namespace mmo;

TEST_CASE("A permutation of the owned characters is a valid order", "[character_order]")
{
	CHECK(IsValidCharacterOrder({ 3, 1, 2 }, { 1, 2, 3 }));
}

TEST_CASE("Orders that are not exactly the owned set are rejected", "[character_order]")
{
	CHECK_FALSE(IsValidCharacterOrder({ 1, 2 }, { 1, 2, 3 }));        // missing
	CHECK_FALSE(IsValidCharacterOrder({ 1, 2, 2 }, { 1, 2, 3 }));     // duplicate
	CHECK_FALSE(IsValidCharacterOrder({ 1, 2, 9 }, { 1, 2, 3 }));     // foreign
	CHECK_FALSE(IsValidCharacterOrder({}, { 1 }));                    // empty
	CHECK(IsValidCharacterOrder({}, {}));                             // no characters, nothing to order
}
