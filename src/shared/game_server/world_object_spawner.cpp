
#include "world_object_spawner.h"
#include "world_instance.h"
#include "world_instance_manager.h"
#include "game_world_object_s.h"
#include "base/erase_by_move.h"
#include "log/default_log_levels.h"
#include "base/utilities.h"
#include "shared/proto_data/objects.pb.h"
#include "universe.h"

namespace mmo
{
	WorldObjectSpawner::WorldObjectSpawner(
		WorldInstance& world,
		const proto::ObjectEntry& entry,
		size_t maxCount,
		GameTime respawnDelay,
		const Vector3& center,
		const Quaternion& rotation,
		float radius,
		uint32 animProgress,
		uint32 state)
		: m_world(world)
		, m_entry(entry)
		, m_maxCount(maxCount)
		, m_respawnDelay(respawnDelay)
		, m_center(center)
		, m_rotation(rotation)
		, m_radius(radius)
		, m_currentlySpawned(0)
		, m_respawnCountdown(world.GetUniverse().GetTimers())
		, m_animProgress(animProgress)
		, m_state(state)
	{
		// Immediatly spawn all objects
		for (size_t i = 0; i < m_maxCount; ++i)
		{
			SpawnOne();
		}

		m_respawnCountdown.ended.connect(std::bind(&WorldObjectSpawner::OnSpawnTime, this));
	}

	WorldObjectSpawner::~WorldObjectSpawner()
	{
	}

	void WorldObjectSpawner::SpawnOne()
	{
		ASSERT(m_currentlySpawned < m_maxCount);

		// TODO: Generate random point and if needed, random rotation
		const Vector3 position(m_center);

		// Spawn a new creature
		auto spawned = m_world.SpawnWorldObject(m_entry, position);
		spawned->Set<float>(object_fields::Scale, m_entry.scale());
		spawned->Set<float>(object_fields::RotationW, m_rotation.w);
		spawned->Set<float>(object_fields::RotationX, m_rotation.x);
		spawned->Set<float>(object_fields::RotationY, m_rotation.y);
		spawned->Set<float>(object_fields::RotationZ, m_rotation.z);
		spawned->Set<uint32>(object_fields::AnimProgress, m_animProgress);
		spawned->Set<uint32>(object_fields::State, m_state);

		// watch for destruction
		spawned->destroy = std::bind(&WorldObjectSpawner::OnRemoval, this, std::placeholders::_1);
		m_world.AddGameObject(*spawned);

		// Remember that creature
		m_objects.push_back(std::move(spawned));
		++m_currentlySpawned;
	}

	void WorldObjectSpawner::OnSpawnTime()
	{
		SpawnOne();
		SetRespawnTimer();
	}

	void WorldObjectSpawner::OnRemoval(GameObjectS& removed)
	{
		--m_currentlySpawned;

		const auto i = std::find_if(
			std::begin(m_objects),
			std::end(m_objects),
			[&removed](const std::shared_ptr<GameWorldObjectS>& element)
			{
				return (element.get() == &removed);
			});

		ASSERT(i != m_objects.end());
		EraseByMove(m_objects, i);

		SetRespawnTimer();
	}

	void WorldObjectSpawner::SetRespawnTimer()
	{
		if (m_currentlySpawned >= m_maxCount)
		{
			return;
		}

		m_respawnCountdown.SetEnd(
			GetAsyncTimeMs() + m_respawnDelay);
	}
}
