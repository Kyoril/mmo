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

		/// Units the navigation mesh could not get us to, and when to forget that.
		///
		/// A creature stays attackable whether or not there is a path to it, so without this a bot
		/// that picks an unreachable target keeps picking the same one - it is, after all, still
		/// the nearest thing it could attack.
		std::unordered_map<uint64, GameTime> unreachableUnits;

		/// The target the current run of failed approaches is against, and how many there have
		/// been. Reset whenever the target changes, so one bad path does not condemn a target the
		/// bot simply had not finished walking to.
		uint64 approachFailureTarget { 0 };
		uint32 approachFailures { 0 };

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

		void MarkUnreachable(const uint64 guid, const GameTime nowMs, const GameTime durationMs)
		{
			unreachableUnits[guid] = nowMs + durationMs;
		}

		[[nodiscard]] bool IsUnreachable(const uint64 guid, const GameTime nowMs) const
		{
			const auto it = unreachableUnits.find(guid);
			return it != unreachableUnits.end() && it->second > nowMs;
		}

		/// Records an approach that failed to find a path.
		/// @return Consecutive failures against this target, including this one.
		uint32 NoteApproachFailure(const uint64 guid)
		{
			if (approachFailureTarget != guid)
			{
				approachFailureTarget = guid;
				approachFailures = 0;
			}

			return ++approachFailures;
		}

		void ClearApproachFailures()
		{
			approachFailureTarget = 0;
			approachFailures = 0;
		}
	};
}
