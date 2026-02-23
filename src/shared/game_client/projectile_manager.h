// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/signal.h"
#include "math/vector3.h"
#include "math/quaternion.h"
#include "math/radian.h"
#include "audio/audio.h"
#include "game_common/projectile_target.h"

#include <vector>
#include <memory>

namespace mmo
{
    class Scene;
    class SceneNode;
    class Entity;
    class ParticleEmitter;
    class Light;
    class RibbonTrail;
    class GameUnitC;
    class IAudio;

    namespace proto_client
    {
        class SpellEntry;
        class SpellVisualization;
        class ProjectileVisual;
    }

    /// @brief Individual projectile instance
    class Projectile
    {
    public:
        /// @brief Constructor for projectiles with IProjectileTarget interface.
        /// @param scene The scene to create visual objects in.
        /// @param audio Audio system for sound playback (may be null).
        /// @param spell The spell entry containing speed and other data.
        /// @param projectileVisual The projectile visual configuration (may be null).
        /// @param startPosition The starting position of the projectile.
        /// @param target The target to track (uses IProjectileTarget interface).
        /// @param animationDelay Seconds the projectile spawn was delayed by animation. Used to boost speed.
        Projectile(Scene &scene,
                   IAudio *audio,
                   const proto_client::SpellEntry &spell,
                   const proto_client::ProjectileVisual *projectileVisual,
                   const Vector3 &startPosition,
                   std::shared_ptr<IProjectileTarget> target,
                   float animationDelay = 0.0f);

        ~Projectile();

    public:
        /// @brief Update projectile position and check for impact
        /// @param deltaTime Time elapsed since last update
        /// @return True if projectile has hit target
        bool Update(float deltaTime);

        /// @brief Get the spell driving this projectile
        const proto_client::SpellEntry &GetSpell() const
        {
            return m_spell;
        }

        /// @brief Get the target if still valid
        std::shared_ptr<IProjectileTarget> GetTarget() const
        {
            return m_target;
        }

        /// @brief Get current projectile position
        const Vector3 &GetPosition() const;

    private:
        void UpdateLinearMotion(float deltaTime);
        void UpdateArcMotion(float deltaTime);
        void UpdateHomingMotion(float deltaTime);
        void UpdateSineWaveMotion(float deltaTime);
        void UpdateRotation(float deltaTime);
        void CleanupVisuals();

    private:
        Scene &m_scene;
        IAudio *m_audio;
        const proto_client::SpellEntry &m_spell;
        const proto_client::ProjectileVisual *m_projectileVisual;
        std::shared_ptr<IProjectileTarget> m_target;

        // Scene objects
        SceneNode *m_node;
        Entity *m_entity;
        ParticleEmitter *m_trailEmitter;
        Light *m_light;
        RibbonTrail *m_ribbonTrail;

        // Light fade state
        float m_lightTargetIntensity;
        float m_lightFadeInTime;

        // Movement state
        Vector3 m_startPosition;
        Vector3 m_velocity;
        float m_travelTime;
        float m_totalDistance;
        float m_speedMultiplier;
        bool m_hasHit;

        // Audio
        ChannelIndex m_soundChannel;
    };

    /// @brief Manages all active projectiles in the game
    class ProjectileManager
    {
    public:
        explicit ProjectileManager(Scene &scene, IAudio *audio);
        ~ProjectileManager();

    public:
        /// @brief Spawn a new projectile from GameUnitC caster/target.
        /// @param spell Spell entry containing speed and data.
        /// @param visualization Spell visualization containing projectile config.
        /// @param caster Unit casting the spell.
        /// @param target Target unit.
        /// @param animationDelay Seconds the spawn was delayed by animation.
        void SpawnProjectile(const proto_client::SpellEntry &spell,
                             const proto_client::SpellVisualization *visualization,
                             GameUnitC *caster,
                             GameUnitC *target,
                             float animationDelay = 0.0f);

        /// @brief Spawn a new projectile using abstract targets.
        /// @param spell Spell entry containing speed and data.
        /// @param visualization Spell visualization containing projectile config.
        /// @param startPosition The starting position.
        /// @param target Abstract target implementing IProjectileTarget.
        void SpawnProjectile(const proto_client::SpellEntry &spell,
                             const proto_client::SpellVisualization *visualization,
                             const Vector3 &startPosition,
                             std::shared_ptr<IProjectileTarget> target);

        /// @brief Spawn a projectile using explicit parameters instead of proto types.
        /// This overload is useful when proto types are not available (e.g., in editor).
        /// @param params The projectile configuration parameters.
        /// @param startPosition The starting position.
        /// @param target Abstract target implementing IProjectileTarget.
        void SpawnProjectileEx(const ProjectileParams& params,
                               const Vector3 &startPosition,
                               std::shared_ptr<IProjectileTarget> target);

        /// @brief Update all active projectiles
        /// @param deltaTime Time elapsed since last update
        void Update(float deltaTime);

        /// @brief Clear all projectiles
        void Clear();

    public:
        /// @brief Signal emitted when a projectile hits its target
        /// @param spell The spell entry
        /// @param target The target that was hit (may be null if target died)
        signal<void(const proto_client::SpellEntry &, IProjectileTarget *)> projectileImpact;

        /// @brief Simple signal emitted when any projectile hits its target.
        /// This version has no proto dependencies and can be used by the editor.
        /// @param target The target that was hit (may be null if target died).
        signal<void(IProjectileTarget *)> projectileImpactSimple;

    private:
        Scene &m_scene;
        IAudio *m_audio;
        std::vector<std::unique_ptr<Projectile>> m_projectiles;
        
        // Internal storage for SpawnProjectileEx - implementation details hidden in cpp
        struct TempProtoStorage;
        std::unique_ptr<TempProtoStorage> m_tempStorage;
    };
}
