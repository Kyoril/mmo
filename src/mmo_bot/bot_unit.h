// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/movement_info.h"
#include "game/movement_type.h"
#include "game/object_type_id.h"
#include "math/vector3.h"

#include <array>
#include <string>

namespace mmo
{
	/// @brief Lightweight unit representation for the bot framework.
	/// 
	/// Unlike GameUnitC which depends on Scene, Entity, and rendering infrastructure,
	/// BotUnit stores only the essential data needed for bot decision-making.
	/// This class represents both players and creatures (NPCs) in the game world.
	class BotUnit final
	{
	public:
		/// @brief Default constructor.
		BotUnit() = default;

		/// @brief Constructs a BotUnit with the given GUID and type.
		/// @param guid The unique identifier for this unit.
		/// @param typeId The type of the object (Unit or Player).
		explicit BotUnit(uint64 guid, ObjectTypeId typeId);

	public:
		// ============================================================
		// Identity
		// ============================================================

		/// @brief Gets the unique identifier for this unit.
		/// @return The GUID of this unit.
		uint64 GetGuid() const { return m_guid; }

		/// @brief Gets the object type ID.
		/// @return The type ID (Unit or Player).
		ObjectTypeId GetTypeId() const { return m_typeId; }

		/// @brief Gets the creature/NPC template entry ID.
		/// @return The entry ID for creatures, 0 for players.
		uint32 GetEntry() const { return m_entry; }

		/// @brief Gets the display name of the unit.
		/// @return The name (for players) or empty string (for creatures without name query).
		const std::string& GetName() const { return m_name; }

		/// @brief Checks if this unit is a player character.
		/// @return True if this is a player, false if it's a creature/NPC.
		bool IsPlayer() const { return m_typeId == ObjectTypeId::Player; }

		/// @brief Checks if this unit is a creature/NPC.
		/// @return True if this is a creature, false if it's a player.
		bool IsCreature() const { return m_typeId == ObjectTypeId::Unit; }

		// ============================================================
		// Position & Movement
		// ============================================================

		/// @brief Gets the current position of the unit.
		/// @return The position in world coordinates.
		const Vector3& GetPosition() const { return m_position; }

		/// @brief Gets the facing direction of the unit.
		/// @return The facing direction in radians.
		Radian GetFacing() const { return m_facing; }

		/// @brief Gets the movement information of the unit.
		/// @return The current movement state.
		const MovementInfo& GetMovementInfo() const { return m_movementInfo; }

		/// @brief Gets the movement speed for a specific movement type.
		/// @param type The movement type (Walk, Run, etc.).
		/// @return The speed value.
		float GetSpeed(MovementType type) const;

		/// @brief Calculates the distance to another point.
		/// @param point The target point.
		/// @return The distance in world units.
		float GetDistanceTo(const Vector3& point) const;

		/// @brief Calculates the squared distance to another point (faster than GetDistanceTo).
		/// @param point The target point.
		/// @return The squared distance.
		float GetDistanceToSquared(const Vector3& point) const;

		/// @brief Calculates the distance to another unit.
		/// @param other The other unit.
		/// @return The distance in world units.
		float GetDistanceTo(const BotUnit& other) const;

		// ============================================================
		// Stats & State
		// ============================================================

		/// @brief Gets the level of the unit.
		/// @return The level.
		uint32 GetLevel() const { return m_level; }

		/// @brief Gets the current health of the unit.
		/// @return The current health points.
		uint32 GetHealth() const { return m_health; }

		/// @brief Gets the maximum health of the unit.
		/// @return The maximum health points.
		uint32 GetMaxHealth() const { return m_maxHealth; }

		/// @brief Gets the health as a percentage (0.0 to 1.0).
		/// @return The health percentage.
		float GetHealthPercent() const;

		/// @brief Gets the faction template ID.
		/// @return The faction template ID.
		uint32 GetFactionTemplate() const { return m_factionTemplate; }

		/// @brief Gets the display ID (model).
		/// @return The display ID.
		uint32 GetDisplayId() const { return m_displayId; }

		/// @brief Gets the unit flags.
		/// @return The unit flags bitmask.
		uint32 GetUnitFlags() const { return m_unitFlags; }

		/// @brief Gets the NPC flags.
		/// @return The NPC flags bitmask.
		uint32 GetNpcFlags() const { return m_npcFlags; }

		/// @brief Gets the GUID of the unit's current target.
		/// @return The target GUID, or 0 if no target.
		uint64 GetTargetGuid() const { return m_targetGuid; }

		// ============================================================
		// State Queries
		// ============================================================

		/// @brief Checks if the unit is alive.
		/// @return True if health > 0, false otherwise.
		bool IsAlive() const { return m_health > 0; }

		/// @brief Checks if the unit is dead.
		/// @return True if health == 0, false otherwise.
		bool IsDead() const { return m_health == 0; }

		/// @brief Checks if the unit is in combat.
		/// @return True if the InCombat flag is set.
		bool IsInCombat() const;

		/// @brief Checks if the unit is moving.
		/// @return True if any movement flag is set.
		bool IsMoving() const;

		/// @brief Checks if this unit is targeting the specified unit.
		/// @param targetGuid The GUID to check against.
		/// @return True if this unit is targeting the specified GUID.
		bool IsTargeting(uint64 targetGuid) const { return m_targetGuid == targetGuid; }

		// ============================================================
		// Faction/Relationship Queries (Simplified)
		// ============================================================

		/// @brief Checks if this unit is hostile to another unit (simplified check).
		/// 
		/// This is a simplified hostility check based on faction templates.
		/// For full accuracy, faction data would need to be loaded from proto files.
		/// 
		/// Current logic:
		/// - Players are never hostile to other players in this simplified check
		/// - Creatures with certain faction templates may be hostile
		/// 
		/// @param other The other unit to check against.
		/// @return True if this unit is hostile to the other unit.
		bool IsHostileTo(const BotUnit& other) const;

		/// @brief Checks if this unit is friendly to another unit (simplified check).
		/// @param other The other unit to check against.
		/// @return True if this unit is friendly to the other unit.
		bool IsFriendlyTo(const BotUnit& other) const;

		/// @brief Checks if this unit can be attacked by the specified unit.
		/// 
		/// This considers neutral creatures (like boars) as attackable even if
		/// they are not actively hostile. A unit is attackable if:
		/// - It is not the same unit
		/// - It is alive
		/// - It is a creature without NPC flags (vendor, quest giver, etc.)
		/// 
		/// @param attacker The unit that wants to attack this unit.
		/// @return True if this unit can be attacked.
		bool IsAttackableBy(const BotUnit& attacker) const;

		// ============================================================
		// Setters (for BotObjectManager to update)
		// ============================================================

		/// @brief Sets the entry ID.
		void SetEntry(uint32 entry) { m_entry = entry; }

		/// @brief Sets the name.
		void SetName(const std::string& name) { m_name = name; }

		/// @brief Sets the position.
		void SetPosition(const Vector3& position) { m_position = position; }

		/// @brief Sets the facing direction.
		void SetFacing(Radian facing) { m_facing = facing; }

		/// @brief Sets the movement information.
		void SetMovementInfo(const MovementInfo& info);

		/// @brief Sets a movement speed.
		void SetSpeed(MovementType type, float speed);

		/// @brief Sets all movement speeds from an array.
		void SetSpeeds(const std::array<float, static_cast<size_t>(MovementType::Count)>& speeds);

		/// @brief Sets the level.
		void SetLevel(uint32 level) { m_level = level; }

		/// @brief Sets the current health.
		void SetHealth(uint32 health) { m_health = health; }

		/// @brief Sets the maximum health.
		void SetMaxHealth(uint32 maxHealth) { m_maxHealth = maxHealth; }

		/// @brief Sets the faction template ID.
		void SetFactionTemplate(uint32 factionTemplate) { m_factionTemplate = factionTemplate; }

		/// @brief Sets the display ID.
		void SetDisplayId(uint32 displayId) { m_displayId = displayId; }

		/// @brief Sets the unit flags.
		void SetUnitFlags(uint32 flags) { m_unitFlags = flags; }

		/// @brief Sets the NPC flags.
		void SetNpcFlags(uint32 flags) { m_npcFlags = flags; }

		/// @brief Sets the target GUID.
		void SetTargetGuid(uint64 targetGuid) { m_targetGuid = targetGuid; }

	private:
		// Identity
		uint64 m_guid = 0;
		ObjectTypeId m_typeId = ObjectTypeId::Object;
		uint32 m_entry = 0;
		std::string m_name;

		// Position & Movement
		Vector3 m_position = Vector3::Zero;
		Radian m_facing = Radian(0.0f);
		MovementInfo m_movementInfo;
		std::array<float, static_cast<size_t>(MovementType::Count)> m_speeds = {};

		// Stats & State
		uint32 m_level = 1;
		uint32 m_health = 0;
		uint32 m_maxHealth = 0;
		uint32 m_factionTemplate = 0;
		uint32 m_displayId = 0;
		uint32 m_unitFlags = 0;
		uint32 m_npcFlags = 0;
		uint64 m_targetGuid = 0;
	};

}
