// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/countdown.h"
#include "base/random.h"
#include "math/vector3.h"

namespace mmo
{
	class WorldInstance;
	class GameObjectS;
	class GameUnitS;
	class GameCreatureS;

	namespace proto
	{
		class Project;
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
		/// @param project Game data, used to resolve the entries of a mixed spawn (UnitSpawnEntry::unitentries).
		/// @param entry Base data of the creatures to be spawned, and the fallback of a mixed spawn.
		/// @param maxCount Maximum number of creatures to spawn.
		/// @param respawnDelay The delay in milliseconds until a creature of this spawn point will respawn.
		/// @param centerX The x coordinate of the spawn center.
		/// @param centerY The y coordinate of the spawn center.
		/// @param centerZ The z coordinate of the spawn center.
		/// @param rotation The rotation of the creatures. If nothing provided, will be randomized.
		/// @param radius The radius in which creatures will spawn. Also used as the maximum random walk distance.
		explicit CreatureSpawner(
			WorldInstance& world,
			const proto::Project& project,
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

		/// Rolls the unit entry id a spawn point spawns next. Without an entry of weight > 0 in
		/// unitentries this is always unitentry; otherwise one of the weighted entries, chosen
		/// with a chance of weight / total weight. Weight 0 entries are never chosen.
		/// @param spawnEntry The spawn point.
		/// @param generator Random generator used for the roll.
		/// @return The unit entry id to spawn.
		static uint32 RollUnitEntryId(const proto::UnitSpawnEntry& spawnEntry, RandomnessGenerator& generator);

	private:
		/// Picks the unit entry for the next spawn of this spawn point.
		const proto::UnitEntry& NextEntry() const;

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
		const proto::Project& m_project;
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
