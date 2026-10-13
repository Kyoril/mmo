// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <algorithm>
#include <map>
#include <optional>

namespace mmo
{
	/// Decides whether another player should be told that a player created an item.
	/// Created items are announced to the other members of the creator's group.
	/// @param creatorGuid Guid of the player who created the item.
	/// @param creatorGroupId Group id of the creator (0 if not in a group).
	/// @param otherGuid Guid of the player to check.
	/// @param otherGroupId Group id of the player to check (0 if not in a group).
	/// @return True if the other player should receive the notification.
	inline bool ShouldNotifyOfCreatedItem(const uint64 creatorGuid, const uint64 creatorGroupId, const uint64 otherGuid, const uint64 otherGroupId)
	{
		return creatorGroupId != 0 && otherGuid != creatorGuid && otherGroupId == creatorGroupId;
	}

	/// One item push notification summarizing all slots an item stack was added to.
	struct AggregatedItemPush
	{
		/// Absolute inventory slot of the first slot the items went to.
		uint16 slot = 0;

		/// Total amount added across all slots, clamped to the uint16 wire field.
		uint16 amount = 0;
	};

	/// Collapses the per-slot result of Inventory::CreateItems into a single notification, so one
	/// created stack is announced once even when it topped off a stack and spilled into a new slot.
	/// @param addedBySlot Amount added per absolute inventory slot.
	/// @return The aggregated push, or nothing if no items were added.
	inline std::optional<AggregatedItemPush> AggregateAddedItems(const std::map<uint16, uint16>& addedBySlot)
	{
		uint32 total = 0;
		for (const auto& [slot, amount] : addedBySlot)
		{
			total += amount;
		}

		if (total == 0)
		{
			return std::nullopt;
		}

		AggregatedItemPush push;
		push.slot = addedBySlot.begin()->first;
		push.amount = static_cast<uint16>(std::min<uint32>(total, 0xFFFF));
		return push;
	}
}
