// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spell_zone_set.h"

namespace mmo
{
	uint32 SpellZoneSet::Add(SpellZone zone)
	{
		zone.id = m_nextId++;
		if (m_nextId == 0)
		{
			m_nextId = 1;
		}

		m_zones.push_back(zone);
		return zone.id;
	}

	void SpellZoneSet::Advance(const GameTime now, const std::function<void(const SpellZone&)>& onTick, const std::function<void(const SpellZone&)>& onExpired)
	{
		// Callbacks cast spells, and a spell may create another zone. Work on a snapshot so the
		// vector is never mutated while it is being iterated; new zones join the next advance.
		std::vector<SpellZone> zones;
		zones.swap(m_zones);

		std::vector<SpellZone> expired;
		for (SpellZone& zone : zones)
		{
			if (zone.tickInterval > 0)
			{
				while (zone.nextTickAt <= now && zone.nextTickAt <= zone.expiresAt)
				{
					onTick(zone);
					zone.nextTickAt += zone.tickInterval;
				}
			}

			if (now >= zone.expiresAt)
			{
				expired.push_back(zone);
			}
			else
			{
				m_zones.push_back(zone);
			}
		}

		// Zones added by the callbacks above landed in m_zones already; keep them.
		for (const SpellZone& zone : expired)
		{
			onExpired(zone);
		}
	}

	void SpellZoneSet::RemoveIf(const std::function<bool(const SpellZone&)>& predicate, const std::function<void(const SpellZone&)>& onRemoved)
	{
		std::vector<SpellZone> removed;
		for (auto it = m_zones.begin(); it != m_zones.end();)
		{
			if (predicate(*it))
			{
				removed.push_back(*it);
				it = m_zones.erase(it);
			}
			else
			{
				++it;
			}
		}

		for (const SpellZone& zone : removed)
		{
			onRemoved(zone);
		}
	}

	bool IsInsideSpellZone(const SpellZone& zone, const Vector3& position)
	{
		const float dx = position.x - zone.position.x;
		const float dz = position.z - zone.position.z;
		return dx * dx + dz * dz <= zone.radius * zone.radius;
	}
}
