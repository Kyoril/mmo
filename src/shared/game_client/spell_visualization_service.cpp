// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spell_visualization_service.h"
#include "game_unit_c.h"
#include "object_mgr.h"
#include "log/default_log_levels.h"
#include "scene_graph/animation_state.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/particle_emitter.h"
#include "scene_graph/particle_emitter_serializer.h"
#include "scene_graph/ribbon_trail.h"
#include "scene_graph/light.h"
#include "scene_graph/skeleton.h"
#include "scene_graph/tag_point.h"
#include "scene_graph/material_manager.h"
#include "assets/asset_registry.h"

#include <algorithm>

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
            // Clear any locked loop animation (e.g. cast channel animation)
            caster->SetLockedLoopAnimation(nullptr);

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
            
            // Fade out any looped sound for this caster (smooth transition)
            FadeOutLoopedSoundForActor(caster->GetGuid());
            
            // Remove tints for this spell on the caster
            RemoveTintFromActor(*caster, spell.id());

            // Clean up visual effects (particles, lights, ribbon trails)
            CleanupEffectsForActor(casterGuid, spell.id());
        }
        
        if (!hasKits)
        {
            // No kits to apply, we're done
            return;
        }

        const bool isInstantEvent = (event == Event::CancelCast || event == Event::CastSucceeded || event == Event::Impact || event == Event::AuraTick);

        const proto_client::SpellKitList& kitList = it->second;

        // Apply kits by scope
        for (const auto& kit : kitList.kits())
        {
            const proto_client::KitScope scope = kit.has_scope() ? kit.scope() : proto_client::CASTER;
            auto applyToList = [&](GameUnitC* unit)
            {
                if(unit)
                {
                    ApplyKitToActor(*vis, kit, *unit, spell.id(), isInstantEvent);
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
                                                     uint32 spellId,
                                                     bool instantEvent)
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
                    // Fade out any existing looped sound for this actor (only one loop per actor at a time)
                    FadeOutLoopedSoundForActor(guid);
                    
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
                            // Start at zero volume for fade-in
                            if (IChannelInstance* channelInstance = m_audioPlayer->GetChannelInstance(channel))
                            {
                                channelInstance->SetVolume(0.0f);
                            }
                            
                            // Set 3D position for the sound
                            m_audioPlayer->Set3DPosition(channel, actorPosition);
                            
                            // Set reasonable attenuation distance (10 units min, 50 units max)
                            m_audioPlayer->Set3DMinMaxDistance(channel, 10.0f, 50.0f);
                            
                            LoopedSoundHandle loopHandle;
                            loopHandle.audioHandle = channel;
                            loopHandle.spellId = vis.id();
                            loopHandle.event = Event::Casting;
                            loopHandle.currentVolume = 0.0f;
                            loopHandle.targetVolume = 1.0f;
                            loopHandle.fadeSpeed = 3.0f; // Fade in over ~0.33 seconds
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
                            // Start at zero volume for fade-in
                            if (IChannelInstance* channelInstance = m_audioPlayer->GetChannelInstance(channel))
                            {
                                channelInstance->SetVolume(0.0f);
                            }
                            
                            // Set 3D position for the sound
                            m_audioPlayer->Set3DPosition(channel, actorPosition);
                            
                            // Set reasonable attenuation distance (5 units min, 30 units max)
                            m_audioPlayer->Set3DMinMaxDistance(channel, 5.0f, 30.0f);
                            
                            // Track for fade-in
                            FadingSound fadingSound;
                            fadingSound.channel = channel;
                            fadingSound.currentVolume = 0.0f;
                            fadingSound.targetVolume = 1.0f;
                            fadingSound.fadeSpeed = 3.0f; // Fade in over ~0.33 seconds
                            fadingSound.markedForRemoval = false;
                            m_fadingSounds.push_back(fadingSound);
                        }
                    }
                }
            }
        }

        // Apply tint to the actor
        ApplyTintToActor(kit, actor, spellId);

        // Spawn particle emitters
        ApplyParticlesToActor(kit, actor, spellId);

        // Spawn point light
        ApplyLightToActor(kit, actor, spellId, instantEvent);

        // Spawn ribbon trail
        ApplyRibbonTrailToActor(kit, actor, spellId);
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
            // For looped animations, lock as the target state so movement animations can't evict it
            actor.SetLockedLoopAnimation(animState);
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

    void SpellVisualizationService::FadeOutLoopedSoundForActor(uint64 actorGuid)
    {
        auto it = m_loopedSounds.find(actorGuid);
        if (it != m_loopedSounds.end())
        {
            // Move the looped sound to the fading sounds list so it can fade out
            // while a new sound can be added for this actor
            if (it->second.audioHandle != InvalidChannel)
            {
                FadingSound fadingSound;
                fadingSound.channel = it->second.audioHandle;
                fadingSound.currentVolume = it->second.currentVolume;
                fadingSound.targetVolume = 0.0f;
                fadingSound.fadeSpeed = 2.0f; // Fade out over ~0.5 seconds
                fadingSound.markedForRemoval = false;
                m_fadingSounds.push_back(fadingSound);
            }
            
            // Remove from the looped sounds map so a new looped sound can be added
            m_loopedSounds.erase(it);
        }
    }

    void SpellVisualizationService::Update(float deltaTime)
    {
        if (!m_audioPlayer)
        {
            return;
        }

        // Update looped sounds fading
        for (auto it = m_loopedSounds.begin(); it != m_loopedSounds.end(); )
        {
            LoopedSoundHandle& handle = it->second;
            
            if (handle.audioHandle == InvalidChannel)
            {
                it = m_loopedSounds.erase(it);
                continue;
            }

            IChannelInstance* channelInstance = m_audioPlayer->GetChannelInstance(handle.audioHandle);
            if (!channelInstance)
            {
                it = m_loopedSounds.erase(it);
                continue;
            }

            // Lerp volume toward target
            if (handle.currentVolume < handle.targetVolume)
            {
                handle.currentVolume += handle.fadeSpeed * deltaTime;
                if (handle.currentVolume >= handle.targetVolume)
                {
                    handle.currentVolume = handle.targetVolume;
                }
            }
            else if (handle.currentVolume > handle.targetVolume)
            {
                handle.currentVolume -= handle.fadeSpeed * deltaTime;
                if (handle.currentVolume <= handle.targetVolume)
                {
                    handle.currentVolume = handle.targetVolume;
                }
            }

            channelInstance->SetVolume(handle.currentVolume);

            // If faded out completely, stop the sound and remove
            if (handle.currentVolume <= 0.0f && handle.targetVolume <= 0.0f)
            {
                m_audioPlayer->StopSound(&handle.audioHandle);
                it = m_loopedSounds.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // Update one-shot sounds fading
        for (auto& fadingSound : m_fadingSounds)
        {
            if (fadingSound.channel == InvalidChannel)
            {
                fadingSound.markedForRemoval = true;
                continue;
            }

            IChannelInstance* channelInstance = m_audioPlayer->GetChannelInstance(fadingSound.channel);
            if (!channelInstance)
            {
                fadingSound.markedForRemoval = true;
                continue;
            }

            // Lerp volume toward target
            if (fadingSound.currentVolume < fadingSound.targetVolume)
            {
                fadingSound.currentVolume += fadingSound.fadeSpeed * deltaTime;
                if (fadingSound.currentVolume >= fadingSound.targetVolume)
                {
                    fadingSound.currentVolume = fadingSound.targetVolume;
                }
            }
            else if (fadingSound.currentVolume > fadingSound.targetVolume)
            {
                fadingSound.currentVolume -= fadingSound.fadeSpeed * deltaTime;
                if (fadingSound.currentVolume <= fadingSound.targetVolume)
                {
                    fadingSound.currentVolume = fadingSound.targetVolume;
                }
            }

            channelInstance->SetVolume(fadingSound.currentVolume);

            // If faded out completely, stop the sound and mark for removal
            if (fadingSound.currentVolume <= 0.0f && fadingSound.targetVolume <= 0.0f)
            {
                m_audioPlayer->StopSound(&fadingSound.channel);
                fadingSound.markedForRemoval = true;
            }
        }

        // Remove sounds that are done
        m_fadingSounds.erase(
            std::remove_if(m_fadingSounds.begin(), m_fadingSounds.end(),
                [](const FadingSound& fs) { return fs.markedForRemoval; }),
            m_fadingSounds.end());

        // Update fading lights
        for (auto it = m_fadingLights.begin(); it != m_fadingLights.end(); )
        {
            if (!it->light)
            {
                it = m_fadingLights.erase(it);
                continue;
            }

            if (it->fadingOut)
            {
                it->currentIntensity -= it->fadeOutSpeed * deltaTime;
                if (it->currentIntensity <= 0.0f)
                {
                    it->currentIntensity = 0.0f;
                    it->light->SetIntensity(0.0f);

                    // Find the scene via the actor and destroy the light
                    auto actor = ObjectMgr::Get<GameUnitC>(it->actorGuid);
                    if (actor)
                    {
                        Scene& scene = actor->GetScene();
                        scene.DestroyLight(*it->light);
                    }

                    // Remove from effect tracking
                    for (auto& effect : m_activeEffects)
                    {
                        for (auto litIt = effect.lights.begin(); litIt != effect.lights.end(); ++litIt)
                        {
                            if (*litIt == it->light)
                            {
                                effect.lights.erase(litIt);
                                break;
                            }
                        }
                    }

                    // Clean up empty effects (destroy remaining scene nodes)
                    for (auto effIt = m_activeEffects.begin(); effIt != m_activeEffects.end(); )
                    {
                        if (effIt->particles.empty() && effIt->lights.empty() &&
                            effIt->ribbonTrails.empty())
                        {
                            auto effActor = ObjectMgr::Get<GameUnitC>(effIt->actorGuid);
                            if (effActor)
                            {
                                Scene& effScene = effActor->GetScene();
                                for (auto* node : effIt->effectNodes)
                                {
                                    if (node)
                                    {
                                        effScene.DestroySceneNode(*node);
                                    }
                                }
                            }
                            effIt = m_activeEffects.erase(effIt);
                        }
                        else
                        {
                            ++effIt;
                        }
                    }

                    it = m_fadingLights.erase(it);
                    continue;
                }

                it->light->SetIntensity(it->currentIntensity);
            }
            else
            {
                if (it->currentIntensity < it->targetIntensity)
                {
                    it->currentIntensity += it->fadeInSpeed * deltaTime;
                    if (it->currentIntensity >= it->targetIntensity)
                    {
                        it->currentIntensity = it->targetIntensity;
                    }
                    it->light->SetIntensity(it->currentIntensity);
                }
                else if (it->autoFadeOut && it->fadeOutSpeed > 0.0f)
                {
                    // Fade-in is complete and this is an instant event — auto-trigger fade-out
                    it->fadingOut = true;
                }
            }

            ++it;
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

    void SpellVisualizationService::ApplyParticlesToActor(const proto_client::SpellKit& kit, GameUnitC& actor, uint32 spellId)
    {
        if (kit.particles_size() == 0)
        {
            return;
        }

        Entity* entity = actor.GetEntity();
        SceneNode* sceneNode = actor.GetSceneNode();
        if (!entity || !sceneNode)
        {
            return;
        }

        Scene& scene = actor.GetScene();
        const uint64 guid = actor.GetGuid();

        // Find or create the effect tracking for this actor+spell
        ActiveSpellEffect* effect = nullptr;
        for (auto& e : m_activeEffects)
        {
            if (e.actorGuid == guid && e.spellId == spellId)
            {
                effect = &e;
                break;
            }
        }

        if (!effect)
        {
            m_activeEffects.emplace_back();
            effect = &m_activeEffects.back();
            effect->spellId = spellId;
            effect->actorGuid = guid;
        }

        for (int i = 0; i < kit.particles_size(); ++i)
        {
            const auto& particlePath = kit.particles(i);
            if (particlePath.empty())
            {
                continue;
            }

            try
            {
                const String emitterName = "SpellParticle_" + std::to_string(m_effectCounter++);
                ParticleEmitter* emitter = scene.CreateParticleEmitter(emitterName);
                if (!emitter)
                {
                    continue;
                }

                // Load particle parameters from .hpfx file
                const auto file = AssetRegistry::OpenFile(particlePath);
                if (file)
                {
                    io::StreamSource source(*file);
                    io::Reader reader(source);

                    ParticleEmitterSerializer serializer;
                    ParticleEmitterParameters params;
                    if (serializer.Deserialize(params, reader))
                    {
                        emitter->SetParameters(params);
                    }
                }

                // Attach to bone if specified
                if (kit.has_attach_bone() && !kit.attach_bone().empty() && entity->HasSkeleton())
                {
                    auto skeleton = entity->GetSkeleton();
                    if (skeleton && skeleton->HasBone(kit.attach_bone()))
                    {
                        entity->AttachObjectToBone(kit.attach_bone(), *emitter);
                    }
                    else
                    {
                        SceneNode* childNode = sceneNode->CreateChildSceneNode(emitterName + "_Node");
                        childNode->AttachObject(*emitter);
                        effect->effectNodes.push_back(childNode);
                    }
                }
                else
                {
                    SceneNode* childNode = sceneNode->CreateChildSceneNode(emitterName + "_Node");
                    childNode->AttachObject(*emitter);
                    effect->effectNodes.push_back(childNode);
                }

                emitter->Play();
                effect->particles.push_back(emitter);
            }
            catch (const std::exception& e)
            {
                ELOG("Failed to spawn spell particle '" << particlePath << "': " << e.what());
            }
        }
    }

    void SpellVisualizationService::ApplyLightToActor(const proto_client::SpellKit& kit, GameUnitC& actor, uint32 spellId, bool instantEvent)
    {
        if (!kit.has_light())
        {
            return;
        }

        Entity* entity = actor.GetEntity();
        SceneNode* sceneNode = actor.GetSceneNode();
        if (!entity || !sceneNode)
        {
            return;
        }

        Scene& scene = actor.GetScene();
        const uint64 guid = actor.GetGuid();

        // Find or create the effect tracking
        ActiveSpellEffect* effect = nullptr;
        for (auto& e : m_activeEffects)
        {
            if (e.actorGuid == guid && e.spellId == spellId)
            {
                effect = &e;
                break;
            }
        }

        if (!effect)
        {
            m_activeEffects.emplace_back();
            effect = &m_activeEffects.back();
            effect->spellId = spellId;
            effect->actorGuid = guid;
        }

        try
        {
            const auto& lightConfig = kit.light();
            const String lightName = "SpellLight_" + std::to_string(m_effectCounter++);

            const float targetIntensity = lightConfig.intensity();
            const float fadeInTime = lightConfig.has_fade_in_time() ? lightConfig.fade_in_time() : 0.3f;
            const float fadeOutTime = lightConfig.has_fade_out_time() ? lightConfig.fade_out_time() : 0.5f;

            Light& light = scene.CreateLight(lightName, LightType::Point);
            light.SetColor(Vector4(lightConfig.r(), lightConfig.g(), lightConfig.b(), 1.0f));
            light.SetRange(lightConfig.range());

            // Start at 0 intensity if fading in, otherwise full intensity
            if (fadeInTime > 0.0f)
            {
                light.SetIntensity(0.0f);
            }
            else
            {
                light.SetIntensity(targetIntensity);
            }

            // Attach to bone if specified
            if (kit.has_attach_bone() && !kit.attach_bone().empty() && entity->HasSkeleton())
            {
                auto skeleton = entity->GetSkeleton();
                if (skeleton && skeleton->HasBone(kit.attach_bone()))
                {
                    entity->AttachObjectToBone(kit.attach_bone(), light);
                }
                else
                {
                    SceneNode* childNode = sceneNode->CreateChildSceneNode(lightName + "_Node");
                    childNode->AttachObject(light);
                    effect->effectNodes.push_back(childNode);
                }
            }
            else
            {
                SceneNode* childNode = sceneNode->CreateChildSceneNode(lightName + "_Node");
                childNode->AttachObject(light);
                effect->effectNodes.push_back(childNode);
            }

            effect->lights.push_back(&light);

            // Track for fading
            FadingLight fl;
            fl.light = &light;
            fl.actorGuid = guid;
            fl.currentIntensity = (fadeInTime > 0.0f) ? 0.0f : targetIntensity;
            fl.targetIntensity = targetIntensity;
            fl.fadeInSpeed = (fadeInTime > 0.0f) ? (targetIntensity / fadeInTime) : 0.0f;
            fl.fadeOutSpeed = (fadeOutTime > 0.0f) ? (targetIntensity / fadeOutTime) : 0.0f;
            fl.fadingOut = false;
            fl.autoFadeOut = instantEvent;
            m_fadingLights.push_back(fl);
        }
        catch (const std::exception& e)
        {
            ELOG("Failed to spawn spell light: " << e.what());
        }
    }

    void SpellVisualizationService::ApplyRibbonTrailToActor(const proto_client::SpellKit& kit, GameUnitC& actor, uint32 spellId)
    {
        if (!kit.has_ribbon_trail())
        {
            return;
        }

        Entity* entity = actor.GetEntity();
        SceneNode* sceneNode = actor.GetSceneNode();
        if (!entity || !sceneNode)
        {
            return;
        }

        Scene& scene = actor.GetScene();
        const uint64 guid = actor.GetGuid();

        // Find or create the effect tracking
        ActiveSpellEffect* effect = nullptr;
        for (auto& e : m_activeEffects)
        {
            if (e.actorGuid == guid && e.spellId == spellId)
            {
                effect = &e;
                break;
            }
        }

        if (!effect)
        {
            m_activeEffects.emplace_back();
            effect = &m_activeEffects.back();
            effect->spellId = spellId;
            effect->actorGuid = guid;
        }

        try
        {
            const auto& trailConfig = kit.ribbon_trail();
            const String trailName = "SpellRibbon_" + std::to_string(m_effectCounter++);

            RibbonTrail* trail = scene.CreateRibbonTrail(trailName);
            if (!trail)
            {
                return;
            }

            // Configure ribbon trail parameters
            RibbonTrailParameters params;
            params.initialWidth = trailConfig.initial_width();
            params.finalWidth = trailConfig.final_width();
            params.initialColor = Vector4(trailConfig.initial_r(), trailConfig.initial_g(),
                trailConfig.initial_b(), trailConfig.initial_a());
            params.finalColor = Vector4(trailConfig.final_r(), trailConfig.final_g(),
                trailConfig.final_b(), trailConfig.final_a());
            params.segmentLifetime = trailConfig.segment_lifetime();
            params.maxSegments = trailConfig.max_segments();
            trail->SetParameters(params);

            // Load material if specified
            if (trailConfig.has_material_name() && !trailConfig.material_name().empty())
            {
                auto material = MaterialManager::Get().Load(trailConfig.material_name());
                if (material)
                {
                    trail->SetMaterial(material);
                }
            }
            else
            {
                trail->SetMaterial(RibbonTrail::GetDefaultMaterial(true));
            }

            // Attach to bone if specified
            if (kit.has_attach_bone() && !kit.attach_bone().empty() && entity->HasSkeleton())
            {
                auto skeleton = entity->GetSkeleton();
                if (skeleton && skeleton->HasBone(kit.attach_bone()))
                {
                    entity->AttachObjectToBone(kit.attach_bone(), *trail);
                }
                else
                {
                    SceneNode* childNode = sceneNode->CreateChildSceneNode(trailName + "_Node");
                    childNode->AttachObject(*trail);
                    effect->effectNodes.push_back(childNode);
                }
            }
            else
            {
                SceneNode* childNode = sceneNode->CreateChildSceneNode(trailName + "_Node");
                childNode->AttachObject(*trail);
                effect->effectNodes.push_back(childNode);
            }

            trail->Play();
            effect->ribbonTrails.push_back(trail);
        }
        catch (const std::exception& e)
        {
            ELOG("Failed to spawn spell ribbon trail: " << e.what());
        }
    }

    void SpellVisualizationService::CleanupEffectsForActor(uint64 actorGuid, uint32 spellId)
    {
        for (auto it = m_activeEffects.begin(); it != m_activeEffects.end(); )
        {
            if (it->actorGuid == actorGuid && it->spellId == spellId)
            {
                // We need a Scene reference to destroy the objects.
                // Try to get it from ObjectMgr via the actor guid.
                Scene* scene = nullptr;
                auto actor = ObjectMgr::Get<GameUnitC>(actorGuid);
                if (actor)
                {
                    scene = &actor->GetScene();
                }

                if (scene)
                {
                    for (auto* emitter : it->particles)
                    {
                        if (emitter)
                        {
                            emitter->Stop();
                            scene->DestroyParticleEmitter(*emitter);
                        }
                    }

                    for (auto* light : it->lights)
                    {
                        if (light)
                        {
                            // Trigger fade-out instead of instant destroy
                            bool foundFading = false;
                            for (auto& fl : m_fadingLights)
                            {
                                if (fl.light == light)
                                {
                                    if (fl.fadeOutSpeed > 0.0f)
                                    {
                                        fl.fadingOut = true;
                                        foundFading = true;
                                    }
                                    break;
                                }
                            }

                            if (!foundFading)
                            {
                                scene->DestroyLight(*light);
                            }
                        }
                    }

                    // Clear the lights vector - fading lights are tracked in m_fadingLights
                    // For non-fading lights, they are already destroyed above
                    // For fading lights, do NOT erase from effect.lights yet (the fade loop handles it)
                    {
                        std::vector<Light*> remainingLights;
                        for (auto* light : it->lights)
                        {
                            bool isFading = false;
                            for (const auto& fl : m_fadingLights)
                            {
                                if (fl.light == light && fl.fadingOut)
                                {
                                    isFading = true;
                                    break;
                                }
                            }
                            if (isFading)
                            {
                                remainingLights.push_back(light);
                            }
                        }
                        it->lights = remainingLights;
                    }

                    for (auto* trail : it->ribbonTrails)
                    {
                        if (trail)
                        {
                            trail->Stop();
                            scene->DestroyRibbonTrail(*trail);
                        }
                    }

                    // Only destroy scene nodes if no lights are still fading
                    if (it->lights.empty())
                    {
                        for (auto* node : it->effectNodes)
                        {
                            if (node)
                            {
                                scene->DestroySceneNode(*node);
                            }
                        }
                        it->effectNodes.clear();
                    }
                }

                // Don't erase if there are still fading-out lights
                if (it->lights.empty())
                {
                    it = m_activeEffects.erase(it);
                }
                else
                {
                    // Keep the effect alive for fading lights, but clear everything else
                    it->particles.clear();
                    it->ribbonTrails.clear();
                    ++it;
                }
            }
            else
            {
                ++it;
            }
        }
    }

    // Free functions for aura visualization notifications
    void NotifyAuraVisualizationApplied(const proto_client::SpellEntry& spell, GameUnitC* caster, GameUnitC* target)
    {
        if (target)
        {
            std::vector<GameUnitC*> targets { target };
            SpellVisualizationService::Get().Apply(SpellVisualizationService::Event::AuraApplied, spell, caster, targets);

            // Start looping idle visualization for the aura duration
            SpellVisualizationService::Get().Apply(SpellVisualizationService::Event::AuraIdle, spell, caster, targets);
        }
    }

    void NotifyAuraVisualizationRemoved(const proto_client::SpellEntry& spell, GameUnitC* caster, GameUnitC* target)
    {
        if (target)
        {
            std::vector<GameUnitC*> targets { target };
            SpellVisualizationService::Get().Apply(SpellVisualizationService::Event::AuraRemoved, spell, caster, targets);
            // Stop looped sounds for this target when aura is removed
            SpellVisualizationService::Get().StopLoopedSoundForActor(target->GetGuid());
            // Remove tints for this spell on the target
            SpellVisualizationService::Get().RemoveTintFromActor(*target, spell.id());
            // Clean up visual effects for this target
            SpellVisualizationService::Get().CleanupEffectsForActor(target->GetGuid(), spell.id());
        }
    }
}
