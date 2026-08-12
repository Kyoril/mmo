// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game/chat_channel_membership.h"

#include <algorithm>

using namespace mmo;

namespace
{
	bool Contains(const std::vector<uint32>& ids, uint32 id)
	{
		return std::find(ids.begin(), ids.end(), id) != ids.end();
	}
}

TEST_CASE("New character joins all default channels", "[chat_channel_membership]")
{
	// Channel 1 and 3 are join-by-default, channel 2 is not.
	const std::vector<std::pair<uint32, bool>> defs{ {1, true}, {2, false}, {3, true} };

	// A brand new character has no persisted records.
	const std::vector<std::pair<uint32, uint8>> states{};

	const auto result = ComputeEffectiveChannelMemberships(defs, states);

	REQUIRE(result.size() == 2);
	REQUIRE(Contains(result, 1));
	REQUIRE(Contains(result, 3));
	REQUIRE_FALSE(Contains(result, 2));
}

TEST_CASE("Leaving a default channel persists as an opt-out", "[chat_channel_membership]")
{
	const std::vector<std::pair<uint32, bool>> defs{ {1, true}, {2, true} };

	// Character explicitly left channel 1 (status 0).
	const std::vector<std::pair<uint32, uint8>> states{ {1, 0} };

	const auto result = ComputeEffectiveChannelMemberships(defs, states);

	REQUIRE(result.size() == 1);
	REQUIRE(Contains(result, 2));
	REQUIRE_FALSE(Contains(result, 1));
}

TEST_CASE("Joining a non-default channel requires an explicit join record", "[chat_channel_membership]")
{
	const std::vector<std::pair<uint32, bool>> defs{ {1, false}, {2, false} };

	// Character explicitly joined channel 2.
	const std::vector<std::pair<uint32, uint8>> states{ {2, 1} };

	const auto result = ComputeEffectiveChannelMemberships(defs, states);

	REQUIRE(result.size() == 1);
	REQUIRE(Contains(result, 2));
	REQUIRE_FALSE(Contains(result, 1));
}

TEST_CASE("A default channel added later is auto-joined by existing characters", "[chat_channel_membership]")
{
	// Channel 5 is a brand new join-by-default channel the character has never seen.
	const std::vector<std::pair<uint32, bool>> defs{ {1, true}, {5, true} };

	// The character has records only for the older channel 1 (still a member).
	const std::vector<std::pair<uint32, uint8>> states{ {1, 1} };

	const auto result = ComputeEffectiveChannelMemberships(defs, states);

	// Channel 5 is joined automatically because there is no opt-out record for it.
	REQUIRE(Contains(result, 1));
	REQUIRE(Contains(result, 5));
}

TEST_CASE("Records for deleted channels are ignored", "[chat_channel_membership]")
{
	// Only channel 1 still exists in the game data.
	const std::vector<std::pair<uint32, bool>> defs{ {1, true} };

	// The character has a stale join record for channel 99 which no longer exists.
	const std::vector<std::pair<uint32, uint8>> states{ {1, 1}, {99, 1} };

	const auto result = ComputeEffectiveChannelMemberships(defs, states);

	REQUIRE(result.size() == 1);
	REQUIRE(Contains(result, 1));
	REQUIRE_FALSE(Contains(result, 99));
}

TEST_CASE("Effective channels preserve definition order", "[chat_channel_membership]")
{
	const std::vector<std::pair<uint32, bool>> defs{ {10, true}, {3, true}, {7, true} };
	const std::vector<std::pair<uint32, uint8>> states{};

	const auto result = ComputeEffectiveChannelMemberships(defs, states);

	REQUIRE(result == std::vector<uint32>{ 10, 3, 7 });
}
