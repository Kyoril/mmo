// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "area_trigger_manager.h"

#include "shared/client_data/proto_client/area_triggers.pb.h"
#include "log/default_log_levels.h"

#include <cmath>

namespace mmo
{
	void AreaTriggerManager::LoadTriggersForMap(const uint32 mapId, const proto_client::AreaTriggerManager& areaTriggerManager)
	{
		ClearTriggers();

		// Load all area triggers for this map
		for (const auto& trigger : areaTriggerManager.getTemplates().entry())
		{
			if (trigger.map() == mapId)
			{
				m_triggers.push_back(&trigger);
			}
		}

		ILOG("Loaded " << m_triggers.size() << " area triggers for map " << mapId);
	}

	void AreaTriggerManager::ClearTriggers()
	{
		m_triggers.clear();
		m_activeTriggers.clear();
	}

	void AreaTriggerManager::CheckForTriggerOverlap(const Vector3& position, std::vector<uint32>& outNewlyEnteredTriggers)
	{
		outNewlyEnteredTriggers.clear();

		// Check which triggers we're currently in
		std::unordered_set<uint32> currentlyInTriggers;

		for (const auto* trigger : m_triggers)
		{
			if (IsPointInTrigger(*trigger, position))
			{
				currentlyInTriggers.insert(trigger->id());

				// Check if this is a newly entered trigger
				if (m_activeTriggers.find(trigger->id()) == m_activeTriggers.end())
				{
					outNewlyEnteredTriggers.push_back(trigger->id());
				}
			}
		}

		// Update the active triggers set
		m_activeTriggers = std::move(currentlyInTriggers);
	}

	bool AreaTriggerManager::IsPointInTrigger(const proto_client::AreaTriggerEntry& trigger, const Vector3& position)
	{
		const Vector3 triggerCenter(trigger.x(), trigger.y(), trigger.z());

		// Check if it's a sphere trigger
		if (trigger.has_radius())
		{
			return IsPointInSphere(triggerCenter, trigger.radius(), position);
		}
		
		// Check if it's a box trigger
		if (trigger.has_box_x() && trigger.has_box_y() && trigger.has_box_z())
		{
			const Vector3 halfExtents(trigger.box_x() * 0.5f, trigger.box_y() * 0.5f, trigger.box_z() * 0.5f);
			const float orientation = trigger.has_box_o() ? trigger.box_o() : 0.0f;
			return IsPointInBox(triggerCenter, halfExtents, orientation, position);
		}

		// Invalid trigger definition
		WLOG("Area trigger " << trigger.id() << " has neither radius nor box dimensions defined");
		return false;
	}

	bool AreaTriggerManager::IsPointInSphere(const Vector3& center, const float radius, const Vector3& point)
	{
		const Vector3 delta = point - center;
		const float distanceSquared = delta.GetSquaredLength();
		const float radiusSquared = radius * radius;
		return distanceSquared <= radiusSquared;
	}

	bool AreaTriggerManager::IsPointInBox(const Vector3& center, const Vector3& halfExtents, const float orientation, const Vector3& point)
	{
		// Transform point to box local space
		Vector3 localPoint = point - center;

		// Apply inverse rotation if box is oriented
		if (std::abs(orientation) > 1e-6f)
		{
			const float cosAngle = std::cos(-orientation);
			const float sinAngle = std::sin(-orientation);

			// Rotate around Y axis (assuming orientation is yaw)
			const float rotatedX = localPoint.x * cosAngle - localPoint.z * sinAngle;
			const float rotatedZ = localPoint.x * sinAngle + localPoint.z * cosAngle;

			localPoint.x = rotatedX;
			localPoint.z = rotatedZ;
		}

		// Check if point is within box bounds
		return std::abs(localPoint.x) <= halfExtents.x &&
		       std::abs(localPoint.y) <= halfExtents.y &&
		       std::abs(localPoint.z) <= halfExtents.z;
	}
}
