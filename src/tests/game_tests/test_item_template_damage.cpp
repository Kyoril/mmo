// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game/item.h"

#include <type_traits>

using namespace mmo;

TEST_CASE("Item template damage is reported as whole numbers", "[item_template_damage]")
{
	// The tooltip renders the damage range with the integer format "%d - %d"
	// (WEAPON_DAMAGE_MIN_MAX), so the accessors the script API exposes have to hand out whole
	// numbers just like ItemHandle::GetMinDamage does for an item in the bags. Fractional
	// damage values - "Tollmaster's Cudgel" (item 172) deals 8.5 - 15.2 - would otherwise make
	// the tooltip script fail with "number has no integer representation".
	CHECK(std::is_integral_v<decltype(std::declval<const ItemInfo&>().GetMinDamage())>);
	CHECK(std::is_integral_v<decltype(std::declval<const ItemInfo&>().GetMaxDamage())>);

	ItemInfo info{};
	info.damage.min = 8.5f;
	info.damage.max = 15.2f;

	// Rounded down, matching the item instance accessors.
	CHECK(info.GetMinDamage() == 8);
	CHECK(info.GetMaxDamage() == 15);
}
