// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <unordered_map>

namespace mmo
{
	/// What a grinding bot remembers between ticks.
	///
	/// The blacklist is the important part. Spawn positions come out of the proto project, and a
	/// position being in the data does not mean the navigation mesh can reach it - the spawn may
	/// sit on a rooftop, inside geometry, or on a page with no mesh at all. Without a memory of
	/// which spots failed, a bot picks the nearest unreachable one forever.
	struct BotGrindState final
	{
		/// Index into the shared spot list, or npos when the bot has no spot.
		std::size_t spotIndex { npos };

		Vector3 spotPosition { Vector3::Zero };

		/// Set once we are close enough to the spot for the object manager to have the creatures.
		bool arrived { false };

		/// When the bot arrived. A spawn point in the data says nothing about what is alive right
		/// now, so a bot has to be able to notice that it walked somewhere for nothing.
		GameTime arrivedMs { 0 };

		/// Whether the bot is currently recovering. Held across ticks so that resting runs from
		/// the low threshold all the way back up to the rested one, instead of stopping the moment
		/// the bot is one hit point better off than it was.
		bool resting { false };

		/// Spot index -> time the blacklist entry expires.
		std::unordered_map<std::size_t, GameTime> blacklist;

		/// Kills credited to this bot since it entered the world. Telemetry, and the signal that
		/// a grind spot is actually productive.
		uint32 kills { 0 };

		static constexpr std::size_t npos = static_cast<std::size_t>(-1);

		[[nodiscard]] bool HasSpot() const { return spotIndex != npos; }

		void ClearSpot()
		{
			spotIndex = npos;
			spotPosition = Vector3::Zero;
			arrived = false;
			arrivedMs = 0;
		}

		/// Marks a spot as not worth trying again until nowMs + durationMs.
		void Blacklist(const std::size_t index, const GameTime nowMs, const GameTime durationMs)
		{
			blacklist[index] = nowMs + durationMs;
		}

		[[nodiscard]] bool IsBlacklisted(const std::size_t index, const GameTime nowMs) const
		{
			const auto it = blacklist.find(index);
			return it != blacklist.end() && it->second > nowMs;
		}
	};
}
