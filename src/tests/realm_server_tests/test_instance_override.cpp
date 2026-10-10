// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "realm_server/instance_override.h"

#include "catch.hpp"

using namespace mmo;

namespace
{
	InstanceId MakeId()
	{
		return InstanceId::from_string("0b9b1f3a-6a7e-4a52-9d3c-6f0e0d4b2a11").value();
	}
}

TEST_CASE("Override for the transfer map is used once", "[instance_override]")
{
	std::optional<InstanceOverride> pending = InstanceOverride{ 1, MakeId() };

	const auto used = ConsumeInstanceOverride(pending, 1);
	REQUIRE(used.has_value());
	CHECK(*used == MakeId());
	CHECK_FALSE(pending.has_value());

	CHECK_FALSE(ConsumeInstanceOverride(pending, 1).has_value());
}

TEST_CASE("Override for another map is ignored and cleared", "[instance_override]")
{
	std::optional<InstanceOverride> pending = InstanceOverride{ 1, MakeId() };

	CHECK_FALSE(ConsumeInstanceOverride(pending, 0).has_value());
	CHECK_FALSE(pending.has_value());
}

TEST_CASE("Nil override instance is never used", "[instance_override]")
{
	std::optional<InstanceOverride> pending = InstanceOverride{ 1, InstanceId{} };

	CHECK_FALSE(ConsumeInstanceOverride(pending, 1).has_value());
	CHECK_FALSE(pending.has_value());
}
