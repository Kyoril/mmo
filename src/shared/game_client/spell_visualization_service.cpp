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
            // No kits for this event, but still handle cleanup for certain events
            if (caster != nullptr)
            {
                if (event == Event::CancelCast || event == Event::CastSucceeded)
                {
                    // Stop any looped sound for this caster
                    StopLoopedSoundForActor(caster->GetGuid());
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

        // Stop looped sounds on cancel/success
        if (caster != nullptr)
        {
            if (event == Event::CancelCast || event == Event::CastSucceeded)
            {
                StopLoopedSoundForActor(caster->GetGuid());
            }
        }

        // No legacy caster notifications - visualization is fully data-driven
    }

    void SpellVisualizationService::ApplyKitToActor(const proto_client::SpellVisualization& vis,
                                                     const proto_client::SpellKit& kit,
                                                     GameUnitC& actor)
    {
        // Apply animation if specified
        ApplyAnimationToActor(kit, actor);

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

    void SpellVisualizationService::ApplyAnimationToActor(const proto_client::SpellKit& kit, GameUnitC& actor)
    {
        if (!kit.has_animation_name())
        {
            return;
        }

        const std::string& animName = kit.animation_name();
        if (animName.empty())
        {
            return;
        }

        // Get the animation state from the entity
        Entity* entity = actor.GetEntity();
        if (!entity)
        {
            return;
        }

        AnimationState* animState = entity->GetAnimationState(animName);
        if (!animState)
        {
            WLOG("Animation '" << animName << "' not found on entity");
            return;
        }

        // Determine if this is a looped or one-shot animation
        const bool isLooped = kit.has_loop() && kit.loop();
        animState->SetLoop(isLooped);

        // Apply duration if specified (for one-shot animations)
        if (!isLooped && kit.has_duration_ms())
        {
            const float durationSeconds = static_cast<float>(kit.duration_ms()) / 1000.0f;
            const float animLength = animState->GetLength();
            if (animLength > 0.0f)
            {
                // Adjust playback speed to match desired duration
                const float playRate = animLength / durationSeconds;
                animState->SetPlayRate(playRate);
            }
        }

        // Play the animation
        if (isLooped)
        {
            // For looped animations, set as target state (replaces current animation)
            actor.SetTargetAnimState(animState);
        }
        else
        {
            // For one-shot animations, play as overlay
            actor.PlayOneShotAnimation(animState);
        }
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
