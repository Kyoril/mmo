// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <array>

#include "base/typedefs.h"
#include "base/countdown.h"
#include "math/quaternion.h"
#include "math/vector3.h"

namespace mmo
{
	class WorldInstance;
	class GameObjectS;
	class GameUnitS;
	class GameWorldObjectS;

	namespace proto
	{
		class ObjectEntry;
	}

	///
	class WorldObjectSpawner final
	{
		typedef std::vector<std::shared_ptr<GameWorldObjectS>> OwnedObjects;

	public:
		///
		explicit WorldObjectSpawner(
			WorldInstance& world,
			const proto::ObjectEntry& entry,
			size_t maxCount,
			GameTime respawnDelay,
			const Vector3& center,
			const Quaternion& rotation,
			float radius,
			uint32 animProgress,
			uint32 state);
		virtual ~WorldObjectSpawner();

		///
		const OwnedObjects& getSpawnedObjects() const
		{
			return m_objects;
		}

	private:

		/// Spawns one more creature and adds it to the world.
		void SpawnOne();

		/// Callback which is fired after a respawn delay invalidated.
		void OnSpawnTime();

		/// Callback which is fired after a creature despawned.
		void OnRemoval(GameObjectS& removed);

		/// Setup a new respawn timer.
		void SetRespawnTimer();


	private:

		WorldInstance& m_world;
		const proto::ObjectEntry& m_entry;
		const size_t m_maxCount;
		const GameTime m_respawnDelay;
		const Vector3 m_center;
		const Quaternion m_rotation;
		const float m_radius;
		size_t m_currentlySpawned;
		OwnedObjects m_objects;
		Countdown m_respawnCountdown;
		uint32 m_animProgress;
		uint32 m_state;
	};
}
