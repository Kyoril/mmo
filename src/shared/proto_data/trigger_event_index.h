// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "shared/proto_data/triggers.pb.h"
#include "shared/proto_data/trigger_helper.h"

#include <algorithm>
#include <array>
#include <vector>

namespace mmo
{
	namespace proto
	{
		/// @brief Buckets a set of triggers by the event type each one listens for, so raising an
		///	       event is an array index instead of a scan.
		///
		/// Owners that hold a *small, explicit* trigger list (a creature entry, an object entry)
		/// do not need this -- their list is already the answer. It exists for the two owners whose
		/// candidate set is otherwise the whole trigger table or requires a lookup per raise:
		/// player characters, which have no trigger list at all and are found by flag, and world
		/// instances, whose map names its triggers by id.
		///
		/// Entries are stored by pointer into the trigger manager's protobuf storage, which keeps
		/// element addresses stable for as long as the table is not rebuilt. Rebuild the index
		/// whenever that storage changes.
		class TriggerEventIndex final
		{
		public:
			/// @brief Drops every bucketed trigger.
			void Clear()
			{
				for (auto& bucket : m_byEvent)
				{
					bucket.clear();
				}
			}

			/// @brief Adds a trigger to the bucket of every event type it listens for.
			///
			/// A trigger listing the same event type more than once -- how a designer expresses
			/// "at level 10 or level 20" -- is bucketed once, because a raise fires it at most
			/// once.
			/// @param entry The trigger to index. Must outlive the index.
			void Add(const TriggerEntry& entry)
			{
				for (const auto& triggerEvent : entry.newevents())
				{
					const auto eventType = static_cast<std::size_t>(triggerEvent.type());
					if (eventType >= m_byEvent.size())
					{
						// An event value outside the enum: bad data, and indexing on it would run
						// off the end of the bucket array.
						continue;
					}

					auto& bucket = m_byEvent[eventType];
					if (std::find(bucket.begin(), bucket.end(), &entry) == bucket.end())
					{
						bucket.push_back(&entry);
					}
				}
			}

			/// @brief Gets the triggers listening for an event.
			/// @param eventType The event being raised.
			/// @return The matching triggers; empty for every event nothing listens for, which is
			///	        the common case and costs one array index to discover.
			[[nodiscard]] const std::vector<const TriggerEntry*>& Get(const trigger_event::Type eventType) const
			{
				static const std::vector<const TriggerEntry*> s_empty;

				const auto index = static_cast<std::size_t>(eventType);
				if (index >= m_byEvent.size())
				{
					return s_empty;
				}

				return m_byEvent[index];
			}

			/// @brief Whether any trigger at all is indexed. Used to skip work entirely.
			[[nodiscard]] bool IsEmpty() const
			{
				return std::all_of(m_byEvent.begin(), m_byEvent.end(),
					[](const std::vector<const TriggerEntry*>& bucket) { return bucket.empty(); });
			}

		private:
			std::array<std::vector<const TriggerEntry*>, trigger_event::Count_> m_byEvent;
		};
	}
}
