// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <vector>
#include <map>
#include <cstdint>
#include <string>

#include "base/typedefs.h"
#include "shared/client_data/proto_client/spells.pb.h"
#include "shared/client_data/proto_client/spell_visualizations.pb.h"
#include "shared/client_data/project.h"

namespace mmo
{
    class GameUnitC;

    namespace proto_client
    {
        class Project;
    }

    /// \brief Abstract interface for audio playback used by spell visualization service.
    /// This allows the service to remain in the shared game_client library without
    /// directly depending on the concrete mmo_client audio implementation.
    class ISpellAudioPlayer
    {
    public:
        virtual ~ISpellAudioPlayer() = default;

        /// \brief Play a sound effect (fire-and-forget).
        /// \param soundFile The sound file path to play.
        virtual void PlaySound(const std::string& soundFile) = 0;

        /// \brief Start a looped sound and return a handle for stopping it.
        /// \param soundFile The sound file path to loop.
        /// \return A handle to identify this looped sound (0 = failed).
        virtual uint64 PlayLoopedSound(const std::string& soundFile) = 0;

        /// \brief Stop a looped sound by handle.
        /// \param handle The handle returned from PlayLoopedSound.
        virtual void StopLoopedSound(uint64 handle) = 0;
    };

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

        /// \brief Initializes the visualization service with a project reference and audio player.
        /// \param project The loaded client project containing the spell visualization dataset.
        /// \param audioPlayer Audio player interface for sound playback (optional, may be null).
        void Initialize(const proto_client::Project& project, ISpellAudioPlayer* audioPlayer);

        /// \brief Stop any looped sound currently playing for an actor (e.g., on cancel/success/death).
        void StopLoopedSoundForActor(uint64 actorGuid);

    private:
        SpellVisualizationService() = default;
        ~SpellVisualizationService() = default;

        SpellVisualizationService(const SpellVisualizationService&) = delete;
        SpellVisualizationService& operator=(const SpellVisualizationService&) = delete;

    private:
        void ApplyKitToActor(const proto_client::SpellVisualization& vis,
                             const proto_client::SpellKit& kit,
                             GameUnitC& actor);

        static uint32 ToProtoEventValue(Event e);

    private:
        /// \brief Structure to track a looped sound handle per event/actor.
        struct LoopedSoundHandle
        {
            uint64 audioHandle;
            uint32 spellId;
            Event event;
            
            LoopedSoundHandle() 
                : audioHandle(0)
                , spellId(0)
                , event(Event::StartCast)
            {
            }
        };

        /// \brief Pointer to the loaded client project for dataset lookups. Set via Initialize.
        const proto_client::Project* m_project { nullptr };
        ISpellAudioPlayer* m_audioPlayer { nullptr }; // not owned

        /// \brief Map actor guid -> looped sound handle for proper cleanup on cancel/success/aura removal.
        mutable std::map<uint64, LoopedSoundHandle> m_loopedSounds;
    };

    // Free functions for aura visualization notifications (avoid circular includes)
    void NotifyAuraVisualizationApplied(const proto_client::SpellEntry& spell, GameUnitC* target);
    void NotifyAuraVisualizationRemoved(const proto_client::SpellEntry& spell, GameUnitC* target);
}
