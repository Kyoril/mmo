// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "grind_spot_index.h"

#include "log/default_log_levels.h"
#include "proto_data/project.h"

#include <algorithm>
#include <string>

namespace mmo
{
	namespace
	{
		/// Elites and above. A solo bot at its own level loses to these, so they are left out
		/// rather than made a special case in the combat strategy.
		constexpr uint32 NormalRank = 0;

		/// Dungeon and raid maps are not grinding ground: a bot cannot walk into one, and the
		/// spawns inside would otherwise look like perfectly good candidates on the world map.
		[[nodiscard]] bool IsGrindableMap(const proto::MapEntry& map)
		{
			return map.instancetype() == proto::MapEntry_MapInstanceType_GLOBAL;
		}
	}

	bool IsFactionFriendly(
		const proto::Project& project,
		const uint32 attackerFactionTemplate,
		const uint32 defenderFactionTemplate)
	{
		const proto::FactionTemplateEntry* attacker = project.factionTemplates.getById(attackerFactionTemplate);
		const proto::FactionTemplateEntry* defender = project.factionTemplates.getById(defenderFactionTemplate);

		if (!attacker || !defender)
		{
			return false;
		}

		if (attacker == defender || attacker->faction() == defender->faction())
		{
			return true;
		}

		// Enemies win over friends, exactly as the server orders these two checks.
		for (int i = 0; i < attacker->enemies_size(); ++i)
		{
			if (attacker->enemies(i) == defender->faction())
			{
				return false;
			}
		}

		for (int i = 0; i < attacker->friends_size(); ++i)
		{
			if (attacker->friends(i) == defender->faction())
			{
				return true;
			}
		}

		return (attacker->friendmask() & defender->selfmask()) != 0;
	}

	bool IsFactionHostile(
		const proto::Project& project,
		const uint32 attackerFactionTemplate,
		const uint32 defenderFactionTemplate)
	{
		const proto::FactionTemplateEntry* attacker = project.factionTemplates.getById(attackerFactionTemplate);
		const proto::FactionTemplateEntry* defender = project.factionTemplates.getById(defenderFactionTemplate);

		if (!attacker || !defender)
		{
			return false;
		}

		if (attacker == defender || attacker->faction() == defender->faction())
		{
			return false;
		}

		for (int i = 0; i < attacker->enemies_size(); ++i)
		{
			if (attacker->enemies(i) == defender->faction())
			{
				return true;
			}
		}

		// An explicit friend entry outranks the mask below, exactly as it does on the server.
		for (int i = 0; i < attacker->friends_size(); ++i)
		{
			if (attacker->friends(i) == defender->faction())
			{
				return false;
			}
		}

		return attacker->enemymask() != 0 && (attacker->enemymask() & defender->selfmask()) != 0;
	}

	void GrindSpotIndex::Build(const proto::Project& project, const uint32 playerFactionTemplate)
	{
		m_spots.clear();
		m_spotsByMap.clear();
		m_factionFiltered = playerFactionTemplate != 0
			&& project.factionTemplates.getById(playerFactionTemplate) != nullptr;

		const auto& maps = project.maps.getTemplates();
		for (int mapIndex = 0; mapIndex < maps.entry_size(); ++mapIndex)
		{
			const proto::MapEntry& map = maps.entry(mapIndex);
			if (!IsGrindableMap(map))
			{
				continue;
			}

			for (int spawnIndex = 0; spawnIndex < map.unitspawns_size(); ++spawnIndex)
			{
				const proto::UnitSpawnEntry& spawn = map.unitspawns(spawnIndex);
				if (!spawn.isactive())
				{
					continue;
				}

				const proto::UnitEntry* unit = project.units.getById(spawn.unitentry());
				if (!unit)
				{
					continue;
				}

				// Anything with an NPC flag is a vendor, questgiver, trainer or similar. The
				// runtime target picker refuses to attack them, so a spot full of them would
				// send the bot somewhere it can do nothing.
				if (unit->npcflags() != 0)
				{
					continue;
				}

				if (unit->rank() != NormalRank)
				{
					continue;
				}

				if (m_factionFiltered && !IsFactionHostile(project, playerFactionTemplate, unit->factiontemplate()))
				{
					continue;
				}

				GrindSpot spot;
				spot.mapId = map.id();
				spot.unitEntry = unit->id();
				spot.position = Vector3(spawn.positionx(), spawn.positiony(), spawn.positionz());
				spot.minLevel = unit->minlevel();
				spot.maxLevel = unit->maxlevel();
				spot.factionTemplate = unit->factiontemplate();
				spot.maxCount = std::max<uint32>(1, spawn.maxcount());

				m_spotsByMap[spot.mapId].push_back(m_spots.size());
				m_spots.push_back(spot);
			}
		}

		ILOG("Grind spot index built: " << m_spots.size() << " spots across " << m_spotsByMap.size() << " maps"
			<< (m_factionFiltered
				? " (hostile to faction template " + std::to_string(playerFactionTemplate) + ")"
				: " (no faction filter: the bots have no usable faction template)"));
	}

	std::vector<std::size_t> GrindSpotIndex::FindCandidates(
		const uint32 mapId,
		const Vector3& origin,
		const uint32 minLevel,
		const uint32 maxLevel,
		const float maxDistance,
		const std::size_t maxResults) const
	{
		std::vector<std::size_t> results;
		if (maxResults == 0)
		{
			return results;
		}

		const auto mapIt = m_spotsByMap.find(mapId);
		if (mapIt == m_spotsByMap.end())
		{
			return results;
		}

		const float maxDistanceSquared = maxDistance * maxDistance;

		std::vector<std::pair<float, std::size_t>> scored;
		for (const std::size_t index : mapIt->second)
		{
			const GrindSpot& spot = m_spots[index];

			// Overlap rather than containment: a creature spawning at level 4 to 6 is a fine
			// target for a level 5 bot whose band is 2 to 6, and demanding containment would
			// reject most of the world.
			if (spot.maxLevel < minLevel || spot.minLevel > maxLevel)
			{
				continue;
			}

			const float distanceSquared = (spot.position - origin).GetSquaredLength();
			if (distanceSquared > maxDistanceSquared)
			{
				continue;
			}

			scored.emplace_back(distanceSquared, index);
		}

		std::sort(scored.begin(), scored.end());

		const std::size_t take = std::min(maxResults, scored.size());
		results.reserve(take);
		for (std::size_t i = 0; i < take; ++i)
		{
			results.push_back(scored[i].second);
		}

		return results;
	}
}
