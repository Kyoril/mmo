// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "npc_index.h"

#include "grind_spot_index.h"

#include "proto_data/project.h"

namespace mmo
{
	void NpcIndex::Build(const proto::Project& project, const uint32 playerFactionTemplate)
	{
		m_vendorsByMap.clear();
		m_vendorCount = 0;

		// Template 0 exists ("PLAYER, Human"); only a template the data does not have skips the check.
		const bool checkFaction = project.factionTemplates.getById(playerFactionTemplate) != nullptr;

		const auto& maps = project.maps.getTemplates();
		for (int mapIndex = 0; mapIndex < maps.entry_size(); ++mapIndex)
		{
			const proto::MapEntry& map = maps.entry(mapIndex);
			for (int spawnIndex = 0; spawnIndex < map.unitspawns_size(); ++spawnIndex)
			{
				const proto::UnitSpawnEntry& spawn = map.unitspawns(spawnIndex);
				if (!spawn.isactive())
				{
					continue;
				}

				const proto::UnitEntry* unit = project.units.getById(spawn.unitentry());
				// The vendor entry is what makes a vendor; the server derives the NPC flag from it at
				// spawn, and the data leaves npcflags empty.
				if (!unit || unit->vendorentry() == 0)
				{
					continue;
				}

				// A hostile vendor will not trade, and walking to one would be a trip for nothing.
				if (checkFaction && IsFactionHostile(project, playerFactionTemplate, unit->factiontemplate()))
				{
					continue;
				}

				BotNpcSpot spot;
				spot.mapId = map.id();
				spot.unitEntry = unit->id();
				spot.position = Vector3(spawn.positionx(), spawn.positiony(), spawn.positionz());
				m_vendorsByMap[spot.mapId].push_back(spot);
				++m_vendorCount;
			}
		}
	}

	std::optional<BotNpcSpot> NpcIndex::FindNearestVendor(const uint32 mapId, const Vector3& position) const
	{
		const auto it = m_vendorsByMap.find(mapId);
		if (it == m_vendorsByMap.end())
		{
			return std::nullopt;
		}

		const BotNpcSpot* nearest = nullptr;
		float nearestDistance = 0.0f;
		for (const BotNpcSpot& spot : it->second)
		{
			const float distance = (spot.position - position).GetLength();
			if (!nearest || distance < nearestDistance)
			{
				nearest = &spot;
				nearestDistance = distance;
			}
		}

		return nearest ? std::optional<BotNpcSpot>(*nearest) : std::nullopt;
	}
}
