// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_unit.h"
#include "base/signal.h"

#include <functional>
#include <unordered_map>
#include <vector>

namespace mmo
{
	/// @brief Lightweight state of an item or container object known to the bot.
	struct BotItemState final
	{
		uint64 guid { 0 };
		uint32 entry { 0 };
		uint32 stackCount { 0 };
		uint64 ownerGuid { 0 };
	};

	/// @brief Lightweight state of a world object (chest, door, ...) known to the bot.
	struct BotWorldObjectState final
	{
		uint64 guid { 0 };
		uint32 entry { 0 };
		/// The game_world_object_type value (Chest, Door, Mailbox, QuestObject).
		uint32 type { 0 };
		/// The object's State field (doors: 0 = closed, 1 = open).
		uint32 state { 0 };
		Vector3 position;
	};

	/// @brief Manages all known units in the bot's awareness.
	///
	/// This class acts as the central registry for all units (players and creatures)
	/// that the bot knows about through UpdateObject packets from the server.
	/// It provides query methods for finding units by various criteria and emits
	/// signals when units are added, updated, or removed.
	class BotObjectManager final
	{
	public:
		/// @brief Emitted when a new unit is spawned/created.
		signal<void(const BotUnit&)> UnitSpawned;

		/// @brief Emitted when a unit is despawned/destroyed.
		signal<void(uint64)> UnitDespawned;

		/// @brief Emitted when a unit's data is updated.
		signal<void(const BotUnit&)> UnitUpdated;

	public:
		/// @brief Default constructor.
		BotObjectManager() = default;

		/// @brief Destructor.
		~BotObjectManager() = default;

		// Non-copyable
		BotObjectManager(const BotObjectManager&) = delete;
		BotObjectManager& operator=(const BotObjectManager&) = delete;

		// Movable
		BotObjectManager(BotObjectManager&&) = default;
		BotObjectManager& operator=(BotObjectManager&&) = default;

	public:
		// ============================================================
		// Self Management
		// ============================================================

		/// @brief Sets the GUID of the bot's own character.
		/// 
		/// This is used to exclude the bot from hostile queries and to
		/// provide quick access to the bot's own unit data.
		/// 
		/// @param guid The GUID of the bot's character.
		void SetSelfGuid(uint64 guid) { m_selfGuid = guid; }

		/// @brief Gets the GUID of the bot's own character.
		/// @return The bot's GUID, or 0 if not set.
		uint64 GetSelfGuid() const { return m_selfGuid; }

		/// @brief Gets the bot's own unit data.
		/// @return Pointer to the bot's unit, or nullptr if not found.
		const BotUnit* GetSelf() const;

		/// @brief Gets the bot's own unit data (mutable).
		/// @return Pointer to the bot's unit, or nullptr if not found.
		BotUnit* GetSelfMutable();

		// ============================================================
		// Unit Management
		// ============================================================

		/// @brief Adds a new unit or updates an existing one.
		/// 
		/// If a unit with the same GUID already exists, it will be updated.
		/// Otherwise, a new unit will be created.
		/// 
		/// @param unit The unit to add or update.
		/// @return True if this was a new unit, false if it was an update.
		bool AddOrUpdateUnit(const BotUnit& unit);

		/// @brief Removes a unit by GUID.
		/// @param guid The GUID of the unit to remove.
		/// @return True if the unit was found and removed.
		bool RemoveUnit(uint64 guid);

		/// @brief Clears all units from the manager.
		void Clear();

		/// @brief Gets the total number of known units.
		/// @return The number of units.
		size_t GetUnitCount() const { return m_units.size(); }

		// ============================================================
		// Query by ID
		// ============================================================

		/// @brief Gets a unit by GUID.
		/// @param guid The GUID to look up.
		/// @return Pointer to the unit, or nullptr if not found.
		const BotUnit* GetUnit(uint64 guid) const;

		/// @brief Gets a unit by GUID (mutable).
		/// @param guid The GUID to look up.
		/// @return Pointer to the unit, or nullptr if not found.
		BotUnit* GetUnitMutable(uint64 guid);

		/// @brief Checks if a unit with the given GUID exists.
		/// @param guid The GUID to check.
		/// @return True if the unit exists.
		bool HasUnit(uint64 guid) const;

		// ============================================================
		// Spatial Queries
		// ============================================================

		/// @brief Callback type for unit iteration.
		/// Return true to continue iteration, false to stop.
		using UnitCallback = std::function<bool(const BotUnit&)>;

		/// @brief Finds all units within a radius of a point.
		/// 
		/// The callback is invoked for each unit within the radius.
		/// Return false from the callback to stop iteration early.
		/// 
		/// @param center The center point of the search.
		/// @param radius The search radius.
		/// @param callback The callback to invoke for each found unit.
		void FindUnitsInRadius(
			const Vector3& center,
			float radius,
			const UnitCallback& callback) const;

		/// @brief Finds the nearest unit to a point.
		/// 
		/// @param position The reference position.
		/// @param filter Optional filter predicate. Return true to include a unit.
		/// @return Pointer to the nearest unit, or nullptr if none found.
		const BotUnit* GetNearestUnit(
			const Vector3& position,
			const std::function<bool(const BotUnit&)>& filter = nullptr) const;

		/// @brief Gets all players within a radius.
		/// @param position The center position.
		/// @param radius The search radius.
		/// @return Vector of pointers to matching units.
		std::vector<const BotUnit*> GetNearbyPlayers(
			const Vector3& position,
			float radius) const;

		/// @brief Gets all creatures within a radius.
		/// @param position The center position.
		/// @param radius The search radius.
		/// @return Vector of pointers to matching units.
		std::vector<const BotUnit*> GetNearbyCreatures(
			const Vector3& position,
			float radius) const;

		/// @brief Gets all units within a radius.
		/// @param position The center position.
		/// @param radius The search radius.
		/// @return Vector of pointers to matching units.
		std::vector<const BotUnit*> GetNearbyUnits(
			const Vector3& position,
			float radius) const;

		// ============================================================
		// Convenience Queries (relative to self)
		// ============================================================

		/// @brief Gets the nearest hostile unit to the bot.
		/// 
		/// Uses the bot's own position and faction to determine hostility.
		/// 
		/// @param maxRange Maximum search range.
		/// @return Pointer to the nearest hostile unit, or nullptr if none.
		const BotUnit* GetNearestHostile(float maxRange = 100.0f) const;

		/// @brief Gets the nearest attackable creature to the bot.
		/// 
		/// Returns the nearest creature that can be attacked (any creature without
		/// NPC flags like vendors or quest givers). This includes neutral creatures
		/// like boars that are not actively hostile.
		/// 
		/// @param maxRange Maximum search range.
		/// @return Pointer to the nearest attackable creature, or nullptr if none.
		const BotUnit* GetNearestAttackable(float maxRange = 100.0f) const;

		/// @brief Gets the nearest friendly unit to the bot.
		/// 
		/// Uses the bot's own position and faction to determine friendliness.
		/// Excludes the bot itself from results.
		/// 
		/// @param maxRange Maximum search range.
		/// @return Pointer to the nearest friendly unit, or nullptr if none.
		const BotUnit* GetNearestFriendly(float maxRange = 100.0f) const;

		/// @brief Gets the nearest friendly player to the bot.
		/// 
		/// Excludes the bot itself from results.
		/// 
		/// @param maxRange Maximum search range.
		/// @return Pointer to the nearest friendly player, or nullptr if none.
		const BotUnit* GetNearestFriendlyPlayer(float maxRange = 100.0f) const;

		/// @brief Gets all hostile units within range of the bot.
		/// @param maxRange Maximum search range.
		/// @return Vector of pointers to hostile units.
		std::vector<const BotUnit*> GetHostilesInRange(float maxRange = 100.0f) const;

		/// @brief Gets all friendly players within range of the bot.
		/// 
		/// Excludes the bot itself from results.
		/// 
		/// @param maxRange Maximum search range.
		/// @return Vector of pointers to friendly players.
		std::vector<const BotUnit*> GetFriendlyPlayersInRange(float maxRange = 100.0f) const;

		/// @brief Gets units that are targeting the bot.
		/// @param maxRange Maximum search range (0 = unlimited).
		/// @return Vector of pointers to units targeting the bot.
		std::vector<const BotUnit*> GetUnitsTargetingSelf(float maxRange = 0.0f) const;

		// ============================================================
		// Item Tracking
		// ============================================================

		/// @brief Adds a new item or merges an update into an existing one.
		/// @param item The item state to add or update. Zero-valued fields of an
		///        update are ignored so partial field updates don't wipe known data.
		void AddOrUpdateItem(const BotItemState& item);

		/// @brief Removes an item by GUID.
		/// @param guid The GUID of the item to remove.
		/// @return True if the item was found and removed.
		bool RemoveItem(uint64 guid);

		/// @brief Gets an item by GUID.
		/// @param guid The GUID to look up.
		/// @return Pointer to the item state, or nullptr if not found.
		const BotItemState* GetItem(uint64 guid) const;

		/// @brief Sums the stack counts of all known items with the given entry id.
		/// @param entry The item entry id.
		/// @return The total number of items of that entry the bot knows about.
		uint32 GetItemCountByEntry(uint32 entry) const;

		// ============================================================
		// World Object Tracking
		// ============================================================

		/// @brief Adds a new world object or updates an existing one.
		/// @param object The world object state to add or update.
		void AddOrUpdateWorldObject(const BotWorldObjectState& object);

		/// @brief Removes a world object by GUID.
		/// @param guid The GUID of the world object to remove.
		/// @return True if the world object was found and removed.
		bool RemoveWorldObject(uint64 guid);

		/// @brief Gets a world object by GUID.
		/// @param guid The GUID to look up.
		/// @return Pointer to the world object state, or nullptr if not found.
		const BotWorldObjectState* GetWorldObject(uint64 guid) const;

		/// @brief Finds the first known world object with the given entry id.
		/// @param entry The object entry id.
		/// @return Pointer to the world object state, or nullptr if none is known.
		const BotWorldObjectState* FindWorldObjectByEntry(uint32 entry) const;

		// ============================================================
		// Iteration
		// ============================================================

		/// @brief Iterates over all known units.
		/// @tparam Callback The callback type.
		/// @param callback The callback to invoke for each unit.
		template<typename Callback>
		void ForEachUnit(Callback&& callback) const
		{
			for (const auto& [guid, unit] : m_units)
			{
				callback(unit);
			}
		}

		/// @brief Iterates over all known players.
		/// @tparam Callback The callback type.
		/// @param callback The callback to invoke for each player.
		template<typename Callback>
		void ForEachPlayer(Callback&& callback) const
		{
			for (const auto& [guid, unit] : m_units)
			{
				if (unit.IsPlayer())
				{
					callback(unit);
				}
			}
		}

		/// @brief Iterates over all known creatures.
		/// @tparam Callback The callback type.
		/// @param callback The callback to invoke for each creature.
		template<typename Callback>
		void ForEachCreature(Callback&& callback) const
		{
			for (const auto& [guid, unit] : m_units)
			{
				if (unit.IsCreature())
				{
					callback(unit);
				}
			}
		}

	private:
		/// @brief Map of GUIDs to units.
		std::unordered_map<uint64, BotUnit> m_units;

		/// @brief Map of GUIDs to known item/container objects.
		std::unordered_map<uint64, BotItemState> m_items;

		/// @brief Map of GUIDs to known world objects (chests, doors, ...).
		std::unordered_map<uint64, BotWorldObjectState> m_worldObjects;

		/// @brief The GUID of the bot's own character.
		uint64 m_selfGuid = 0;
	};

}
