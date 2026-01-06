// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_unit_watcher.h"
#include "bot_object_manager.h"

namespace mmo
{
	BotUnitWatcher::BotUnitWatcher(
		BotObjectManager& objectManager,
		const Vector3& center,
		const float radius,
		const bool excludeSelf)
		: m_objectManager(objectManager)
		, m_center(center)
		, m_radius(radius)
		, m_radiusSquared(radius * radius)
		, m_excludeSelf(excludeSelf)
	{
		// Connect to object manager signals
		m_spawnedConnection = m_objectManager.UnitSpawned.connect(
			[this](const BotUnit& unit) { OnUnitSpawned(unit); });
		m_despawnedConnection = m_objectManager.UnitDespawned.connect(
			[this](uint64 guid) { OnUnitDespawned(guid); });
		m_updatedConnection = m_objectManager.UnitUpdated.connect(
			[this](const BotUnit& unit) { OnUnitUpdated(unit); });

		// Perform initial scan to populate watched units
		Rescan();
	}

	void BotUnitWatcher::SetCenter(const Vector3& center)
	{
		if (m_center != center)
		{
			m_center = center;
			// Note: Don't rescan here - let Update() handle movement detection
			// to properly emit enter/leave events
		}
	}

	void BotUnitWatcher::SetRadius(const float radius)
	{
		if (m_radius != radius)
		{
			m_radius = radius;
			m_radiusSquared = radius * radius;
			// Note: Don't rescan here - let Update() handle the change
		}
	}

	void BotUnitWatcher::Update()
	{
		// Check all currently watched units to see if any have left
		std::vector<uint64> leftUnits;
		for (const uint64 guid : m_watchedUnits)
		{
			const BotUnit* unit = m_objectManager.GetUnit(guid);
			if (!unit || !IsWithinRadius(*unit))
			{
				leftUnits.push_back(guid);
			}
		}

		// Remove units that left and emit events
		for (const uint64 guid : leftUnits)
		{
			m_watchedUnits.erase(guid);
			UnitLeft(guid);
		}

		// Check all units in the object manager to see if any have entered
		m_objectManager.ForEachUnit([this](const BotUnit& unit)
		{
			const uint64 guid = unit.GetGuid();

			// Skip if excluded
			if (m_excludeSelf && guid == m_objectManager.GetSelfGuid())
			{
				return;
			}

			// Skip if already watched
			if (m_watchedUnits.count(guid) > 0)
			{
				return;
			}

			// Check if now in range
			if (IsWithinRadius(unit))
			{
				m_watchedUnits.insert(guid);
				UnitEntered(unit);
			}
		});
	}

	void BotUnitWatcher::Rescan()
	{
		// Clear existing state without emitting leave events
		m_watchedUnits.clear();

		// Scan all units and add those in range
		m_objectManager.ForEachUnit([this](const BotUnit& unit)
		{
			const uint64 guid = unit.GetGuid();

			// Skip if excluded
			if (m_excludeSelf && guid == m_objectManager.GetSelfGuid())
			{
				return;
			}

			if (IsWithinRadius(unit))
			{
				m_watchedUnits.insert(guid);
			}
		});
	}

	bool BotUnitWatcher::IsUnitInArea(const uint64 guid) const
	{
		return m_watchedUnits.count(guid) > 0;
	}

	void BotUnitWatcher::ForEachWatchedUnit(const std::function<void(const BotUnit&)>& callback) const
	{
		for (const uint64 guid : m_watchedUnits)
		{
			if (const BotUnit* unit = m_objectManager.GetUnit(guid))
			{
				callback(*unit);
			}
		}
	}

	bool BotUnitWatcher::IsWithinRadius(const BotUnit& unit) const
	{
		return unit.GetDistanceToSquared(m_center) <= m_radiusSquared;
	}

	void BotUnitWatcher::OnUnitSpawned(const BotUnit& unit)
	{
		const uint64 guid = unit.GetGuid();

		// Skip if excluded
		if (m_excludeSelf && guid == m_objectManager.GetSelfGuid())
		{
			return;
		}

		// Check if the new unit is within range
		if (IsWithinRadius(unit))
		{
			m_watchedUnits.insert(guid);
			UnitEntered(unit);
		}
	}

	void BotUnitWatcher::OnUnitDespawned(const uint64 guid)
	{
		// If unit was being watched, remove and emit event
		if (m_watchedUnits.erase(guid) > 0)
		{
			UnitLeft(guid);
		}
	}

	void BotUnitWatcher::OnUnitUpdated(const BotUnit& unit)
	{
		const uint64 guid = unit.GetGuid();

		// Skip if excluded
		if (m_excludeSelf && guid == m_objectManager.GetSelfGuid())
		{
			return;
		}

		const bool wasWatched = m_watchedUnits.count(guid) > 0;
		const bool isInRange = IsWithinRadius(unit);

		if (wasWatched && !isInRange)
		{
			// Unit left the area
			m_watchedUnits.erase(guid);
			UnitLeft(guid);
		}
		else if (!wasWatched && isInRange)
		{
			// Unit entered the area
			m_watchedUnits.insert(guid);
			UnitEntered(unit);
		}
		else if (wasWatched && isInRange)
		{
			// Unit is still in area but was updated
			UnitUpdated(unit);
		}
	}

}
