// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "item.h"

namespace mmo
{
	namespace bank_result
	{
		enum Type
		{
			/// The banker npc is hostile towards the player.
			BankerHostile,

			/// The banker npc is too far away.
			BankerTooFarAway,

			/// The banker npc is dead.
			BankerIsDead,

			/// The player is dead and can't use the bank.
			CantBankWhileDead
		};
	}

	namespace buy_bank_bag_slot_result
	{
		enum Type
		{
			/// The bank bag slot was successfully purchased.
			Ok,

			/// All bank bag slots have already been purchased.
			FailedTooMany,

			/// The player doesn't have enough money to purchase the slot.
			FailedInsufficientFunds,

			/// There is no banker npc to interact with.
			FailedNotBanker
		};
	}

	/// Gets the price in copper for purchasing a specific bank bag slot.
	/// @param slotIndex The 0-based index of the bank bag slot to purchase.
	/// @returns The price in copper, or 0 if the slot index is out of range.
	inline uint32 GetBankBagSlotPrice(const uint8 slotIndex)
	{
		static constexpr uint32 prices[player_bank_bag_slots::Count_] =
		{
			1000,		// 10 silver
			10000,		// 1 gold
			100000,		// 10 gold
			250000,		// 25 gold
			250000,		// 25 gold
			250000,		// 25 gold
			250000		// 25 gold
		};

		if (slotIndex >= player_bank_bag_slots::Count_)
		{
			return 0;
		}

		return prices[slotIndex];
	}
}
