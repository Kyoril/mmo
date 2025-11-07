// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spell_visualization_service.h"

#include "shared/game_client/object_mgr.h"
#include "shared/game_client/game_unit_c.h"
#include "log/default_log_levels.h"
#include "audio/audio.h"

namespace mmo
{
    SpellVisualizationService& SpellVisualizationService::Get()
    {
        static SpellVisualizationService instance;
        return instance;
    }

    void SpellVisualizationService::Initialize(const proto_client::Project& project, IAudio* audio)
    {
        m_project = &project;
        m_audio = audio;
    }

    uint32 SpellVisualizationService::ToProtoEventValue(Event e)
    {
        switch (e)
        {
        case Event::StartCast:
        {
            return static_cast<uint32>(proto_client::START_CAST);
        }
        case Event::CancelCast:
        {
            return static_cast<uint32>(proto_client::CANCEL_CAST);
        }
        case Event::Casting:
        {
            return static_cast<uint32>(proto_client::CASTING);
        }
        case Event::CastSucceeded:
        {
            return static_cast<uint32>(proto_client::CAST_SUCCEEDED);
        }
        case Event::Impact:
        {
            return static_cast<uint32>(proto_client::IMPACT);
        }
        case Event::AuraApplied:
        {
            return static_cast<uint32>(proto_client::AURA_APPLIED);
        }
        case Event::AuraRemoved:
        {
            return static_cast<uint32>(proto_client::AURA_REMOVED);
        }
        case Event::AuraTick:
        {
            return static_cast<uint32>(proto_client::AURA_TICK);
        }
        case Event::AuraIdle:
        {
            return static_cast<uint32>(proto_client::AURA_IDLE);
        }
        default:
        {
            return static_cast<uint32>(proto_client::START_CAST);
        }
        }
    }

    void SpellVisualizationService::Apply(Event event, const proto_client::SpellEntry& spell, GameUnitC* caster, const std::vector<GameUnitC*>& targets)
    {
        // If no visualization referenced we do nothing (backward compatibility).
        if (!spell.has_visualization_id())
        {
            return;
        }

        // Resolve project
        if (m_project == nullptr)
        {
            // As a fallback, try reading from ObjectMgr's project if available
            WLOG("SpellVisualizationService not initialized with a project; skipping visualization application.");
            return;
        }

        // Lookup visualization entry by id
        const auto* vis = m_project->spellVisualizations.getById(spell.visualization_id());
        if (vis == nullptr)
        {
            WLOG("SpellVisualizationService: visualization id " << spell.visualization_id() << " not found for spell " << spell.id());
            return;
        }

        const uint32 key = ToProtoEventValue(event);
        const auto& eventMap = vis->kits_by_event();
        const auto it = eventMap.find(key);
        if (it == eventMap.end())
        {
            // No kits for this event; but still trigger base unit notifications for key events
            if (caster != nullptr)
            {
                if (event == Event::StartCast)
                {
                    caster->NotifySpellCastStarted();
                }
                else if (event == Event::CancelCast)
                {
                    caster->NotifySpellCastCancelled();
                }
                else if (event == Event::CastSucceeded)
                {
                    caster->NotifySpellCastSucceeded();
                }
            }
            return;
        }

        const proto_client::SpellKitList& kitList = it->second;

        // Apply kits by scope
        for (const auto& kit : kitList.kits())
        {
            const proto_client::KitScope scope = kit.has_scope() ? kit.scope() : proto_client::CASTER;
            auto applyToList = [&](GameUnitC* unit){ if(unit) { ApplyKitToActor(*vis, kit, *unit); } };

            if (scope == proto_client::CASTER)
            {
                applyToList(caster);
            }
            else
            {
                for (GameUnitC* target : targets)
                {
                    applyToList(target);
                }
            }
        }

        // Keep legacy caster notifications for base animations when applicable
        if (caster != nullptr)
        {
            if (event == Event::StartCast)
            {
                caster->NotifySpellCastStarted();
            }
            else if (event == Event::CancelCast)
            {
                caster->NotifySpellCastCancelled();
            }
            else if (event == Event::CastSucceeded)
            {
                caster->NotifySpellCastSucceeded();
            }
        }
    }

    void SpellVisualizationService::ApplyKitToActor(const proto_client::SpellVisualization& vis,
                                                     const proto_client::SpellKit& kit,
                                                     GameUnitC& actor) const
    {
        // Minimal application logic:
        // - Trigger basic notify flags for casting loop/release
        // - If an animation_name is provided, try to map known names to existing states
        if (kit.has_animation_name())
        {
            const auto& name = kit.animation_name();
            if (name == "CastLoop")
            {
                actor.NotifySpellCastStarted();
            }
            else if (name == "CastRelease")
            {
                actor.NotifySpellCastSucceeded();
            }
        }

        // Play sounds (fire-and-forget 2D for now; could be extended to 3D positioning later)
        if (m_audio && kit.sounds_size() > 0)
        {
            for (const auto& snd : kit.sounds())
            {
                // Attempt to find existing sound first (looped vs normal decision could be encoded later)
                auto soundIdx = m_audio->FindSound(snd, SoundType::Sound2D);
                if (soundIdx == InvalidSound)
                {
                    soundIdx = m_audio->CreateSound(snd, SoundType::Sound2D);
                }
                if (soundIdx != InvalidSound)
                {
                    ChannelIndex ch = InvalidChannel;
                    m_audio->PlaySound(soundIdx, &ch);
                }
            }
        }

        // TODO: Apply tint (requires material/visual pipeline hook on GameUnitC)
    }
}
