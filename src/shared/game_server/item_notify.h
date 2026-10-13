// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

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
}
