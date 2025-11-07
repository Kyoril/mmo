// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <vector>
#include <cstdint>

#include "base/typedefs.h"
#include "shared/client_data/proto_client/spells.pb.h"
#include "shared/client_data/proto_client/spell_visualizations.pb.h"
#include "shared/client_data/project.h" // For proto_client::Project definition

namespace mmo
{
    class GameUnitC;
    class IAudio; // forward declare audio interface

    namespace proto_client
    {
        class Project; // Forward declaration (already included, but keeps intent clear)
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

        /// \brief Initializes the visualization service with a project reference and audio so it can resolve visualization entries and play sounds.
        /// \param project The loaded client project containing the spell visualization dataset.
        /// \param audio Audio interface for sound playback.
        void Initialize(const proto_client::Project& project, IAudio* audio);

    private:
        SpellVisualizationService() = default;
        ~SpellVisualizationService() = default;

        SpellVisualizationService(const SpellVisualizationService&) = delete;
        SpellVisualizationService& operator=(const SpellVisualizationService&) = delete;

    private:
        void ApplyKitToActor(const proto_client::SpellVisualization& vis,
                             const proto_client::SpellKit& kit,
                             GameUnitC& actor) const;

        static uint32 ToProtoEventValue(Event e);

    private:
        /// \brief Pointer to the loaded client project for dataset lookups. Set via Initialize.
        const proto_client::Project* m_project { nullptr };
        IAudio* m_audio { nullptr }; // not owned
    };
}
