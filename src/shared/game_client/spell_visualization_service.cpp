// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spell_visualization_service.h"
#include "game_unit_c.h"
#include "object_mgr.h"
#include "log/default_log_levels.h"

namespace mmo
{
    SpellVisualizationService& SpellVisualizationService::Get()
    {
        static SpellVisualizationService instance;
        return instance;
    }

    void SpellVisualizationService::Initialize(const proto_client::Project& project, IAudio* audioPlayer)
    {
        m_project = &project;
        m_audioPlayer = audioPlayer;
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
            auto applyToList = [&](GameUnitC* unit)
            {
                if(unit)
                {
                    ApplyKitToActor(*vis, kit, *unit);
                }
            };

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
                // Stop any looped sound for this caster
                StopLoopedSoundForActor(caster->GetGuid());
            }
            else if (event == Event::CastSucceeded)
            {
                caster->NotifySpellCastSucceeded();
                // Stop any looped sound for this caster
                StopLoopedSoundForActor(caster->GetGuid());
            }
        }
    }

    void SpellVisualizationService::ApplyKitToActor(const proto_client::SpellVisualization& vis,
                                                     const proto_client::SpellKit& kit,
                                                     GameUnitC& actor)
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

        // Play sounds using the audio interface
        if (m_audioPlayer && kit.sounds_size() > 0)
        {
            const bool isLooped = kit.has_loop() && kit.loop();
            for (const auto& snd : kit.sounds())
            {
                if (isLooped)
                {
                    const uint64 guid = actor.GetGuid();
                    // Stop any existing looped sound for this actor (only one loop per actor at a time)
                    auto it = m_loopedSounds.find(guid);
                    if (it != m_loopedSounds.end() && it->second.audioHandle != InvalidChannel)
                    {
                        m_audioPlayer->StopSound(&it->second.audioHandle);
                    }
                    
                    // Create looped sound
                    SoundIndex soundIdx = m_audioPlayer->FindSound(snd, SoundType::SoundLooped2D);
                    if (soundIdx == InvalidSound)
                    {
                        soundIdx = m_audioPlayer->CreateLoopedSound(snd);
                    }
                    
                    if (soundIdx != InvalidSound)
                    {
                        ChannelIndex channel = InvalidChannel;
                        m_audioPlayer->PlaySound(soundIdx, &channel);
                        if (channel != InvalidChannel)
                        {
                            LoopedSoundHandle loopHandle;
                            loopHandle.audioHandle = channel;
                            loopHandle.spellId = vis.id();
                            loopHandle.event = Event::Casting;
                            m_loopedSounds[guid] = loopHandle;
                        }
                    }
                }
                else
                {
                    SoundIndex soundIdx = m_audioPlayer->FindSound(snd, SoundType::Sound2D);
                    if (soundIdx == InvalidSound)
                    {
                        soundIdx = m_audioPlayer->CreateSound(snd);
                    }
                    if (soundIdx != InvalidSound)
                    {
                        ChannelIndex channel = InvalidChannel;
                        m_audioPlayer->PlaySound(soundIdx, &channel);
                    }
                }
            }
        }

        // TODO: Apply tint (requires material/visual pipeline hook on GameUnitC)
    }

    void SpellVisualizationService::StopLoopedSoundForActor(uint64 actorGuid)
    {
        auto it = m_loopedSounds.find(actorGuid);
        if (it != m_loopedSounds.end())
        {
            if (m_audioPlayer && it->second.audioHandle != InvalidChannel)
            {
                m_audioPlayer->StopSound(&it->second.audioHandle);
            }
            m_loopedSounds.erase(it);
        }
    }

    // Free functions for aura visualization notifications
    void NotifyAuraVisualizationApplied(const proto_client::SpellEntry& spell, GameUnitC* target)
    {
        if (target)
        {
            std::vector<GameUnitC*> targets { target };
            SpellVisualizationService::Get().Apply(SpellVisualizationService::Event::AuraApplied, spell, nullptr, targets);
        }
    }

    void NotifyAuraVisualizationRemoved(const proto_client::SpellEntry& spell, GameUnitC* target)
    {
        if (target)
        {
            std::vector<GameUnitC*> targets { target };
            SpellVisualizationService::Get().Apply(SpellVisualizationService::Event::AuraRemoved, spell, nullptr, targets);
            // Stop looped sounds for this target when aura is removed
            SpellVisualizationService::Get().StopLoopedSoundForActor(target->GetGuid());
        }
    }
}
