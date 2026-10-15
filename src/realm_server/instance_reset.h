// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/game.h"

#include <functional>
#include <map>
#include <vector>

namespace mmo
{
	/// Why a reset request was refused before any single instance was looked at.
	namespace instance_reset_rejection
	{
		enum Type
		{
			/// The request is carried out.
			None,
			/// The requesting character is in a group but does not lead it.
			NotGroupLeader,
			/// The requesting character is itself inside a dungeon instance.
			InsideInstance,
			/// The requesting character is not bound to any dungeon instance.
			NothingBound,
		};
	}

	/// One dungeon instance a reset request covers.
	struct InstanceResetEntry
	{
		/// The dungeon map the instance belongs to.
		MapId mapId = 0;

		/// The instance bound on that map.
		InstanceId instanceId;

		/// Guids of the characters currently inside the instance. Empty for resettable entries.
		std::vector<uint64> occupants;
	};

	/// What a reset request amounts to: which instances are released and which are held back.
	struct InstanceResetPlan
	{
		/// Set when the request is refused as a whole; both lists are empty then.
		instance_reset_rejection::Type rejection = instance_reset_rejection::None;

		/// Instances nobody is inside of, which can therefore be released.
		std::vector<InstanceResetEntry> reset;

		/// Instances which still hold characters, together with those characters.
		std::vector<InstanceResetEntry> blocked;
	};

	/// Decides what resetting the bound dungeon instances amounts to.
	///
	/// An instance may only be released while nobody is inside it, and only the group leader
	/// decides for a group. The decision is kept apart from the session so it can be reasoned
	/// about (and tested) without a connected client.
	///
	/// @param bindings The dungeon instances the requester is bound to (map id -> instance id).
	/// @param requesterInsideInstance Whether the requester itself currently is inside a dungeon instance.
	/// @param isGroupLeader Whether the requester leads its group. True when it is in no group.
	/// @param occupantsOf Returns the guids of the characters currently inside an instance.
	/// @returns The resulting plan.
	inline InstanceResetPlan PlanInstanceReset(const std::map<MapId, InstanceId>& bindings,
		const bool requesterInsideInstance,
		const bool isGroupLeader,
		const std::function<std::vector<uint64>(InstanceId)>& occupantsOf)
	{
		InstanceResetPlan plan;

		if (!isGroupLeader)
		{
			plan.rejection = instance_reset_rejection::NotGroupLeader;
			return plan;
		}

		// Standing inside the dungeon is the most common way of still having someone inside it,
		// and the requester is the one player whose own position is known for certain here.
		if (requesterInsideInstance)
		{
			plan.rejection = instance_reset_rejection::InsideInstance;
			return plan;
		}

		if (bindings.empty())
		{
			plan.rejection = instance_reset_rejection::NothingBound;
			return plan;
		}

		for (const auto& [mapId, instanceId] : bindings)
		{
			InstanceResetEntry entry;
			entry.mapId = mapId;
			entry.instanceId = instanceId;
			entry.occupants = occupantsOf ? occupantsOf(instanceId) : std::vector<uint64>();

			if (entry.occupants.empty())
			{
				plan.reset.push_back(std::move(entry));
			}
			else
			{
				plan.blocked.push_back(std::move(entry));
			}
		}

		return plan;
	}
}
