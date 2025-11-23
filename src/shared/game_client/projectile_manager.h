// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/signal.h"
#include "math/vector3.h"
#include "math/quaternion.h"
#include "math/radian.h"
#include "audio/audio.h"

#include <vector>
#include <memory>

namespace mmo
{
    class Scene;
    class SceneNode;
    class Entity;
    class ParticleEmitter;
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
        Projectile(Scene &scene,
                   IAudio *audio,
                   const proto_client::SpellEntry &spell,
                   const proto_client::SpellVisualization *visualization,
                   const Vector3 &startPosition,
                   std::weak_ptr<GameUnitC> target);

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

        /// @brief Get the target unit if still valid
        std::shared_ptr<GameUnitC> GetTarget() const
        {
            return m_target.lock();
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
        const proto_client::SpellVisualization *m_visualization;
        std::weak_ptr<GameUnitC> m_target;

        // Scene objects
        SceneNode *m_node;
        Entity *m_entity;
        ParticleEmitter *m_trailEmitter;

        // Movement state
        Vector3 m_startPosition;
        Vector3 m_velocity;
        float m_travelTime;
        float m_totalDistance;
        bool m_hasHit;

        // Audio
        ChannelIndex m_soundChannel;
    };

    /// @brief Manages all active projectiles in the game
    class ProjectileManager
    {
    public:
        explicit ProjectileManager(Scene &scene, IAudio *audio);
        ~ProjectileManager() = default;

    public:
        /// @brief Spawn a new projectile
        /// @param spell Spell entry containing speed and data
        /// @param visualization Spell visualization containing projectile config
        /// @param caster Unit casting the spell
        /// @param target Target unit
        void SpawnProjectile(const proto_client::SpellEntry &spell,
                             const proto_client::SpellVisualization *visualization,
                             GameUnitC *caster,
                             GameUnitC *target);

        /// @brief Update all active projectiles
        /// @param deltaTime Time elapsed since last update
        void Update(float deltaTime);

        /// @brief Clear all projectiles
        void Clear();

    public:
        /// @brief Signal emitted when a projectile hits its target
        /// @param spell The spell entry
        /// @param target The target that was hit (may be null if target died)
        signal<void(const proto_client::SpellEntry &, GameUnitC *)> projectileImpact;

    private:
        Scene &m_scene;
        IAudio *m_audio;
        std::vector<std::unique_ptr<Projectile>> m_projectiles;
    };
}
