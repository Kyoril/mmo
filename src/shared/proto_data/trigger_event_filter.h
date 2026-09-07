// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "shared/proto_data/triggers.pb.h"

#include <vector>

namespace mmo
{
	namespace proto
	{
		/// @brief Whether a trigger event's configured filter data matches the data the raiser
		///	       supplied.
		///
		/// The generic positional filter: each configured value must equal the raised value at the
		/// same index, and a configured zero is a wildcard for that slot. An event with no data at
		/// all therefore matches every raise, which is what makes "fire on any level up" the
		/// default and "fire only at level 10" a one-field edit.
		///
		/// @note GameCreatureS::RaiseTrigger deliberately does NOT use this. Creature events carry
		///	      event-specific semantics that this cannot express - OnHealthDroppedBelow tests a
		///	      threshold *crossing* against two supplied values, OnGossipAction requires an exact
		///	      match on both fields including zeros, and it fires once per matching event rather
		///	      than once per trigger. Folding it in here would silently change all three.
		///
		/// @param triggerEvent The event as configured on the trigger.
		/// @param data The values supplied by whatever raised the event.
		/// @return true when the trigger should fire.
		inline bool TriggerEventDataMatches(const TriggerEvent& triggerEvent, const std::vector<uint32>& data)
		{
			for (int i = 0; i < triggerEvent.data_size(); ++i)
			{
				const uint32 filter = triggerEvent.data(i);
				if (filter == 0)
				{
					// Wildcard for this slot.
					continue;
				}

				if (i >= static_cast<int>(data.size()) || data[i] != filter)
				{
					return false;
				}
			}

			return true;
		}
	}
}
