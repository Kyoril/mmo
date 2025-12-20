// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/non_copyable.h"
#include "math/vector3.h"

#include <vector>
#include <unordered_set>

#include "client_data/project.h"

namespace mmo
{
	namespace proto_client
	{
		class AreaTriggerEntry;
	}

	/// Manages area triggers for the current map and tracks player overlap state.
	class AreaTriggerManager final : public NonCopyable
	{
	public:
		/// Initializes a new instance of the AreaTriggerManager class.
		explicit AreaTriggerManager() = default;

		/// Default destructor.
		~AreaTriggerManager() = default;

	public:
		/// Loads all area triggers for the specified map.
		/// @param mapId The map ID to load triggers for.
		/// @param allTriggers The complete list of area triggers from the project.
		void LoadTriggersForMap(uint32 mapId, const proto_client::AreaTriggerManager& areaTriggerManager);

		/// Clears all loaded area triggers.
		void ClearTriggers();

		/// Checks for area trigger overlaps at the specified position.
		/// @param position The player position to check.
		/// @param outNewlyEnteredTriggers Output list of newly entered trigger IDs.
		void CheckForTriggerOverlap(const Vector3& position, std::vector<uint32>& outNewlyEnteredTriggers);

		/// Tests if a point is inside a specific area trigger.
		/// @param trigger The area trigger to test against.
		/// @param position The point to test.
		/// @return True if the point is inside the trigger, false otherwise.
		static bool IsPointInTrigger(const proto_client::AreaTriggerEntry& trigger, const Vector3& position);

	private:
		/// Tests if a point is inside a sphere trigger.
		static bool IsPointInSphere(const Vector3& center, float radius, const Vector3& point);

		/// Tests if a point is inside an oriented box trigger.
		static bool IsPointInBox(const Vector3& center, const Vector3& halfExtents, float orientation, const Vector3& point);

	private:
		/// All area triggers for the current map.
		std::vector<const proto_client::AreaTriggerEntry*> m_triggers;

		/// Set of trigger IDs the player is currently inside.
		std::unordered_set<uint32> m_activeTriggers;
	};
}
