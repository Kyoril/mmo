// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "creature_spawner.h"
#include "world_instance.h"
#include "world_instance_manager.h"
#include "objects/game_unit_s.h"
#include "objects/game_creature_s.h"
#include "universe.h"
#include "base/erase_by_move.h"
#include "log/default_log_levels.h"
#include "base/utilities.h"
#include "shared/proto_data/maps.pb.h"
#include "shared/proto_data/project.h"

namespace mmo
{
	namespace
	{
		CreatureMovement ConvertSpawnMovement(const proto::UnitSpawnEntry& spawnEntry)
		{
			switch (spawnEntry.movement())
			{
			case proto::UnitSpawnEntry_MovementType_STATIONARY:
				return creature_movement::None;
			case proto::UnitSpawnEntry_MovementType_RANDOM:
				return creature_movement::Random;
			case proto::UnitSpawnEntry_MovementType_PATROL:
				return creature_movement::Patrol;
			default:
				WLOG("Invalid movement type for creature spawn - falling back to stationary");
				return creature_movement::None;
			}
		}

		std::vector<GameCreatureS::PatrolWaypoint> BuildPatrolWaypoints(const proto::UnitSpawnEntry& spawnEntry)
		{
			std::vector<GameCreatureS::PatrolWaypoint> patrolWaypoints;
			patrolWaypoints.reserve(spawnEntry.waypoints_size());

			for (const auto& waypoint : spawnEntry.waypoints())
			{
				patrolWaypoints.push_back({
					Vector3(waypoint.positionx(), waypoint.positiony(), waypoint.positionz()),
					waypoint.waittime()
				});
			}

			return patrolWaypoints;
		}
	}

	CreatureSpawner::CreatureSpawner(
		WorldInstance& world,
		const proto::Project& project,
		const proto::UnitEntry& entry,
		const proto::UnitSpawnEntry& spawnEntry)
		: m_world(world)
		, m_project(project)
		, m_entry(entry)
		, m_spawnEntry(spawnEntry)
		, m_active(spawnEntry.isactive())
		, m_respawn(spawnEntry.respawn())
		, m_currentlySpawned(0)
		, m_respawnCountdown(world.GetUniverse().GetTimers())
		, m_location(spawnEntry.positionx(), spawnEntry.positiony(), spawnEntry.positionz())
	{
		for (const auto& candidate : m_spawnEntry.unitentries())
		{
			if (!m_project.units.getById(candidate.unitentry()))
			{
				WLOG("Unit spawn '" << m_spawnEntry.name() << "' lists unknown unit entry " << candidate.unitentry() << " - it falls back to unit entry " << m_entry.id() << " when rolled");
			}
		}

		if (m_active)
		{
			for (size_t i = 0; i < m_spawnEntry.maxcount(); ++i)
			{
				SpawnOne();
			}
		}

		m_respawnCountdown.ended.connect(*this, &CreatureSpawner::OnSpawnTime);
	}

	CreatureSpawner::~CreatureSpawner()
	= default;

	uint32 CreatureSpawner::RollUnitEntryId(const proto::UnitSpawnEntry& spawnEntry, RandomnessGenerator& generator)
	{
		uint64 totalWeight = 0;
		for (const auto& candidate : spawnEntry.unitentries())
		{
			totalWeight += candidate.weight();
		}

		if (totalWeight == 0)
		{
			return spawnEntry.unitentry();
		}

		std::uniform_int_distribution<uint64> distribution(0, totalWeight - 1);
		uint64 roll = distribution(generator);
		for (const auto& candidate : spawnEntry.unitentries())
		{
			if (roll < candidate.weight())
			{
				return candidate.unitentry();
			}

			roll -= candidate.weight();
		}

		UNREACHABLE();
		return spawnEntry.unitentry();
	}

	const proto::UnitEntry& CreatureSpawner::NextEntry() const
	{
		if (m_spawnEntry.unitentries_size() == 0)
		{
			return m_entry;
		}

		const proto::UnitEntry* rolled = m_project.units.getById(RollUnitEntryId(m_spawnEntry, RandomGenerator));
		return rolled ? *rolled : m_entry;
	}

	void CreatureSpawner::SpawnOne()
	{
		// TODO: Generate random point and if needed, random rotation
		const Vector3 location(m_spawnEntry.positionx(), m_spawnEntry.positiony(), m_spawnEntry.positionz());
		const float o = m_spawnEntry.rotation();

		// Spawn a new creature
		auto spawned = m_world.CreateCreature(NextEntry(), location, o, m_spawnEntry.radius());
		spawned->ClearFieldChanges();

		const CreatureMovement movement = ConvertSpawnMovement(m_spawnEntry);
		spawned->SetPatrolWaypoints(BuildPatrolWaypoints(m_spawnEntry));
		spawned->SetMovementType(movement);
		spawned->SetHealthPercent(m_spawnEntry.health_percent());

		if (m_spawnEntry.standstate() < unit_stand_state::Count_)
		{
			spawned->SetStandState(static_cast<unit_stand_state::Type>(m_spawnEntry.standstate()));
		}
		else
		{
			ELOG("Unit spawn has invalid stand state value " << m_spawnEntry.standstate() << " - value is ignored");
		}

		if (m_spawnEntry.additional_trigger_ids_size() > 0)
		{
			std::vector<uint32> additionalTriggers(
				m_spawnEntry.additional_trigger_ids().begin(),
				m_spawnEntry.additional_trigger_ids().end());
			spawned->SetSpawnAdditionalTriggers(std::move(additionalTriggers));
		}

		// watch for destruction
		spawned->destroy = [this]<typename TUnit>(TUnit&& destroyedUnit) { OnRemoval(std::forward<TUnit>(destroyedUnit)); };
		m_world.AddGameObject(*spawned);

		// Creates are bound to their spawn point
		spawned->SetBinding(m_world.GetMapId(), location, Radian(o));

		// Remember that creature
		m_creatures.emplace_back(spawned);
		++m_currentlySpawned;
	}

	void CreatureSpawner::OnSpawnTime()
	{
		SpawnOne();
		SetRespawnTimer();
	}

	void CreatureSpawner::OnRemoval(GameObjectS& removed)
	{
		--m_currentlySpawned;

		const auto i = std::find_if(
			std::begin(m_creatures),
			std::end(m_creatures),
			[&removed](const std::shared_ptr<GameUnitS>& element)
			{
				return (element.get() == &removed);
			});

		ASSERT(i != m_creatures.end());
		EraseByMove(m_creatures, i);

		SetRespawnTimer();
	}

	void CreatureSpawner::SetRespawnTimer()
	{
		if (!m_respawn)
		{
			return;
		}

		if (m_currentlySpawned >= m_spawnEntry.maxcount())
		{
			return;
		}

		m_respawnCountdown.SetEnd(
			GetAsyncTimeMs() + m_spawnEntry.respawndelay());
	}

	const Vector3& CreatureSpawner::RandomPoint()
	{
		/*
		auto* mapData = m_world.GetMapData();
		if (mapData)
		{
			if (mapData->getRandomPointOnGround(m_location, 5.0f, m_randomPoint))
			{
				return m_randomPoint;
			}
		}
		*/

		return m_location;
	}

	void CreatureSpawner::SetState(bool active)
	{
		m_active = active;

		if (active && !m_currentlySpawned)
		{
			m_respawnCountdown.Cancel();
			for (size_t i = 0; i < m_spawnEntry.maxcount(); ++i)
			{
				SpawnOne();
			}
		}
		else if (!active)
		{
			m_respawnCountdown.Cancel();
		}
	}

	void CreatureSpawner::SetRespawn(bool enabled)
	{
		if (m_respawn != enabled)
		{
			if (!enabled)
			{
				m_respawnCountdown.Cancel();
			}
			else
			{
				SetRespawnTimer();
			}

			m_respawn = enabled;
		}
	}
}
