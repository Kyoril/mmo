// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_object_manager.h"

#include <limits>

namespace mmo
{
	const BotUnit* BotObjectManager::GetSelf() const
	{
		if (m_selfGuid == 0)
		{
			return nullptr;
		}

		return GetUnit(m_selfGuid);
	}

	BotUnit* BotObjectManager::GetSelfMutable()
	{
		if (m_selfGuid == 0)
		{
			return nullptr;
		}

		return GetUnitMutable(m_selfGuid);
	}

	bool BotObjectManager::AddOrUpdateUnit(const BotUnit& unit)
	{
		const uint64 guid = unit.GetGuid();
		
		auto it = m_units.find(guid);
		if (it != m_units.end())
		{
			// Update existing unit
			it->second = unit;
			UnitUpdated(it->second);
			return false;
		}
		else
		{
			// Add new unit
			auto [insertIt, inserted] = m_units.emplace(guid, unit);
			if (inserted)
			{
				UnitSpawned(insertIt->second);
			}
			return inserted;
		}
	}

	bool BotObjectManager::RemoveUnit(const uint64 guid)
	{
		auto it = m_units.find(guid);
		if (it != m_units.end())
		{
			m_units.erase(it);
			UnitDespawned(guid);
			return true;
		}

		return false;
	}

	void BotObjectManager::Clear()
	{
		// Emit despawn signals for all units
		for (const auto& [guid, unit] : m_units)
		{
			UnitDespawned(guid);
		}

		m_units.clear();
	}

	const BotUnit* BotObjectManager::GetUnit(const uint64 guid) const
	{
		auto it = m_units.find(guid);
		if (it != m_units.end())
		{
			return &it->second;
		}

		return nullptr;
	}

	BotUnit* BotObjectManager::GetUnitMutable(const uint64 guid)
	{
		auto it = m_units.find(guid);
		if (it != m_units.end())
		{
			return &it->second;
		}

		return nullptr;
	}

	bool BotObjectManager::HasUnit(const uint64 guid) const
	{
		return m_units.find(guid) != m_units.end();
	}

	void BotObjectManager::FindUnitsInRadius(
		const Vector3& center,
		const float radius,
		const UnitCallback& callback) const
	{
		const float radiusSquared = radius * radius;

		for (const auto& [guid, unit] : m_units)
		{
			if (unit.GetDistanceToSquared(center) <= radiusSquared)
			{
				if (!callback(unit))
				{
					return;  // Callback requested to stop
				}
			}
		}
	}

	const BotUnit* BotObjectManager::GetNearestUnit(
		const Vector3& position,
		const std::function<bool(const BotUnit&)>& filter) const
	{
		const BotUnit* nearest = nullptr;
		float nearestDistSq = std::numeric_limits<float>::max();

		for (const auto& [guid, unit] : m_units)
		{
			// Apply filter if provided
			if (filter && !filter(unit))
			{
				continue;
			}

			const float distSq = unit.GetDistanceToSquared(position);
			if (distSq < nearestDistSq)
			{
				nearestDistSq = distSq;
				nearest = &unit;
			}
		}

		return nearest;
	}

	std::vector<const BotUnit*> BotObjectManager::GetNearbyPlayers(
		const Vector3& position,
		const float radius) const
	{
		std::vector<const BotUnit*> result;
		const float radiusSquared = radius * radius;

		for (const auto& [guid, unit] : m_units)
		{
			if (unit.IsPlayer() && unit.GetDistanceToSquared(position) <= radiusSquared)
			{
				result.push_back(&unit);
			}
		}

		return result;
	}

	std::vector<const BotUnit*> BotObjectManager::GetNearbyCreatures(
		const Vector3& position,
		const float radius) const
	{
		std::vector<const BotUnit*> result;
		const float radiusSquared = radius * radius;

		for (const auto& [guid, unit] : m_units)
		{
			if (unit.IsCreature() && unit.GetDistanceToSquared(position) <= radiusSquared)
			{
				result.push_back(&unit);
			}
		}

		return result;
	}

	std::vector<const BotUnit*> BotObjectManager::GetNearbyUnits(
		const Vector3& position,
		const float radius) const
	{
		std::vector<const BotUnit*> result;
		const float radiusSquared = radius * radius;

		for (const auto& [guid, unit] : m_units)
		{
			if (unit.GetDistanceToSquared(position) <= radiusSquared)
			{
				result.push_back(&unit);
			}
		}

		return result;
	}

	const BotUnit* BotObjectManager::GetNearestHostile(const float maxRange) const
	{
		const BotUnit* self = GetSelf();
		if (!self)
		{
			return nullptr;
		}

		const Vector3& selfPos = self->GetPosition();
		const float maxRangeSq = maxRange * maxRange;

		return GetNearestUnit(selfPos, [this, self, maxRangeSq, &selfPos](const BotUnit& unit)
		{
			// Skip self
			if (unit.GetGuid() == m_selfGuid)
			{
				return false;
			}

			// Must be alive
			if (!unit.IsAlive())
			{
				return false;
			}

			// Must be within range
			if (unit.GetDistanceToSquared(selfPos) > maxRangeSq)
			{
				return false;
			}

			// Must be hostile
			return unit.IsHostileTo(*self);
		});
	}

	const BotUnit* BotObjectManager::GetNearestAttackable(const float maxRange) const
	{
		const BotUnit* self = GetSelf();
		if (!self)
		{
			return nullptr;
		}

		const Vector3& selfPos = self->GetPosition();
		const float maxRangeSq = maxRange * maxRange;

		return GetNearestUnit(selfPos, [this, self, maxRangeSq, &selfPos](const BotUnit& unit)
		{
			// Must be within range
			if (unit.GetDistanceToSquared(selfPos) > maxRangeSq)
			{
				return false;
			}

			// Use the attackability check
			return unit.IsAttackableBy(*self);
		});
	}

	const BotUnit* BotObjectManager::GetNearestFriendly(const float maxRange) const
	{
		const BotUnit* self = GetSelf();
		if (!self)
		{
			return nullptr;
		}

		const Vector3& selfPos = self->GetPosition();
		const float maxRangeSq = maxRange * maxRange;

		return GetNearestUnit(selfPos, [this, self, maxRangeSq, &selfPos](const BotUnit& unit)
		{
			// Skip self
			if (unit.GetGuid() == m_selfGuid)
			{
				return false;
			}

			// Must be alive
			if (!unit.IsAlive())
			{
				return false;
			}

			// Must be within range
			if (unit.GetDistanceToSquared(selfPos) > maxRangeSq)
			{
				return false;
			}

			// Must be friendly
			return unit.IsFriendlyTo(*self);
		});
	}

	const BotUnit* BotObjectManager::GetNearestFriendlyPlayer(const float maxRange) const
	{
		const BotUnit* self = GetSelf();
		if (!self)
		{
			return nullptr;
		}

		const Vector3& selfPos = self->GetPosition();
		const float maxRangeSq = maxRange * maxRange;

		return GetNearestUnit(selfPos, [this, self, maxRangeSq, &selfPos](const BotUnit& unit)
		{
			// Skip self
			if (unit.GetGuid() == m_selfGuid)
			{
				return false;
			}

			// Must be a player
			if (!unit.IsPlayer())
			{
				return false;
			}

			// Must be alive
			if (!unit.IsAlive())
			{
				return false;
			}

			// Must be within range
			if (unit.GetDistanceToSquared(selfPos) > maxRangeSq)
			{
				return false;
			}

			// Must be friendly
			return unit.IsFriendlyTo(*self);
		});
	}

	std::vector<const BotUnit*> BotObjectManager::GetHostilesInRange(const float maxRange) const
	{
		std::vector<const BotUnit*> result;

		const BotUnit* self = GetSelf();
		if (!self)
		{
			return result;
		}

		const Vector3& selfPos = self->GetPosition();
		const float maxRangeSq = maxRange * maxRange;

		for (const auto& [guid, unit] : m_units)
		{
			// Skip self
			if (guid == m_selfGuid)
			{
				continue;
			}

			// Must be alive
			if (!unit.IsAlive())
			{
				continue;
			}

			// Must be within range
			if (unit.GetDistanceToSquared(selfPos) > maxRangeSq)
			{
				continue;
			}

			// Must be hostile
			if (unit.IsHostileTo(*self))
			{
				result.push_back(&unit);
			}
		}

		return result;
	}

	std::vector<const BotUnit*> BotObjectManager::GetFriendlyPlayersInRange(const float maxRange) const
	{
		std::vector<const BotUnit*> result;

		const BotUnit* self = GetSelf();
		if (!self)
		{
			return result;
		}

		const Vector3& selfPos = self->GetPosition();
		const float maxRangeSq = maxRange * maxRange;

		for (const auto& [guid, unit] : m_units)
		{
			// Skip self
			if (guid == m_selfGuid)
			{
				continue;
			}

			// Must be a player
			if (!unit.IsPlayer())
			{
				continue;
			}

			// Must be alive
			if (!unit.IsAlive())
			{
				continue;
			}

			// Must be within range
			if (unit.GetDistanceToSquared(selfPos) > maxRangeSq)
			{
				continue;
			}

			// Must be friendly
			if (unit.IsFriendlyTo(*self))
			{
				result.push_back(&unit);
			}
		}

		return result;
	}

	std::vector<const BotUnit*> BotObjectManager::GetUnitsTargetingSelf(const float maxRange) const
	{
		std::vector<const BotUnit*> result;

		if (m_selfGuid == 0)
		{
			return result;
		}

		const BotUnit* self = GetSelf();
		const float maxRangeSq = maxRange * maxRange;
		const bool checkRange = maxRange > 0.0f && self != nullptr;

		for (const auto& [guid, unit] : m_units)
		{
			// Skip self
			if (guid == m_selfGuid)
			{
				continue;
			}

			// Must be targeting us
			if (unit.GetTargetGuid() != m_selfGuid)
			{
				continue;
			}

			// Range check if requested
			if (checkRange && unit.GetDistanceToSquared(self->GetPosition()) > maxRangeSq)
			{
				continue;
			}

			result.push_back(&unit);
		}

		return result;
	}

}
