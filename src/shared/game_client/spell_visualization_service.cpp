// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spell_visualization_service.h"
#include "game_unit_c.h"
#include "object_mgr.h"
#include "log/default_log_levels.h"
#include "scene_graph/animation_state.h"

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
        
        const bool hasKits = (it != eventMap.end() && it->second.kits_size() > 0);
        const bool isTerminatingEvent = (event == Event::CancelCast || event == Event::CastSucceeded);
        
        // Handle terminating events - stop active animations and clean up
        if (caster != nullptr && isTerminatingEvent)
        {
            const uint64 casterGuid = caster->GetGuid();
            auto animIt = m_activeSpellAnimations.find(casterGuid);
            if (animIt != m_activeSpellAnimations.end() && animIt->second.spellId == spell.id())
            {
                // Cancel the one-shot animation properly through GameUnitC
                // This ensures proper state management and prevents T-pose
                if (!hasKits)
                {
                    caster->CancelOneShotAnimation();
                }
                
                m_activeSpellAnimations.erase(animIt);
            }
            
            // Stop any looped sound for this caster
            StopLoopedSoundForActor(caster->GetGuid());
            
            // Remove tints for this spell on the caster
            RemoveTintFromActor(*caster, spell.id());
        }
        
        if (!hasKits)
        {
            // No kits to apply, we're done
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
                    ApplyKitToActor(*vis, kit, *unit, spell.id());
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

        // No legacy caster notifications - visualization is fully data-driven
    }

    void SpellVisualizationService::ApplyKitToActor(const proto_client::SpellVisualization& vis,
                                                     const proto_client::SpellKit& kit,
                                                     GameUnitC& actor,
                                                     uint32 spellId)
    {
        // Apply animation if specified
        ApplyAnimationToActor(kit, actor, spellId);

        // Play sounds using the audio interface
        if (m_audioPlayer && kit.sounds_size() > 0)
        {
            const bool isLooped = kit.has_loop() && kit.loop();
            const Vector3 actorPosition = actor.GetPosition();
            
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
                    
                    // Create looped 3D sound
                    SoundIndex soundIdx = m_audioPlayer->FindSound(snd, SoundType::SoundLooped3D);
                    if (soundIdx == InvalidSound)
                    {
                        soundIdx = m_audioPlayer->CreateSound(snd, SoundType::SoundLooped3D);
                    }
                    
                    if (soundIdx != InvalidSound)
                    {
                        ChannelIndex channel = InvalidChannel;
                        m_audioPlayer->PlaySound(soundIdx, &channel);
                        if (channel != InvalidChannel)
                        {
                            // Set 3D position for the sound
                            m_audioPlayer->Set3DPosition(channel, actorPosition);
                            
                            // Set reasonable attenuation distance (10 units min, 50 units max)
                            m_audioPlayer->Set3DMinMaxDistance(channel, 10.0f, 50.0f);
                            
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
                    // Create one-shot 3D sound
                    SoundIndex soundIdx = m_audioPlayer->FindSound(snd, SoundType::Sound3D);
                    if (soundIdx == InvalidSound)
                    {
                        soundIdx = m_audioPlayer->CreateSound(snd, SoundType::Sound3D);
                    }
                    if (soundIdx != InvalidSound)
                    {
                        ChannelIndex channel = InvalidChannel;
                        m_audioPlayer->PlaySound(soundIdx, &channel);
                        if (channel != InvalidChannel)
                        {
                            // Set 3D position for the sound
                            m_audioPlayer->Set3DPosition(channel, actorPosition);
                            
                            // Set reasonable attenuation distance (5 units min, 30 units max)
                            m_audioPlayer->Set3DMinMaxDistance(channel, 5.0f, 30.0f);
                        }
                    }
                }
            }
        }

        // Apply tint to the actor
        ApplyTintToActor(kit, actor, spellId);
    }

    void SpellVisualizationService::ApplyAnimationToActor(const proto_client::SpellKit& kit, GameUnitC& actor, uint32 spellId)
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
            // For one-shot animations, play as overlay and track it for potential cancellation
            actor.PlayOneShotAnimation(animState);
            
            // Track this animation so it can be canceled by subsequent events from the same spell
            const uint64 actorGuid = actor.GetGuid();
            ActiveSpellAnimation& activeAnim = m_activeSpellAnimations[actorGuid];
            activeAnim.spellId = spellId;
            activeAnim.animState = animState;
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

    void SpellVisualizationService::ApplyTintToActor(const proto_client::SpellKit& kit, GameUnitC& actor, uint32 spellId)
    {
        if (!kit.has_tint())
        {
            return;
        }

        const auto& tintProto = kit.tint();
        const Vector4 tintColor(tintProto.r(), tintProto.g(), tintProto.b(), tintProto.a());
        
        // Delegate to GameUnitC to manage tints
        actor.AddSpellTint(spellId, tintColor);
    }

    void SpellVisualizationService::RemoveTintFromActor(GameUnitC& actor, uint32 spellId)
    {
        // Delegate to GameUnitC to manage tints
        actor.RemoveSpellTint(spellId);
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
            // Remove tints for this spell on the target
            SpellVisualizationService::Get().RemoveTintFromActor(*target, spell.id());
        }
    }
}
