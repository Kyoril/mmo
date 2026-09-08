// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <vector>
#include <map>
#include <cstdint>
#include <string>
#include <functional>

#include "base/typedefs.h"
#include "base/signal.h"
#include "shared/audio/audio.h"
#include "shared/client_data/proto_client/spells.pb.h"
#include "shared/client_data/proto_client/spell_visualizations.pb.h"
#include "shared/client_data/project.h"

namespace mmo
{
    class GameUnitC;
    class AnimationState;
    class ParticleSystem;
    class Light;
    class RibbonTrail;
    class SceneNode;
    class SoundEntryPlayer;

    namespace proto_client
    {
        class Project;
    }

    /// \brief Client-side service to apply data-driven spell visualizations.
    ///
    /// This service resolves a spell's visualization_id to a SpellVisualization entry
    /// and applies defined kits (sounds, animations, tints) on lifecycle events.
    class SpellVisualizationService
    {
    public:
        /// \brief Access the global service instance.
        /// \return Reference to the singleton instance.
        static SpellVisualizationService& Get();

        /// \brief Spell visualization events.
        enum class Event : uint32
        {
            StartCast = 0,
            CancelCast = 1,
            Casting = 2,
            CastSucceeded = 3,
            Impact = 4,
            AuraApplied = 5,
            AuraRemoved = 6,
            AuraTick = 7,
            AuraIdle = 8
        };

    public:
        /// \brief Apply visualization kits for a spell event.
        /// \param event The lifecycle event.
        /// \param spell The spell entry (client proto).
        /// \param caster The caster unit (may be null).
        /// \param targets Optional targets for TARGET scope kits (may be empty).
        void Apply(Event event, const proto_client::SpellEntry& spell, GameUnitC* caster, const std::vector<GameUnitC*>& targets);

        /// \brief Apply visualization kits for an event of a visualization referenced directly by id.
        ///
        /// Used for visuals that belong to no spell -- level up, quest completion and anything else
        /// the server plays through the PlaySpellVisual packet. Prefer the Impact event for these:
        /// the cast and aura events expect a matching lifecycle event to tear their effects down,
        /// and nothing raises those for a visual played this way.
        /// \param event The lifecycle event.
        /// \param visualizationId Id of the SpellVisualization entry to play.
        /// \param actor The unit the visualization plays on (CASTER-scoped kits).
        /// \param targets Optional targets for TARGET scope kits (may be empty).
        void ApplyById(Event event, uint32 visualizationId, GameUnitC* actor, const std::vector<GameUnitC*>& targets);

        /// \brief Initializes the visualization service with a project reference, audio player
        ///        and sound entry player.
        /// \param project The loaded client project containing the spell visualization dataset.
        /// \param audioPlayer Audio player interface for sound playback (optional, may be null).
        /// \param soundEntryPlayer Resolves kit sound_ids against the sounds.data catalog. May
        ///        be nullptr, in which case kits using sound_ids stay silent.
        void Initialize(const proto_client::Project& project, IAudio* audioPlayer,
                        SoundEntryPlayer* soundEntryPlayer);

        /// \brief Stop any looped sound currently playing for an actor (e.g., on cancel/success/death).
        void StopLoopedSoundForActor(uint64 actorGuid);

        /// \brief Fade out any looped sound currently playing for an actor (smooth transition).
        void FadeOutLoopedSoundForActor(uint64 actorGuid);

        /// \brief Update sound fading (call each frame).
        /// \param deltaTime Time since last update.
        void Update(float deltaTime);

        /// \brief Remove tint from an actor for a specific spell (public for aura removal).
        void RemoveTintFromActor(GameUnitC& actor, uint32 spellId);

        /// \brief Remove all active spell effects (particles, lights, ribbons) for a given actor and spell.
        void CleanupEffectsForActor(uint64 actorGuid, uint32 spellId);

    private:
        SpellVisualizationService() = default;
        ~SpellVisualizationService() = default;

        SpellVisualizationService(const SpellVisualizationService&) = delete;
        SpellVisualizationService& operator=(const SpellVisualizationService&) = delete;

    private:
        /// \brief Shared core of Apply and ApplyById.
        /// \param spellId Key the resulting effects, tints and animations are tracked under. Real
        ///        spell id for Apply, SyntheticSpellId() for ApplyById.
        void ApplyVisualization(Event event, const proto_client::SpellVisualization& visualization,
                                uint32 spellId, GameUnitC* caster, const std::vector<GameUnitC*>& targets);

        /// \brief Tracking key for a visualization with no spell behind it. The high bit is set so
        ///        it can never collide with a real spell id.
        static uint32 SyntheticSpellId(uint32 visualizationId);

        /// \brief Whether a tracking key came from SyntheticSpellId, i.e. the effect belongs to a
        ///        visualization played by id and will never receive a lifecycle event.
        static bool IsSyntheticSpellId(uint32 spellId);

        void ApplyKitToActor(const proto_client::SpellVisualization& vis,
                             const proto_client::SpellKit& kit,
                             GameUnitC& actor,
                             uint32 spellId,
                             bool instantEvent = false);

        void ApplyAnimationToActor(const proto_client::SpellKit& kit, GameUnitC& actor, uint32 spellId);

        void ApplyTintToActor(const proto_client::SpellKit& kit, GameUnitC& actor, uint32 spellId);

        /// \brief Spawn particle emitters defined in a kit, optionally attached to a bone.
        void ApplyParticlesToActor(const proto_client::SpellKit& kit, GameUnitC& actor, uint32 spellId);

        /// \brief Spawn a point light defined in a kit, optionally attached to a bone.
        void ApplyLightToActor(const proto_client::SpellKit& kit, GameUnitC& actor, uint32 spellId, bool instantEvent = false);

        /// \brief Spawn a ribbon trail defined in a kit, optionally attached to a bone.
        void ApplyRibbonTrailToActor(const proto_client::SpellKit& kit, GameUnitC& actor, uint32 spellId);

        static uint32 ToProtoEventValue(Event e);

    private:
        /// \brief Structure to track a looped sound handle per event/actor.
        struct LoopedSoundHandle
        {
            ChannelIndex audioHandle;
            uint32 spellId;
            Event event;
            float currentVolume;
            float targetVolume;
            float fadeSpeed;
            
            LoopedSoundHandle() 
                : audioHandle(InvalidChannel)
                , spellId(0)
                , event(Event::StartCast)
                , currentVolume(0.0f)
                , targetVolume(1.0f)
                , fadeSpeed(3.0f)
            {
            }
        };

        /// \brief Structure to track a one-shot sound with fading.
        struct FadingSound
        {
            ChannelIndex channel;
            float currentVolume;
            float targetVolume;
            float fadeSpeed;
            bool markedForRemoval;
            
            FadingSound()
                : channel(InvalidChannel)
                , currentVolume(0.0f)
                , targetVolume(1.0f)
                , fadeSpeed(3.0f)
                , markedForRemoval(false)
            {
            }
        };

        /// \brief Structure to track active spell animations on an actor.
        struct ActiveSpellAnimation
        {
            uint32 spellId;
            AnimationState* animState;
            
            ActiveSpellAnimation()
                : spellId(0)
                , animState(nullptr)
            {
            }
        };

        /// \brief Pointer to the loaded client project for dataset lookups. Set via Initialize.
        const proto_client::Project* m_project { nullptr };
        IAudio* m_audioPlayer { nullptr }; // not owned
        SoundEntryPlayer* m_soundEntryPlayer { nullptr }; // not owned

        /// \brief Map actor guid -> looped sound handle for proper cleanup on cancel/success/aura removal.
        mutable std::map<uint64, LoopedSoundHandle> m_loopedSounds;
        
        /// \brief One-shot sounds with fading.
        mutable std::vector<FadingSound> m_fadingSounds;
        
        /// \brief Map actor guid -> active spell animation for cancellation on same-spell events.
        mutable std::map<uint64, ActiveSpellAnimation> m_activeSpellAnimations;

        /// \brief Structure to track active visual effects (particles, lights, ribbon trails) per actor.
        struct ActiveSpellEffect
        {
            uint32 spellId{ 0 };
            uint64 actorGuid{ 0 };
            std::vector<ParticleSystem*> particles;
            std::vector<Light*> lights;
            std::vector<RibbonTrail*> ribbonTrails;
            std::vector<SceneNode*> effectNodes;
        };

        /// \brief All active spell effects across all actors.
        mutable std::vector<ActiveSpellEffect> m_activeEffects;

        /// \brief Tracks a light that is fading in or out.
        struct FadingLight
        {
            Light* light{ nullptr };
            uint64 actorGuid{ 0 };
            float currentIntensity{ 0.0f };
            float targetIntensity{ 0.0f };
            float fadeInSpeed{ 0.0f };
            float fadeOutSpeed{ 0.0f };
            bool fadingOut{ false };
            bool autoFadeOut{ false };
        };

        /// \brief Active lights with fade state.
        mutable std::vector<FadingLight> m_fadingLights;

        /// \brief A kit scheduled to fire after its delay_ms elapsed.
        ///
        /// The actor is stored by guid and re-resolved when the kit fires so a
        /// despawned actor simply drops the pending kit instead of crashing.
        struct PendingKit
        {
            proto_client::SpellKit kit;
            uint64 actorGuid{ 0 };
            uint32 spellId{ 0 };
            uint32 visualizationId{ 0 };
            bool instantEvent{ false };
            float remainingSeconds{ 0.0f };
        };

        /// \brief Kits waiting for their delay to elapse; drained in Update().
        mutable std::vector<PendingKit> m_pendingKits;

        /// \brief Counter for generating unique effect names.
        mutable uint32 m_effectCounter{ 0 };
    };

    // Free functions for aura visualization notifications (avoid circular includes)
    void NotifyAuraVisualizationApplied(const proto_client::SpellEntry& spell, GameUnitC* caster, GameUnitC* target);
    void NotifyAuraVisualizationRemoved(const proto_client::SpellEntry& spell, GameUnitC* caster, GameUnitC* target);
}
