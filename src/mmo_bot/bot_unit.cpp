// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_unit.h"

namespace mmo
{
	BotUnit::BotUnit(const uint64 guid, const ObjectTypeId typeId)
		: m_guid(guid)
		, m_typeId(typeId)
	{
		// Initialize default speeds
		m_speeds[static_cast<size_t>(MovementType::Walk)] = 2.5f;
		m_speeds[static_cast<size_t>(MovementType::Run)] = 7.0f;
		m_speeds[static_cast<size_t>(MovementType::Backwards)] = 4.5f;
		m_speeds[static_cast<size_t>(MovementType::Swim)] = 4.722222f;
		m_speeds[static_cast<size_t>(MovementType::SwimBackwards)] = 2.5f;
		m_speeds[static_cast<size_t>(MovementType::Turn)] = 3.141593f;
		m_speeds[static_cast<size_t>(MovementType::Flight)] = 7.0f;
		m_speeds[static_cast<size_t>(MovementType::FlightBackwards)] = 4.5f;
	}

	float BotUnit::GetSpeed(const MovementType type) const
	{
		const size_t index = static_cast<size_t>(type);
		if (index < m_speeds.size())
		{
			return m_speeds[index];
		}

		return 0.0f;
	}

	float BotUnit::GetDistanceTo(const Vector3& point) const
	{
		return (m_position - point).GetLength();
	}

	float BotUnit::GetDistanceToSquared(const Vector3& point) const
	{
		return (m_position - point).GetSquaredLength();
	}

	float BotUnit::GetDistanceTo(const BotUnit& other) const
	{
		return GetDistanceTo(other.GetPosition());
	}

	float BotUnit::GetHealthPercent() const
	{
		if (m_maxHealth == 0)
		{
			return 0.0f;
		}

		return static_cast<float>(m_health) / static_cast<float>(m_maxHealth);
	}

	bool BotUnit::IsInCombat() const
	{
		// InCombat flag is 0x00000001 in unit_flags
		return (m_unitFlags & 0x00000001) != 0;
	}

	bool BotUnit::IsMoving() const
	{
		// Check for any directional movement flags
		constexpr uint32 movementMask = 
			movement_flags::Forward | 
			movement_flags::Backward | 
			movement_flags::StrafeLeft | 
			movement_flags::StrafeRight |
			movement_flags::TurnLeft |
			movement_flags::TurnRight;

		return (m_movementInfo.movementFlags & movementMask) != 0;
	}

	bool BotUnit::IsHostileTo(const BotUnit& other) const
	{
		// Simplified hostility check
		// In a full implementation, this would use faction data from proto files

		// Same unit is never hostile to itself
		if (m_guid == other.m_guid)
		{
			return false;
		}

		// Players are generally not hostile to each other in PvE
		// (would need PvP flag checking for full implementation)
		if (IsPlayer() && other.IsPlayer())
		{
			return false;
		}

		// If we're a creature targeting a player, we're likely hostile
		if (IsCreature() && other.IsPlayer())
		{
			// Creatures with friendly NPC flags are not hostile
			// Quest givers, vendors, etc.
			if (m_npcFlags != 0)
			{
				return false;
			}

			// If we're targeting them, we're hostile
			if (m_targetGuid == other.m_guid)
			{
				return true;
			}

			// If they're targeting us and in combat, assume hostile
			if (other.m_targetGuid == m_guid && other.IsInCombat())
			{
				return true;
			}
		}

		// If other is a creature targeting us
		if (other.IsCreature() && IsPlayer())
		{
			if (other.m_npcFlags != 0)
			{
				return false;
			}

			if (other.m_targetGuid == m_guid)
			{
				return true;
			}
		}

		// Default: assume hostile creatures with no NPC flags
		if (IsCreature() && m_npcFlags == 0)
		{
			return true;
		}

		if (other.IsCreature() && other.m_npcFlags == 0)
		{
			return true;
		}

		return false;
	}

	bool BotUnit::IsFriendlyTo(const BotUnit& other) const
	{
		// Simplified friendly check - opposite of hostile

		// Same unit is friendly to itself
		if (m_guid == other.m_guid)
		{
			return true;
		}

		// Players are friendly to each other in PvE
		if (IsPlayer() && other.IsPlayer())
		{
			return true;
		}

		// Units with NPC flags (quest givers, vendors) are typically friendly
		if (IsCreature() && m_npcFlags != 0)
		{
			return true;
		}

		if (other.IsCreature() && other.m_npcFlags != 0)
		{
			return true;
		}

		return !IsHostileTo(other);
	}

	void BotUnit::SetMovementInfo(const MovementInfo& info)
	{
		m_movementInfo = info;
		m_position = info.position;
		m_facing = info.facing;
	}

	void BotUnit::SetSpeed(const MovementType type, const float speed)
	{
		const size_t index = static_cast<size_t>(type);
		if (index < m_speeds.size())
		{
			m_speeds[index] = speed;
		}
	}

	void BotUnit::SetSpeeds(const std::array<float, static_cast<size_t>(MovementType::Count)>& speeds)
	{
		m_speeds = speeds;
	}

}
