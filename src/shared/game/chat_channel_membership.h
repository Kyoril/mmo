// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <unordered_map>
#include <utility>
#include <vector>

namespace mmo
{
	/// @brief Computes the effective set of chat channel ids a character should be a member of.
	///
	/// Membership is stored as a delta from each channel's default behaviour:
	/// - For a join-by-default channel, the character is a member unless they have an explicit
	///   "left" record (status == 0).
	/// - For any other channel, the character is a member only if they have an explicit "joined"
	///   record (status == 1).
	///
	/// This makes the result robust against channels that are added, renamed or removed after the
	/// fact: a newly added default channel is joined automatically (no record means not-left), and
	/// a channel id that no longer exists simply isn't present in @p channelDefs and is ignored.
	///
	/// The function is pure and free of engine dependencies so it can be unit-tested in isolation.
	///
	/// @param channelDefs Pairs of {channelId, joinByDefault} for every known channel.
	/// @param states Pairs of {channelId, status} for the character's persisted membership rows,
	///        where status is 1 (joined) or 0 (left).
	/// @return The channel ids the character is effectively a member of, in @p channelDefs order.
	inline std::vector<uint32> ComputeEffectiveChannelMemberships(
		const std::vector<std::pair<uint32, bool>>& channelDefs,
		const std::vector<std::pair<uint32, uint8>>& states)
	{
		std::unordered_map<uint32, uint8> stateById;
		stateById.reserve(states.size());
		for (const auto& state : states)
		{
			stateById[state.first] = state.second;
		}

		std::vector<uint32> result;
		for (const auto& [channelId, joinByDefault] : channelDefs)
		{
			const auto it = stateById.find(channelId);

			bool isMember;
			if (joinByDefault)
			{
				isMember = (it == stateById.end()) || it->second != 0;
			}
			else
			{
				isMember = (it != stateById.end()) && it->second == 1;
			}

			if (isMember)
			{
				result.push_back(channelId);
			}
		}

		return result;
	}
}
