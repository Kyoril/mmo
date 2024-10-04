
#pragma once

#include "base/typedefs.h"
#include "base/countdown.h"
#include "math/vector3.h"

namespace mmo
{
	class WorldInstance;
	class GameObjectS;
	class GameUnitS;
	class GameCreatureS;

	namespace proto
	{
		class UnitEntry;
		class UnitSpawnEntry;
	}

	/// Manages a spawn point and all creatures which are spawned by this point info.
	/// It also adds spawned creatures to the given world instance.
	class CreatureSpawner final
	{
		typedef std::vector<std::shared_ptr<GameCreatureS>> OwnedCreatures;

	public:
		/// Creates a new instance of the CreatureSpawner class and initializes it.
		/// @param world The world instance this spawner belongs to. Creatures will be spawned in this world.
		/// @param entry Base data of the creatures to be spawned.
		/// @param maxCount Maximum number of creatures to spawn.
		/// @param respawnDelay The delay in milliseconds until a creature of this spawn point will respawn.
		/// @param centerX The x coordinate of the spawn center.
		/// @param centerY The y coordinate of the spawn center.
		/// @param centerZ The z coordinate of the spawn center.
		/// @param rotation The rotation of the creatures. If nothing provided, will be randomized.
		/// @param radius The radius in which creatures will spawn. Also used as the maximum random walk distance.
		explicit CreatureSpawner(
			WorldInstance& world,
			const proto::UnitEntry& entry,
			const proto::UnitSpawnEntry& spawnEntry);
		virtual ~CreatureSpawner();

		///
		const OwnedCreatures& GetCreatures() const { return m_creatures; }

		///
		bool IsActive() const { return m_active; }

		///
		bool IsRespawnEnabled() const { return m_respawn; }

		///
		void SetState(bool active);
		///
		void SetRespawn(bool enabled);

		/// Gets a random movement point in the spawn radius.
		const Vector3& RandomPoint();

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
		const proto::UnitEntry& m_entry;
		const proto::UnitSpawnEntry& m_spawnEntry;
		bool m_active;
		bool m_respawn;
		size_t m_currentlySpawned;
		OwnedCreatures m_creatures;
		Countdown m_respawnCountdown;
		Vector3 m_location;
		Vector3 m_randomPoint;
	};
}
