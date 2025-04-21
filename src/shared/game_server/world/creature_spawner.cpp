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
#include "shared/proto_data/units.pb.h"

namespace mmo
{
	CreatureSpawner::CreatureSpawner(
		WorldInstance& world,
		const proto::UnitEntry& entry,
		const proto::UnitSpawnEntry& spawnEntry)
		: m_world(world)
		, m_entry(entry)
		, m_spawnEntry(spawnEntry)
		, m_active(spawnEntry.isactive())
		, m_respawn(spawnEntry.respawn())
		, m_currentlySpawned(0)
		, m_respawnCountdown(world.GetUniverse().GetTimers())
		, m_location(spawnEntry.positionx(), spawnEntry.positiony(), spawnEntry.positionz())
	{
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

	void CreatureSpawner::SpawnOne()
	{
		// TODO: Generate random point and if needed, random rotation
		const Vector3 location(m_spawnEntry.positionx(), m_spawnEntry.positiony(), m_spawnEntry.positionz());
		const float o = m_spawnEntry.rotation();

		// Spawn a new creature
		auto spawned = m_world.CreateCreature(m_entry, location, o, m_spawnEntry.radius());
		spawned->ClearFieldChanges();

		CreatureMovement movement = creature_movement::None;
		if (m_spawnEntry.movement() >= creature_movement::Invalid)
		{
			WLOG("Invalid movement type for creature spawn - spawn ignored");
		}
		else
		{
			movement = static_cast<CreatureMovement>(m_spawnEntry.movement());
		}
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
		if (m_active != active)
		{
			if (active && !m_currentlySpawned)
			{
				for (size_t i = 0; i < m_spawnEntry.maxcount(); ++i)
				{
					SpawnOne();
				}
			}
			else
			{
				m_respawnCountdown.Cancel();
			}

			m_active = active;
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
