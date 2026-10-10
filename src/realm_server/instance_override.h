// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/game.h"

#include <optional>

namespace mmo
{
	/// A one-shot request to place the next world transfer into a specific instance, instead of
	/// the one the character's own group or personal bindings resolve to. Used by the GM port
	/// command to follow a player into their dungeon instance.
	struct InstanceOverride
	{
		/// The map the override was issued for.
		MapId mapId = 0;

		/// The instance to join.
		InstanceId instanceId{};
	};

	/// Takes the pending override for a transfer to transferMap. The pending override is always
	/// cleared, used or not, so a teleport that never completed cannot redirect a later one.
	/// @param pending The pending override slot of the transferring player.
	/// @param transferMap The map the player is being transferred to.
	/// @returns The instance to join, or nothing if no override applies.
	inline std::optional<InstanceId> ConsumeInstanceOverride(std::optional<InstanceOverride>& pending, const MapId transferMap)
	{
		const std::optional<InstanceOverride> taken = pending;
		pending.reset();

		if (!taken || taken->mapId != transferMap || taken->instanceId.is_nil())
		{
			return std::nullopt;
		}

		return taken->instanceId;
	}
}
