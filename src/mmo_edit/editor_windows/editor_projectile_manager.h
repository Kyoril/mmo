// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "audio/audio.h"
#include "base/typedefs.h"
#include "base/signal.h"
#include "math/vector3.h"
#include "game_common/projectile_target.h"

#include <vector>
#include <memory>

namespace mmo
{
	class Scene;
	class SceneNode;
	class Entity;
	class ParticleSystem;
	class Light;
	class RibbonTrail;
	class IAudio;

	/// @brief Simple projectile for editor preview.
	/// 
	/// This is a standalone implementation that doesn't depend on proto types,
	/// allowing it to be used in the editor without the header guard conflicts
	/// that arise from using both proto and proto_client.
	class EditorProjectile
	{
	public:
		/// @brief Constructor.
		/// @param scene The scene for visual objects.
		/// @param audio Audio system (may be null).
		/// @param params Projectile configuration.
		/// @param startPosition Starting position.
		/// @param target Target to track.
		EditorProjectile(Scene& scene,
		                 IAudio* audio,
		                 const ProjectileParams& params,
		                 const Vector3& startPosition,
		                 std::shared_ptr<IProjectileTarget> target);

		/// @brief Destructor.
		~EditorProjectile();

		/// @brief Update the projectile.
		/// @param deltaTime Time since last update.
		/// @return True if projectile has hit target.
		bool Update(float deltaTime);

		/// @brief Get the target.
		std::shared_ptr<IProjectileTarget> GetTarget() const
		{
			return m_target;
		}

		/// @brief Get current position.
		const Vector3& GetPosition() const;

	private:
		void UpdateLinearMotion(float deltaTime);
		void UpdateArcMotion(float deltaTime);
		void UpdateHomingMotion(float deltaTime);
		void UpdateSineWaveMotion(float deltaTime);
		void UpdateRotation(float deltaTime);
		void CleanupVisuals();

	private:
		Scene& m_scene;
		IAudio* m_audio;
		ProjectileParams m_params;
		std::shared_ptr<IProjectileTarget> m_target;

		SceneNode* m_node;
		Entity* m_entity;
		ParticleSystem* m_trailEmitter;
		Light* m_light;
		RibbonTrail* m_ribbonTrail;

		Vector3 m_startPosition;
		Vector3 m_velocity;
		float m_travelTime;
		float m_totalDistance;
		bool m_hasHit;

		ChannelIndex m_soundChannel;
	};

	/// @brief Simple projectile manager for editor preview.
	/// 
	/// This is a standalone implementation without proto dependencies.
	class EditorProjectileManager
	{
	public:
		/// @brief Constructor.
		/// @param scene The scene for visuals.
		/// @param audio Audio system (may be null).
		EditorProjectileManager(Scene& scene, IAudio* audio);

		/// @brief Destructor.
		~EditorProjectileManager() = default;

		/// @brief Spawn a projectile.
		/// @param params Projectile configuration.
		/// @param startPosition Starting position.
		/// @param target Target to track.
		void SpawnProjectile(const ProjectileParams& params,
		                     const Vector3& startPosition,
		                     std::shared_ptr<IProjectileTarget> target);

		/// @brief Update all projectiles.
		/// @param deltaTime Time since last update.
		void Update(float deltaTime);

		/// @brief Clear all projectiles.
		void Clear();

	public:
		/// @brief Signal when a projectile hits its target.
		signal<void(IProjectileTarget*)> projectileImpact;

	private:
		Scene& m_scene;
		IAudio* m_audio;
		std::vector<std::unique_ptr<EditorProjectile>> m_projectiles;
	};
}
