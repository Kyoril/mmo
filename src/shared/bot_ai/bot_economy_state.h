// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <unordered_map>
#include <unordered_set>
#include <iterator>

namespace mmo
{
	/// How long a corpse is left alone after the bot looted it, or was refused.
	constexpr GameTime BotLootedCorpseMemoryMs = 10 * 60 * 1000;

	/// What a bot remembers about looting, gear and selling.
	struct BotEconomyState final
	{
		/// Corpses the bot looted or may not loot, and when; it does not walk back to them.
		std::unordered_map<uint64, GameTime> lootedCorpses;

		/// The corpse whose loot window the bot asked for, and when.
		uint64 pendingLootGuid { 0 };
		GameTime pendingLootMs { 0 };

		/// Item entries the server refused to equip: wrong class, armor the bot cannot wear.
		std::unordered_set<uint32> unusableItems;

		/// The item last sent to be equipped, to blame if the server answers with an error.
		uint32 lastEquipEntry { 0 };
		GameTime lastEquipMs { 0 };

		/// A trip to sell: where the vendor stands and which unit it is.
		bool selling { false };
		uint32 vendorMapId { 0 };
		uint32 vendorEntry { 0 };
		Vector3 vendorPosition { Vector3::Zero };
		GameTime sellingSinceMs { 0 };

		/// The bot could not sell the last time it tried; it does not start another trip until then.
		GameTime nextSellAttemptMs { 0 };

		/// A trip to the class trainer to buy spells.
		bool training { false };
		uint32 trainerEntry { 0 };
		uint32 trainerId { 0 };
		uint32 trainerMapId { 0 };
		Vector3 trainerPosition { Vector3::Zero };
		GameTime trainingSinceMs { 0 };

		/// The level the bot last trained at, and when it may look again regardless.
		uint32 trainedAtLevel { 0 };
		GameTime nextTrainAttemptMs { 0 };

		/// When the bot last asked to learn a talent.
		GameTime lastTalentMs { 0 };

		/// When the bot last cast each self-buff, so that one the server refuses is not recast
		/// on every global cooldown.
		std::unordered_map<uint32, GameTime> lastBuffAttemptMs;

		[[nodiscard]] bool WasLooted(const uint64 guid, const GameTime nowMs) const
		{
			const auto it = lootedCorpses.find(guid);
			return it != lootedCorpses.end() && nowMs - it->second < BotLootedCorpseMemoryMs;
		}

		void ForgetOldCorpses(const GameTime nowMs)
		{
			for (auto it = lootedCorpses.begin(); it != lootedCorpses.end();)
			{
				it = nowMs - it->second >= BotLootedCorpseMemoryMs ? lootedCorpses.erase(it) : std::next(it);
			}
		}
	};
}
