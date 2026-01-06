// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_unit.h"
#include "base/signal.h"
#include "math/vector3.h"

#include <set>
#include <functional>

namespace mmo
{
	class BotObjectManager;

	/// @brief Watches a circular area and emits events when units enter or leave.
	/// 
	/// The watcher monitors the BotObjectManager and tracks which units are within
	/// a specified radius of a center point. When units cross the boundary (enter
	/// or leave), appropriate signals are emitted.
	/// 
	/// Usage:
	/// @code
	/// BotUnitWatcher watcher(objectManager, myPosition, 30.0f);
	/// watcher.UnitEntered.connect([](const BotUnit& unit) {
	///     // React to unit entering the watched area
	/// });
	/// // Call Update() periodically or after object manager changes
	/// watcher.Update();
	/// @endcode
	class BotUnitWatcher final
	{
	public:
		/// @brief Emitted when a unit enters the watched area.
		signal<void(const BotUnit&)> UnitEntered;

		/// @brief Emitted when a unit leaves the watched area.
		signal<void(uint64)> UnitLeft;

		/// @brief Emitted when a unit within the watched area is updated.
		signal<void(const BotUnit&)> UnitUpdated;

	public:
		/// @brief Constructs a unit watcher.
		/// @param objectManager The object manager to monitor.
		/// @param center The center point of the watched area.
		/// @param radius The radius of the watched area.
		/// @param excludeSelf Whether to exclude the bot's own unit from events.
		BotUnitWatcher(
			BotObjectManager& objectManager,
			const Vector3& center,
			float radius,
			bool excludeSelf = true);

		/// @brief Destructor.
		~BotUnitWatcher() = default;

		// Non-copyable
		BotUnitWatcher(const BotUnitWatcher&) = delete;
		BotUnitWatcher& operator=(const BotUnitWatcher&) = delete;

		// Movable
		BotUnitWatcher(BotUnitWatcher&&) = default;
		BotUnitWatcher& operator=(BotUnitWatcher&&) = default;

	public:
		/// @brief Sets the center of the watched area.
		/// @param center The new center point.
		void SetCenter(const Vector3& center);

		/// @brief Gets the center of the watched area.
		/// @return The center point.
		const Vector3& GetCenter() const { return m_center; }

		/// @brief Sets the radius of the watched area.
		/// @param radius The new radius.
		void SetRadius(float radius);

		/// @brief Gets the radius of the watched area.
		/// @return The radius.
		float GetRadius() const { return m_radius; }

		/// @brief Sets whether to exclude the bot's own unit from events.
		/// @param exclude True to exclude self.
		void SetExcludeSelf(bool exclude) { m_excludeSelf = exclude; }

		/// @brief Gets whether the bot's own unit is excluded from events.
		/// @return True if self is excluded.
		bool GetExcludeSelf() const { return m_excludeSelf; }

		/// @brief Updates the watcher state and emits enter/leave events.
		/// 
		/// Call this periodically (e.g., each frame or profile update cycle)
		/// to detect units that have moved into or out of the watched area.
		void Update();

		/// @brief Forces a full rescan, clearing current state and rebuilding.
		/// 
		/// Use this after changing center or radius to properly detect
		/// enter/leave events based on the new configuration.
		void Rescan();

		/// @brief Gets the GUIDs of all units currently in the watched area.
		/// @return Set of GUIDs.
		const std::set<uint64>& GetWatchedUnitGuids() const { return m_watchedUnits; }

		/// @brief Gets the number of units currently in the watched area.
		/// @return The count.
		size_t GetWatchedUnitCount() const { return m_watchedUnits.size(); }

		/// @brief Checks if a specific unit is in the watched area.
		/// @param guid The GUID to check.
		/// @return True if the unit is in the area.
		bool IsUnitInArea(uint64 guid) const;

		/// @brief Iterates over all units currently in the watched area.
		/// @param callback The callback to invoke for each unit.
		void ForEachWatchedUnit(const std::function<void(const BotUnit&)>& callback) const;

	private:
		/// @brief Checks if a unit is within the watched radius.
		bool IsWithinRadius(const BotUnit& unit) const;

		/// @brief Handles unit spawn events from the object manager.
		void OnUnitSpawned(const BotUnit& unit);

		/// @brief Handles unit despawn events from the object manager.
		void OnUnitDespawned(uint64 guid);

		/// @brief Handles unit update events from the object manager.
		void OnUnitUpdated(const BotUnit& unit);

	private:
		BotObjectManager& m_objectManager;
		Vector3 m_center;
		float m_radius;
		float m_radiusSquared;
		bool m_excludeSelf;

		/// @brief Set of GUIDs of units currently in the watched area.
		std::set<uint64> m_watchedUnits;

		/// @brief Connections to object manager signals.
		scoped_connection m_spawnedConnection;
		scoped_connection m_despawnedConnection;
		scoped_connection m_updatedConnection;
	};

}
