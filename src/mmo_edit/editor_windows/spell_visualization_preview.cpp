// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spell_visualization_preview.h"
#include "editor_host.h"
#include "editor_imgui_helpers.h"
#include "editor_projectile_manager.h"

#include "audio/audio.h"
#include "graphics/graphics_device.h"
#include "scene_graph/camera.h"
#include "scene_graph/light.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/skeleton.h"
#include "scene_graph/animation.h"
#include "scene_graph/ribbon_trail.h"
#include "scene_graph/particle_emitter_serializer.h"
#include "scene_graph/tag_point.h"
#include "log/default_log_levels.h"

#include "proto_data/project.h"

#include "game_common/projectile_target.h"
#include "assets/asset_registry.h"

#include <algorithm>

namespace mmo
{
	SpellVisualizationPreview::SpellVisualizationPreview(EditorHost& host, IAudio* audioSystem)
		: m_host(host)
		, m_audioSystem(audioSystem)
	{
		m_lastUpdateTime = std::chrono::high_resolution_clock::now();

		// Set up scene camera
		m_cameraAnchor = &m_scene.CreateSceneNode("CameraAnchor");
		m_cameraNode = &m_scene.CreateSceneNode("CameraNode");
		m_cameraAnchor->AddChild(*m_cameraNode);
		m_camera = m_scene.CreateCamera("Camera");
		m_cameraNode->AttachObject(*m_camera);
		m_cameraNode->SetPosition(Vector3(0.0f, 3.0f, 10.0f));
		m_cameraAnchor->SetOrientation(Quaternion(Degree(-10.0f), Vector3::UnitX));
		m_camera->SetNearClipDistance(0.1f);
		m_camera->SetFarClipDistance(500.0f);

		m_scene.GetRootSceneNode().AddChild(*m_cameraAnchor);

		// Create directional light
		m_sunLight = &m_scene.CreateLight("SunLight", LightType::Directional);
		m_sunLight->SetDirection(Vector3(-0.5f, -0.7f, -0.3f));
		m_sunLight->SetColor(Vector4(1.0f, 0.95f, 0.9f, 1.0f));
		m_sunLight->SetIntensity(1.0f);

		// Create grid and axis display
		m_worldGrid = std::make_unique<WorldGrid>(m_scene, "WorldGrid");
		m_axisDisplay = std::make_unique<AxisDisplay>(m_scene, "DebugAxis");
		m_scene.GetRootSceneNode().AddChild(m_axisDisplay->GetSceneNode());

		// Create floor plane
		CreateFloorPlane();

		// Create character entities
		CreateCharacterEntities();

		// Create projectile manager using editor-specific implementation
		m_projectileManager = std::make_unique<EditorProjectileManager>(m_scene, m_audioSystem);
		m_projectileImpactConnection = m_projectileManager->projectileImpact.connect(
			this, &SpellVisualizationPreview::OnProjectileImpact);

		// Create a static target for projectiles (will be positioned at target entity)
		m_projectileTarget = std::make_shared<StaticProjectileTarget>(Vector3::Zero, 1);

		// Connect to update signal
		m_updateConnection = m_host.beforeUiUpdate.connect(this, &SpellVisualizationPreview::Update);
	}

	SpellVisualizationPreview::~SpellVisualizationPreview()
	{
		// Clear audio system reference first to prevent StopAllSounds from accessing destroyed audio
		m_audioSystem = nullptr;
		m_fadingChannels.clear();

		// Clear projectile manager before scene destruction
		m_projectileManager.reset();
		m_projectileTarget.reset();

		CleanupSpellEffects();

		if (m_casterEntity)
		{
			m_scene.DestroyEntity(*m_casterEntity);
			m_casterEntity = nullptr;
		}

		if (m_targetEntity)
		{
			m_scene.DestroyEntity(*m_targetEntity);
			m_targetEntity = nullptr;
		}

		if (m_floorEntity)
		{
			m_scene.DestroyEntity(*m_floorEntity);
			m_floorEntity = nullptr;
		}

		m_worldGrid.reset();
		m_axisDisplay.reset();

		// Destroy deferred renderer before clearing the scene
		m_deferredRenderer.reset();

		m_scene.Clear();
	}

	void SpellVisualizationPreview::Update()
	{
		if (!m_deferredRenderer || m_viewportSize.x <= 0.0f || m_viewportSize.y <= 0.0f)
		{
			return;
		}

		// Calculate delta time
		const auto now = std::chrono::high_resolution_clock::now();
		const float deltaTime = std::chrono::duration<float>(now - m_lastUpdateTime).count();
		m_lastUpdateTime = now;

		// Update animations
		UpdateAnimations(deltaTime);

		// Update sound fades
		UpdateSoundFades(deltaTime);

		// Update light fading (fade-in and fade-out)
		for (auto it = m_fadingLights.begin(); it != m_fadingLights.end(); )
		{
			if (!it->light)
			{
				it = m_fadingLights.erase(it);
				continue;
			}

			if (it->fadingOut)
			{
				// Fade out
				it->currentIntensity -= it->fadeOutSpeed * deltaTime;
				if (it->currentIntensity <= 0.0f)
				{
					it->currentIntensity = 0.0f;
					it->light->SetIntensity(0.0f);
					m_scene.DestroyLight(*it->light);
					it = m_fadingLights.erase(it);
					continue;
				}
				it->light->SetIntensity(it->currentIntensity);
			}
			else
			{
				// Fade in
				if (it->currentIntensity < it->targetIntensity)
				{
					it->currentIntensity += it->fadeInSpeed * deltaTime;
					if (it->currentIntensity >= it->targetIntensity)
					{
						it->currentIntensity = it->targetIntensity;
					}
					it->light->SetIntensity(it->currentIntensity);
				}
			}

			++it;
		}

		// Update projectiles using the EditorProjectileManager
		if (m_projectileManager)
		{
			m_projectileManager->Update(deltaTime);
		}

		// Update cast sequence
		if (m_castSequenceActive)
		{
			m_sequenceTimer += deltaTime;

			switch (m_currentSequenceEvent)
			{
			case PreviewEvent::StartCast:
				// Transition to Casting after a brief moment
				if (m_sequenceTimer >= 0.3f)
				{
					m_currentSequenceEvent = PreviewEvent::Casting;
					m_sequenceTimer = 0.0f;
					TriggerEvent(PreviewEvent::Casting);
				}
				break;

			case PreviewEvent::Casting:
				// Transition to CastSucceeded after cast duration
				if (m_sequenceTimer >= m_castDuration)
				{
					m_currentSequenceEvent = PreviewEvent::CastSucceeded;
					m_sequenceTimer = 0.0f;
					TriggerEvent(PreviewEvent::CastSucceeded);
				}
				break;

			case PreviewEvent::CastSucceeded:
				// Wait for SpellGo animation notify to spawn projectile
				// Fallback: if animation ends without SpellGo notify, spawn anyway
				if (!m_projectileSpawned && m_waitingForSpellGo && m_hasCastSucceededAnimation)
				{
					// Check if the caster animation has ended (fallback trigger)
					if (m_casterAnimState && m_casterAnimState->HasEnded())
					{
						StartProjectile();
						m_projectileSpawned = true;
						m_waitingForSpellGo = false;
					}
				}
				
				// Give some time for projectile to travel, then auto-transition to impact
				// The actual impact timing will depend on projectile speed and distance
				// We use a longer timeout to ensure the projectile has time to reach
				if (m_projectileSpawned && m_sequenceTimer >= 4.0f)
				{
					m_currentSequenceEvent = PreviewEvent::Impact;
					m_sequenceTimer = 0.0f;
					TriggerEvent(PreviewEvent::Impact);
				}
				break;

			case PreviewEvent::Impact:
				// End sequence or loop
				if (m_sequenceTimer >= 1.0f)
				{
					if (m_loopSequence)
					{
						// Restart the sequence
						m_currentSequenceEvent = PreviewEvent::StartCast;
						m_sequenceTimer = 0.0f;
						m_projectileSpawned = false;
						TriggerEvent(PreviewEvent::StartCast);
					}
					else
					{
						m_castSequenceActive = false;

						// Reset caster animation to idle
						if (m_casterEntity && m_casterEntity->HasAnimationState("Idle"))
						{
							if (m_casterAnimState)
							{
								m_casterAnimState->SetEnabled(false);
							}
							m_casterAnimState = m_casterEntity->GetAnimationState("Idle");
							m_casterAnimState->SetEnabled(true);
							m_casterAnimState->SetLoop(true);
						}

						// Reset target animation to idle
						if (m_targetEntity && m_targetEntity->HasAnimationState("Idle"))
						{
							if (m_targetAnimState)
							{
								m_targetAnimState->SetEnabled(false);
							}
							m_targetAnimState = m_targetEntity->GetAnimationState("Idle");
							m_targetAnimState->SetEnabled(true);
							m_targetAnimState->SetLoop(true);
						}
					}
				}
				break;

			default:
				break;
			}
		}

		// Render the scene using deferred renderer
		auto& gx = GraphicsDevice::Get();

		gx.CaptureState();
		gx.Reset();
		m_camera->SetAspectRatio(m_viewportSize.x / m_viewportSize.y);
		m_camera->SetFillMode(m_wireFrame ? FillMode::Wireframe : FillMode::Solid);

		m_deferredRenderer->Render(m_scene, *m_camera);

		gx.RestoreState();
	}

	void SpellVisualizationPreview::DrawViewport(proto::SpellVisualization* visualization, const String& panelId)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		
		if (ImGui::BeginChild(panelId.c_str(), ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			// Store current visualization
			m_currentVisualization = visualization;

			// Get available size
			const ImVec2 availableSpace = ImGui::GetContentRegionAvail();

			// Create or resize deferred renderer
			if (!m_deferredRenderer && availableSpace.x > 0 && availableSpace.y > 0)
			{
				m_deferredRenderer = std::make_unique<DeferredRenderer>(GraphicsDevice::Get(), m_scene,
					static_cast<uint32>(availableSpace.x),
					static_cast<uint32>(availableSpace.y));
				m_viewportSize = availableSpace;
			}
			else if (m_deferredRenderer && (m_viewportSize.x != availableSpace.x || m_viewportSize.y != availableSpace.y))
			{
				if (availableSpace.x > 0 && availableSpace.y > 0)
				{
					m_deferredRenderer->Resize(static_cast<uint32>(availableSpace.x), static_cast<uint32>(availableSpace.y));
					m_viewportSize = availableSpace;
				}
			}

			// Render the viewport image
			if (m_deferredRenderer && m_deferredRenderer->GetFinalRenderTarget())
			{
				ImGui::Image(m_deferredRenderer->GetFinalRenderTarget()->GetTextureObject(), availableSpace);
				ImGui::SetItemUsingMouseWheel();

				// Handle mouse wheel zoom
				if (ImGui::IsItemHovered())
				{
					m_cameraNode->Translate(Vector3::UnitZ * ImGui::GetIO().MouseWheel * -0.5f, TransformSpace::Local);
				}

				// Handle mouse drag for camera control
				if (ImGui::IsItemActive())
				{
					const ImVec2 delta = ImGui::GetIO().MouseDelta;

					if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
					{
						// Rotate camera
						m_cameraAnchor->Yaw(Degree(-delta.x * 0.3f), TransformSpace::World);
						m_cameraAnchor->Pitch(Degree(delta.y * 0.3f), TransformSpace::Local);
					}
					else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
					{
						// Pan camera
						const Vector3 panOffset = m_cameraNode->GetOrientation() * Vector3(-delta.x * 0.01f, delta.y * 0.01f, 0.0f);
						m_cameraAnchor->Translate(panOffset);
					}
					else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
					{
						// Zoom camera
						m_cameraNode->Translate(Vector3::UnitZ * delta.y * 0.05f, TransformSpace::Local);
					}
				}
			}
			else
			{
				ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Initializing preview...");
			}
		}
		ImGui::EndChild();
		
		ImGui::PopStyleVar();
	}

	void SpellVisualizationPreview::DrawToolbar(proto::SpellVisualization* visualization)
	{
		// Row 1: Main playback controls
		if (DrawSuccessButton("Play", ImVec2(50, 0)))
		{
			TriggerFullCastSequence();
		}

		ImGui::SameLine();

		if (DrawDangerButton("Stop", ImVec2(50, 0)))
		{
			StopPreview();
		}

		ImGui::SameLine();
		ImGui::Checkbox("Loop", &m_loopSequence);

		ImGui::SameLine();
		ImGui::Checkbox("Wire", &m_wireFrame);

		ImGui::SameLine();
		ImGui::SetNextItemWidth(70);
		ImGui::DragFloat("##cast", &m_castDuration, 0.1f, 0.5f, 5.0f, "%.1fs cast");

		// Row 2: Individual event triggers
		if (DrawPrimarySmallButton("Start"))
		{
			TriggerEvent(PreviewEvent::StartCast);
		}
		ImGui::SameLine();
		if (DrawPrimarySmallButton("Casting"))
		{
			TriggerEvent(PreviewEvent::Casting);
		}
		ImGui::SameLine();
		if (DrawPrimarySmallButton("OK"))
		{
			TriggerEvent(PreviewEvent::CastSucceeded);
		}
		ImGui::SameLine();
		if (DrawPrimarySmallButton("Impact"))
		{
			TriggerEvent(PreviewEvent::Impact);
		}
		ImGui::SameLine();
		if (DrawPrimarySmallButton("Cancel"))
		{
			TriggerEvent(PreviewEvent::CancelCast);
		}
	}

	void SpellVisualizationPreview::TriggerEvent(PreviewEvent event)
	{
		if (!m_currentVisualization)
		{
			return;
		}

		// Fade out sounds from previous event (allows overlap during transition)
		FadeOutPreviousSounds();

		// Map PreviewEvent to proto::SpellVisualEvent
		uint32 protoEvent = 0;
		switch (event)
		{
		case PreviewEvent::StartCast:
			protoEvent = 0; // START_CAST
			m_projectileSpawned = false; // Reset for new cast
			m_waitingForSpellGo = false;
			m_hasCastSucceededAnimation = false;
			break;
		case PreviewEvent::CancelCast:
			protoEvent = 1; // CANCEL_CAST
			break;
		case PreviewEvent::Casting:
			protoEvent = 2; // CASTING
			break;
		case PreviewEvent::CastSucceeded:
			protoEvent = 3; // CAST_SUCCEEDED
			// Will check after ApplyEventKits if an animation was set
			break;
		case PreviewEvent::Impact:
			protoEvent = 4; // IMPACT
			break;
		}

		// Remember caster animation state before applying kits
		AnimationState* prevCasterAnim = m_casterAnimState;

		// Apply the kits for this event
		ApplyEventKits(m_currentVisualization, protoEvent);

		// For CastSucceeded, check if a valid caster animation was set
		if (event == PreviewEvent::CastSucceeded)
		{
			// Check if a new non-looping caster animation was applied
			m_hasCastSucceededAnimation = (m_casterAnimState != nullptr && 
			                               m_casterAnimState != prevCasterAnim &&
			                               !m_casterAnimState->IsLoop());

			if (m_hasCastSucceededAnimation)
			{
				// Wait for SpellGo animation notify or animation end
				m_waitingForSpellGo = true;
			}
			else
			{
				// No animation to wait for - spawn projectile immediately
				if (!m_projectileSpawned)
				{
					StartProjectile();
					m_projectileSpawned = true;
				}
				m_waitingForSpellGo = false;
			}
		}
	}

	void SpellVisualizationPreview::TriggerFullCastSequence()
	{
		// Reset and start the sequence
		StopPreview();
		
		m_castSequenceActive = true;
		m_currentSequenceEvent = PreviewEvent::StartCast;
		m_sequenceTimer = 0.0f;

		TriggerEvent(PreviewEvent::StartCast);
	}

	void SpellVisualizationPreview::StopPreview()
	{
		m_castSequenceActive = false;
		m_sequenceTimer = 0.0f;
		m_projectileSpawned = false;
		m_waitingForSpellGo = false;
		m_hasCastSucceededAnimation = false;

		// Stop all sounds
		StopAllSounds();

		// Clear any active projectiles
		if (m_projectileManager)
		{
			m_projectileManager->Clear();
		}

		// Reset animations to idle
		if (m_casterEntity && m_casterEntity->HasAnimationState("Idle"))
		{
			m_casterAnimState = m_casterEntity->GetAnimationState("Idle");
			m_casterAnimState->SetEnabled(true);
			m_casterAnimState->SetLoop(true);
		}

		if (m_targetEntity && m_targetEntity->HasAnimationState("Idle"))
		{
			m_targetAnimState = m_targetEntity->GetAnimationState("Idle");
			m_targetAnimState->SetEnabled(true);
			m_targetAnimState->SetLoop(true);
		}
	}

	void SpellVisualizationPreview::SetCasterMesh(const String& meshPath)
	{
		if (meshPath == m_casterMeshPath || meshPath.empty())
		{
			return;
		}

		m_casterMeshPath = meshPath;

		// Clear animation notify connections (will be reconnected)
		m_animNotifyConnections.disconnect();

		// Recreate caster entity
		if (m_casterEntity)
		{
			m_scene.DestroyEntity(*m_casterEntity);
			m_casterEntity = nullptr;
			m_casterAnimState = nullptr;
		}

		try
		{
			m_casterEntity = m_scene.CreateEntity("Caster", m_casterMeshPath);
			if (m_casterEntity && m_casterNode)
			{
				m_casterNode->AttachObject(*m_casterEntity);

				if (m_casterEntity->HasAnimationState("Idle"))
				{
					m_casterAnimState = m_casterEntity->GetAnimationState("Idle");
					m_casterAnimState->SetEnabled(true);
					m_casterAnimState->SetLoop(true);
				}

				// Reconnect animation notify signals for SpellGo events
				ConnectAnimationNotifySignals(m_casterEntity);
			}
		}
		catch (...)
		{
			WLOG("Failed to load caster mesh: " << m_casterMeshPath);
		}
	}

	void SpellVisualizationPreview::SetTargetMesh(const String& meshPath)
	{
		if (meshPath == m_targetMeshPath || meshPath.empty())
		{
			return;
		}

		m_targetMeshPath = meshPath;

		// Recreate target entity
		if (m_targetEntity)
		{
			m_scene.DestroyEntity(*m_targetEntity);
			m_targetEntity = nullptr;
			m_targetAnimState = nullptr;
		}

		try
		{
			m_targetEntity = m_scene.CreateEntity("Target", m_targetMeshPath);
			if (m_targetEntity && m_targetNode)
			{
				m_targetNode->AttachObject(*m_targetEntity);

				if (m_targetEntity->HasAnimationState("Idle"))
				{
					m_targetAnimState = m_targetEntity->GetAnimationState("Idle");
					m_targetAnimState->SetEnabled(true);
					m_targetAnimState->SetLoop(true);
				}
			}
		}
		catch (...)
		{
			WLOG("Failed to load target mesh: " << m_targetMeshPath);
		}
	}

	void SpellVisualizationPreview::SyncWithVisualization(const proto::SpellVisualization& visualization)
	{
		m_currentVisualization = const_cast<proto::SpellVisualization*>(&visualization);
		// Projectile visuals are now handled by EditorProjectileManager
		// The manager creates and destroys projectile entities as needed
	}

	void SpellVisualizationPreview::CreateFloorPlane()
	{
		m_floorNode = m_scene.GetRootSceneNode().CreateChildSceneNode("FloorNode");
		m_floorNode->SetPosition(Vector3(0.0f, 0.0f, 0.0f));
		m_floorNode->SetScale(Vector3(15.0f, 1.0f, 15.0f));

		try
		{
			m_floorEntity = m_scene.CreateEntity("Floor", "Editor/Plane.hmsh");
			if (m_floorEntity)
			{
				m_floorNode->AttachObject(*m_floorEntity);

				auto defaultMat = MaterialManager::Get().Load("Models/Default.hmat");
				if (defaultMat)
				{
					m_floorEntity->SetMaterial(defaultMat);
				}
			}
		}
		catch (...)
		{
			WLOG("Could not load floor plane mesh");
		}
	}

	void SpellVisualizationPreview::CreateCharacterEntities()
	{
		// Clear any existing animation notify connections
		m_animNotifyConnections.disconnect();

		// Create caster node and entity (positioned to the left)
		m_casterNode = m_scene.GetRootSceneNode().CreateChildSceneNode("CasterNode");
		m_casterNode->SetPosition(Vector3(-3.0f, 0.0f, 0.0f));
		m_casterNode->Yaw(Degree(90.0f), TransformSpace::Local);

		try
		{
			m_casterEntity = m_scene.CreateEntity("Caster", m_casterMeshPath);
			if (m_casterEntity)
			{
				m_casterNode->AttachObject(*m_casterEntity);

				if (m_casterEntity->HasAnimationState("Idle"))
				{
					m_casterAnimState = m_casterEntity->GetAnimationState("Idle");
					m_casterAnimState->SetEnabled(true);
					m_casterAnimState->SetLoop(true);
				}

				// Connect to animation notify signals for SpellGo events
				ConnectAnimationNotifySignals(m_casterEntity);
			}
		}
		catch (...)
		{
			WLOG("Failed to load caster mesh: " << m_casterMeshPath);
		}

		// Create target node and entity (positioned to the right)
		m_targetNode = m_scene.GetRootSceneNode().CreateChildSceneNode("TargetNode");
		m_targetNode->SetPosition(Vector3(3.0f, 0.0f, 0.0f));
		m_targetNode->Yaw(Degree(-90.0f), TransformSpace::Local);

		try
		{
			m_targetEntity = m_scene.CreateEntity("Target", m_targetMeshPath);
			if (m_targetEntity)
			{
				m_targetNode->AttachObject(*m_targetEntity);

				if (m_targetEntity->HasAnimationState("Idle"))
				{
					m_targetAnimState = m_targetEntity->GetAnimationState("Idle");
					m_targetAnimState->SetEnabled(true);
					m_targetAnimState->SetLoop(true);
				}
			}
		}
		catch (...)
		{
			WLOG("Failed to load target mesh: " << m_targetMeshPath);
		}

		// Projectile visuals are now managed by EditorProjectileManager
	}

	void SpellVisualizationPreview::UpdateAnimations(float deltaTime)
	{
		if (m_casterAnimState)
		{
			m_casterAnimState->AddTime(deltaTime);
		}

		if (m_targetAnimState)
		{
			m_targetAnimState->AddTime(deltaTime);
		}
	}

	void SpellVisualizationPreview::StartProjectile()
	{
		if (!m_casterNode || !m_targetNode)
		{
			return;
		}

		if (!m_projectileManager)
		{
			return;
		}

		if (!m_currentVisualization)
		{
			return;
		}

		// Update target position for the static target
		const Vector3 targetPos = m_targetNode->GetDerivedPosition() + Vector3(0.0f, 0.8f, 0.0f);
		m_projectileTarget->SetPosition(targetPos);

		// Determine start position: use spawn bone if specified, else default offset
		Vector3 startPos;
		if (m_currentVisualization->has_projectile() && m_currentVisualization->projectile().has_spawn_bone() &&
			!m_currentVisualization->projectile().spawn_bone().empty())
		{
			startPos = GetBoneWorldPosition(m_casterEntity, m_casterNode,
				m_currentVisualization->projectile().spawn_bone(),
				Vector3(0.0f, 1.2f, 0.0f));
		}
		else
		{
			startPos = m_casterNode->GetDerivedPosition() + Vector3(0.0f, 1.2f, 0.0f);
		}

		// Build projectile parameters from the visualization
		ProjectileParams params;
		params.speed = m_projectileSpeed;
		
		if (m_currentVisualization->has_projectile())
		{
			const auto& projVis = m_currentVisualization->projectile();
			
			if (projVis.has_mesh_name())
			{
				params.meshFile = projVis.mesh_name();
			}
			
			if (projVis.has_trail_particle())
			{
				params.particleFile = projVis.trail_particle();
			}
			
			if (projVis.has_motion())
			{
				params.motionType = static_cast<ProjectileMotionType>(projVis.motion());
			}
			
			if (projVis.has_arc_height())
			{
				params.arcHeight = projVis.arc_height();
			}
			
			if (projVis.has_wave_amplitude())
			{
				params.sineAmplitude = projVis.wave_amplitude();
			}
			
			if (projVis.has_wave_frequency())
			{
				params.sineFrequency = projVis.wave_frequency();
			}
			
			if (projVis.has_scale())
			{
				params.scale = projVis.scale();
			}
			
			if (projVis.has_face_movement())
			{
				params.faceMovement = projVis.face_movement();
			}

			// Light parameters
			if (projVis.has_light())
			{
				const auto& lightConfig = projVis.light();
				params.hasLight = true;
				params.lightColor = Vector4(
					lightConfig.has_r() ? lightConfig.r() : 1.0f,
					lightConfig.has_g() ? lightConfig.g() : 0.6f,
					lightConfig.has_b() ? lightConfig.b() : 0.2f,
					1.0f);
				params.lightIntensity = lightConfig.has_intensity() ? lightConfig.intensity() : 2.0f;
				params.lightRange = lightConfig.has_range() ? lightConfig.range() : 8.0f;
				params.lightFadeInTime = lightConfig.has_fade_in_time() ? lightConfig.fade_in_time() : 0.3f;
				params.lightFadeOutTime = lightConfig.has_fade_out_time() ? lightConfig.fade_out_time() : 0.5f;
			}

			// Ribbon trail parameters
			if (projVis.has_ribbon_trail())
			{
				const auto& ribbonConfig = projVis.ribbon_trail();
				params.hasRibbonTrail = true;

				if (ribbonConfig.has_material_name())
				{
					params.ribbonMaterial = ribbonConfig.material_name();
				}

				if (ribbonConfig.has_initial_width())
				{
					params.ribbonInitialWidth = ribbonConfig.initial_width();
				}

				if (ribbonConfig.has_final_width())
				{
					params.ribbonFinalWidth = ribbonConfig.final_width();
				}

				params.ribbonInitialColor = Vector4(
					ribbonConfig.initial_r(),
					ribbonConfig.initial_g(),
					ribbonConfig.initial_b(),
					ribbonConfig.initial_a());

				params.ribbonFinalColor = Vector4(
					ribbonConfig.final_r(),
					ribbonConfig.final_g(),
					ribbonConfig.final_b(),
					ribbonConfig.final_a());

				if (ribbonConfig.has_segment_lifetime())
				{
					params.ribbonSegmentLifetime = ribbonConfig.segment_lifetime();
				}

				if (ribbonConfig.has_max_segments())
				{
					params.ribbonMaxSegments = ribbonConfig.max_segments();
				}
			}
		}

		m_projectileManager->SpawnProjectile(params, startPos, m_projectileTarget);
	}

	void SpellVisualizationPreview::SetProjectileSpeed(float speed)
	{
		m_projectileSpeed = speed;
	}

	void SpellVisualizationPreview::OnProjectileImpact(IProjectileTarget* target)
	{
		// Spawn impact particle if configured
		if (m_currentVisualization && m_currentVisualization->has_projectile())
		{
			const auto& projVis = m_currentVisualization->projectile();
			if (projVis.has_impact_particle() && !projVis.impact_particle().empty() && target)
			{
				SpawnImpactParticle(projVis.impact_particle(), target->GetPosition());
			}
		}

		// When projectile hits, trigger the impact event
		if (m_castSequenceActive && m_currentSequenceEvent == PreviewEvent::CastSucceeded)
		{
			// Transition to impact event
			m_currentSequenceEvent = PreviewEvent::Impact;
			m_sequenceTimer = 0.0f;
			TriggerEvent(PreviewEvent::Impact);
		}
	}

	void SpellVisualizationPreview::OnAnimationNotify(const AnimationNotify& notify, const String& animName, const AnimationState& state)
	{
		// Only handle SpellGo notifies when we're waiting for one
		if (notify.GetType() == AnimationNotifyType::SpellGo && m_waitingForSpellGo && !m_projectileSpawned)
		{
			// SpellGo notify triggered - spawn the projectile now
			StartProjectile();
			m_projectileSpawned = true;
			m_waitingForSpellGo = false;
		}
	}

	void SpellVisualizationPreview::ConnectAnimationNotifySignals(Entity* entity)
	{
		if (!entity || !entity->GetSkeleton())
		{
			return;
		}

		AnimationStateSet* animStateSet = entity->GetAllAnimationStates();
		if (!animStateSet)
		{
			return;
		}

		Skeleton* skeleton = entity->GetSkeleton().get();
		if (!skeleton)
		{
			return;
		}

		// Iterate through all animations in the skeleton
		const uint16 numAnims = skeleton->GetNumAnimations();
		for (uint16 i = 0; i < numAnims; ++i)
		{
			Animation* anim = skeleton->GetAnimation(i);
			if (!anim)
			{
				continue;
			}

			AnimationState* animState = animStateSet->GetAnimationState(anim->GetName());
			if (!animState)
			{
				continue;
			}

			// Subscribe to the signal - filter SpellGo notifies in the callback
			m_animNotifyConnections += animState->notifyTriggered.connect(
				[this](const AnimationNotify& notify, const String& animName, const AnimationState& state)
				{
					OnAnimationNotify(notify, animName, state);
				});
		}
	}

	void SpellVisualizationPreview::CleanupSpellEffects()
	{
		StopAllSounds();

		// Clear projectiles
		if (m_projectileManager)
		{
			m_projectileManager->Clear();
		}

		// Clean up kit-spawned particle emitters
		for (auto* emitter : m_kitParticles)
		{
			if (emitter)
			{
				emitter->Stop();
				m_scene.DestroyParticleEmitter(*emitter);
			}
		}
		m_kitParticles.clear();

		// Fade out kit-spawned lights (or destroy instantly if no fade-out)
		for (auto& fadingLight : m_fadingLights)
		{
			if (fadingLight.light && !fadingLight.fadingOut)
			{
				if (fadingLight.fadeOutSpeed > 0.0f)
				{
					fadingLight.fadingOut = true;
				}
				else
				{
					m_scene.DestroyLight(*fadingLight.light);
					fadingLight.light = nullptr;
				}
			}
		}
		// Remove already destroyed entries
		m_fadingLights.erase(
			std::remove_if(m_fadingLights.begin(), m_fadingLights.end(),
				[](const FadingLight& fl) { return fl.light == nullptr; }),
			m_fadingLights.end());

		// Clean up kit-spawned ribbon trails
		for (auto* trail : m_kitRibbonTrails)
		{
			if (trail)
			{
				trail->Stop();
				m_scene.DestroyRibbonTrail(*trail);
			}
		}
		m_kitRibbonTrails.clear();

		// Clean up kit effect scene nodes
		for (auto* node : m_kitEffectNodes)
		{
			if (node)
			{
				m_scene.DestroySceneNode(*node);
			}
		}
		m_kitEffectNodes.clear();

		// Legacy particles
		if (m_castParticles)
		{
			m_scene.DestroyParticleEmitter(*m_castParticles);
			m_castParticles = nullptr;
		}

		if (m_impactParticles)
		{
			m_scene.DestroyParticleEmitter(*m_impactParticles);
			m_impactParticles = nullptr;
		}
	}

	void SpellVisualizationPreview::ApplyEventKits(proto::SpellVisualization* visualization, uint32 eventValue)
	{
		if (!visualization)
		{
			return;
		}

		// Clean up effects from the previous event
		for (auto* emitter : m_kitParticles)
		{
			if (emitter)
			{
				emitter->Stop();
				m_scene.DestroyParticleEmitter(*emitter);
			}
		}
		m_kitParticles.clear();

		// Trigger fade-out on active lights (or destroy instantly if no fade)
		for (auto& fadingLight : m_fadingLights)
		{
			if (fadingLight.light && !fadingLight.fadingOut)
			{
				if (fadingLight.fadeOutSpeed > 0.0f)
				{
					fadingLight.fadingOut = true;
				}
				else
				{
					m_scene.DestroyLight(*fadingLight.light);
					fadingLight.light = nullptr;
				}
			}
		}
		m_fadingLights.erase(
			std::remove_if(m_fadingLights.begin(), m_fadingLights.end(),
				[](const FadingLight& fl) { return fl.light == nullptr; }),
			m_fadingLights.end());

		for (auto* trail : m_kitRibbonTrails)
		{
			if (trail)
			{
				trail->Stop();
				m_scene.DestroyRibbonTrail(*trail);
			}
		}
		m_kitRibbonTrails.clear();

		for (auto* node : m_kitEffectNodes)
		{
			if (node)
			{
				m_scene.DestroySceneNode(*node);
			}
		}
		m_kitEffectNodes.clear();

		const auto& kitsMap = visualization->kits_by_event();
		auto it = kitsMap.find(eventValue);
		if (it == kitsMap.end())
		{
			return;
		}

		const auto& kitList = it->second;

		for (const auto& kit : kitList.kits())
		{
			// Determine target entity and node based on scope
			Entity* targetEntity = nullptr;
			SceneNode* targetNode = nullptr;

			switch (kit.scope())
			{
			case proto::CASTER:
				targetEntity = m_casterEntity;
				targetNode = m_casterNode;
				break;
			case proto::TARGET:
			case proto::PROJECTILE_IMPACT:
				targetEntity = m_targetEntity;
				targetNode = m_targetNode;
				break;
			default:
				break;
			}

			// Apply animation
			if (kit.has_animation_name() && !kit.animation_name().empty())
			{
				AnimationState** animStatePtr = nullptr;

				if (kit.scope() == proto::CASTER)
				{
					animStatePtr = &m_casterAnimState;
				}
				else
				{
					animStatePtr = &m_targetAnimState;
				}

				if (targetEntity && animStatePtr && targetEntity->HasAnimationState(kit.animation_name()))
				{
					// Disable current animation state first
					if (*animStatePtr)
					{
						(*animStatePtr)->SetEnabled(false);
					}

					*animStatePtr = targetEntity->GetAnimationState(kit.animation_name());
					(*animStatePtr)->SetEnabled(true);
					(*animStatePtr)->SetLoop(kit.has_loop() && kit.loop());
					(*animStatePtr)->SetTimePosition(0.0f);
				}
			}

			// Play sounds
			for (const auto& sound : kit.sounds())
			{
				if (!sound.empty())
				{
					PlaySound(sound);
				}
			}

			// Spawn particles
			if (kit.particles_size() > 0 && targetEntity && targetNode)
			{
				SpawnKitParticles(kit, targetEntity, targetNode);
			}

			// Spawn point light
			if (kit.has_light() && targetEntity && targetNode)
			{
				SpawnKitLight(kit, targetEntity, targetNode);
			}

			// Spawn ribbon trail
			if (kit.has_ribbon_trail() && targetEntity && targetNode)
			{
				SpawnKitRibbonTrail(kit, targetEntity, targetNode);
			}
		}
	}

	void SpellVisualizationPreview::PlaySound(const String& soundPath)
	{
		if (!m_audioSystem || soundPath.empty())
		{
			return;
		}

		try
		{
			SoundIndex soundIndex = m_audioSystem->CreateSound(soundPath, SoundType::Sound2D);
			if (soundIndex != InvalidSound)
			{
				ChannelIndex channelIndex = InvalidChannel;
				m_audioSystem->PlaySound(soundIndex, &channelIndex);
				
				// Track the channel with fade-in state
				if (channelIndex != InvalidChannel)
				{
					// Start at zero volume for fade-in
					if (IChannelInstance* channelInstance = m_audioSystem->GetChannelInstance(channelIndex))
					{
						channelInstance->SetVolume(0.0f);
					}

					FadingChannel fadingChannel;
					fadingChannel.channel = channelIndex;
					fadingChannel.currentVolume = 0.0f;
					fadingChannel.targetVolume = 0.75f;
					fadingChannel.fadeSpeed = 1.0f; // Fade in over ~0.33 seconds
					fadingChannel.markedForRemoval = false;
					m_fadingChannels.push_back(fadingChannel);
				}
			}
		}
		catch (...)
		{
			WLOG("Failed to play sound: " << soundPath);
		}
	}

	void SpellVisualizationPreview::FadeOutPreviousSounds()
	{
		if (!m_audioSystem)
		{
			return;
		}

		// Mark all current sounds for fade-out instead of stopping them
		for (auto& fadingChannel : m_fadingChannels)
		{
			fadingChannel.targetVolume = 0.0f;
			fadingChannel.fadeSpeed = 1.0f; // Fade out over ~0.5 seconds
		}
	}

	void SpellVisualizationPreview::UpdateSoundFades(float deltaTime)
	{
		if (!m_audioSystem)
		{
			return;
		}

		for (auto& fadingChannel : m_fadingChannels)
		{
			if (fadingChannel.channel == InvalidChannel)
			{
				fadingChannel.markedForRemoval = true;
				continue;
			}

			IChannelInstance* channelInstance = m_audioSystem->GetChannelInstance(fadingChannel.channel);
			if (!channelInstance)
			{
				fadingChannel.markedForRemoval = true;
				continue;
			}

			// Lerp volume toward target
			if (fadingChannel.currentVolume < fadingChannel.targetVolume)
			{
				fadingChannel.currentVolume += fadingChannel.fadeSpeed * deltaTime;
				if (fadingChannel.currentVolume >= fadingChannel.targetVolume)
				{
					fadingChannel.currentVolume = fadingChannel.targetVolume;
				}
			}
			else if (fadingChannel.currentVolume > fadingChannel.targetVolume)
			{
				fadingChannel.currentVolume -= fadingChannel.fadeSpeed * deltaTime;
				if (fadingChannel.currentVolume <= fadingChannel.targetVolume)
				{
					fadingChannel.currentVolume = fadingChannel.targetVolume;
				}
			}

			channelInstance->SetVolume(fadingChannel.currentVolume);

			// If faded out completely, stop the sound and mark for removal
			if (fadingChannel.currentVolume <= 0.0f && fadingChannel.targetVolume <= 0.0f)
			{
				m_audioSystem->StopSound(&fadingChannel.channel);
				fadingChannel.markedForRemoval = true;
			}
		}

		// Remove channels that are done
		m_fadingChannels.erase(
			std::remove_if(m_fadingChannels.begin(), m_fadingChannels.end(),
				[](const FadingChannel& fc) { return fc.markedForRemoval; }),
			m_fadingChannels.end());
	}

	void SpellVisualizationPreview::StopAllSounds()
	{
		if (!m_audioSystem)
		{
			return;
		}

		for (auto& fadingChannel : m_fadingChannels)
		{
			if (fadingChannel.channel != InvalidChannel)
			{
				m_audioSystem->StopSound(&fadingChannel.channel);
			}
		}
		m_fadingChannels.clear();
	}

	Vector3 SpellVisualizationPreview::GetBoneWorldPosition(Entity* entity, SceneNode* entityNode, const String& boneName, const Vector3& fallbackOffset)
	{
		if (entity && entity->HasSkeleton() && !boneName.empty())
		{
			auto skeleton = entity->GetSkeleton();
			if (skeleton && skeleton->HasBone(boneName))
			{
				Bone* bone = skeleton->GetBone(boneName);
				if (bone)
				{
					// The bone's derived position is in skeleton-local space.
					// Transform it by the entity's parent node to get world space.
					const Vector3 boneLocalPos = bone->GetDerivedPosition();
					if (entityNode)
					{
						return entityNode->GetDerivedPosition() +
							entityNode->GetDerivedOrientation() * (entityNode->GetDerivedScale() * boneLocalPos);
					}

					return boneLocalPos;
				}
			}
		}

		// Fallback: entity position + offset
		if (entityNode)
		{
			return entityNode->GetDerivedPosition() + fallbackOffset;
		}

		return fallbackOffset;
	}

	void SpellVisualizationPreview::SpawnKitParticles(const proto::SpellKit& kit, Entity* entity, SceneNode* entityNode)
	{
		for (int i = 0; i < kit.particles_size(); ++i)
		{
			const auto& particlePath = kit.particles(i);
			if (particlePath.empty())
			{
				continue;
			}

			try
			{
				// Create a unique name for this emitter
				const String emitterName = "KitParticle_" + std::to_string(m_effectCounter++);
				ParticleEmitter* emitter = m_scene.CreateParticleEmitter(emitterName);
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

				// Attach to bone if specified, otherwise attach to a child scene node
				if (kit.has_attach_bone() && !kit.attach_bone().empty() && entity && entity->HasSkeleton())
				{
					auto skeleton = entity->GetSkeleton();
					if (skeleton && skeleton->HasBone(kit.attach_bone()))
					{
						entity->AttachObjectToBone(kit.attach_bone(), *emitter);
					}
					else
					{
						// Bone not found, attach to entity node
						SceneNode* childNode = entityNode->CreateChildSceneNode(emitterName + "_Node");
						childNode->AttachObject(*emitter);
						m_kitEffectNodes.push_back(childNode);
					}
				}
				else
				{
					// No bone specified, attach to a child scene node at entity position
					SceneNode* childNode = entityNode->CreateChildSceneNode(emitterName + "_Node");
					childNode->AttachObject(*emitter);
					m_kitEffectNodes.push_back(childNode);
				}

				emitter->Play();
				m_kitParticles.push_back(emitter);
			}
			catch (const std::exception& e)
			{
				ELOG("Failed to spawn kit particle '" << particlePath << "': " << e.what());
			}
		}
	}

	void SpellVisualizationPreview::SpawnKitLight(const proto::SpellKit& kit, Entity* entity, SceneNode* entityNode)
	{
		if (!kit.has_light())
		{
			return;
		}

		try
		{
			const auto& lightConfig = kit.light();
			const String lightName = "KitLight_" + std::to_string(m_effectCounter++);

			Light& light = m_scene.CreateLight(lightName, LightType::Point);
			light.SetColor(Vector4(lightConfig.r(), lightConfig.g(), lightConfig.b(), 1.0f));
			light.SetRange(lightConfig.range());

			// Set up fading
			const float fadeInTime = lightConfig.fade_in_time();
			const float fadeOutTime = lightConfig.fade_out_time();
			const float fullIntensity = lightConfig.intensity();

			// Start at zero intensity for fade-in (or full if no fade)
			light.SetIntensity(fadeInTime > 0.0f ? 0.0f : fullIntensity);

			// Attach to bone if specified
			if (kit.has_attach_bone() && !kit.attach_bone().empty() && entity && entity->HasSkeleton())
			{
				auto skeleton = entity->GetSkeleton();
				if (skeleton && skeleton->HasBone(kit.attach_bone()))
				{
					entity->AttachObjectToBone(kit.attach_bone(), light);
				}
				else
				{
					SceneNode* childNode = entityNode->CreateChildSceneNode(lightName + "_Node");
					childNode->AttachObject(light);
					m_kitEffectNodes.push_back(childNode);
				}
			}
			else
			{
				SceneNode* childNode = entityNode->CreateChildSceneNode(lightName + "_Node");
				childNode->AttachObject(light);
				m_kitEffectNodes.push_back(childNode);
			}

			FadingLight fadingLight;
			fadingLight.light = &light;
			fadingLight.currentIntensity = fadeInTime > 0.0f ? 0.0f : fullIntensity;
			fadingLight.targetIntensity = fullIntensity;
			fadingLight.fadeInSpeed = fadeInTime > 0.0f ? (fullIntensity / fadeInTime) : 0.0f;
			fadingLight.fadeOutSpeed = fadeOutTime > 0.0f ? (fullIntensity / fadeOutTime) : 0.0f;
			fadingLight.fadingOut = false;
			m_fadingLights.push_back(fadingLight);
		}
		catch (const std::exception& e)
		{
			ELOG("Failed to spawn kit light: " << e.what());
		}
	}

	void SpellVisualizationPreview::SpawnKitRibbonTrail(const proto::SpellKit& kit, Entity* entity, SceneNode* entityNode)
	{
		if (!kit.has_ribbon_trail())
		{
			return;
		}

		try
		{
			const auto& trailConfig = kit.ribbon_trail();
			const String trailName = "KitRibbon_" + std::to_string(m_effectCounter++);

			RibbonTrail* trail = m_scene.CreateRibbonTrail(trailName);
			if (!trail)
			{
				return;
			}

			// Configure ribbon trail parameters
			RibbonTrailParameters params;
			params.initialWidth = trailConfig.initial_width();
			params.finalWidth = trailConfig.final_width();
			params.initialColor = Vector4(trailConfig.initial_r(), trailConfig.initial_g(), trailConfig.initial_b(), trailConfig.initial_a());
			params.finalColor = Vector4(trailConfig.final_r(), trailConfig.final_g(), trailConfig.final_b(), trailConfig.final_a());
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
			if (kit.has_attach_bone() && !kit.attach_bone().empty() && entity && entity->HasSkeleton())
			{
				auto skeleton = entity->GetSkeleton();
				if (skeleton && skeleton->HasBone(kit.attach_bone()))
				{
					entity->AttachObjectToBone(kit.attach_bone(), *trail);
				}
				else
				{
					SceneNode* childNode = entityNode->CreateChildSceneNode(trailName + "_Node");
					childNode->AttachObject(*trail);
					m_kitEffectNodes.push_back(childNode);
				}
			}
			else
			{
				SceneNode* childNode = entityNode->CreateChildSceneNode(trailName + "_Node");
				childNode->AttachObject(*trail);
				m_kitEffectNodes.push_back(childNode);
			}

			trail->Play();
			m_kitRibbonTrails.push_back(trail);
		}
		catch (const std::exception& e)
		{
			ELOG("Failed to spawn kit ribbon trail: " << e.what());
		}
	}

	void SpellVisualizationPreview::SpawnImpactParticle(const String& particleName, const Vector3& position)
	{
		if (particleName.empty())
		{
			return;
		}

		try
		{
			const String emitterName = "ImpactParticle_" + std::to_string(m_effectCounter++);
			ParticleEmitter* emitter = m_scene.CreateParticleEmitter(emitterName);
			if (!emitter)
			{
				return;
			}

			// Load particle parameters from .hpfx file
			const auto file = AssetRegistry::OpenFile(particleName);
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

			// Create a scene node at the impact position
			SceneNode* impactNode = m_scene.GetRootSceneNode().CreateChildSceneNode(emitterName + "_Node", position);
			impactNode->AttachObject(*emitter);

			emitter->Play();

			// Store for cleanup
			m_kitParticles.push_back(emitter);
			m_kitEffectNodes.push_back(impactNode);
		}
		catch (const std::exception& e)
		{
			ELOG("Failed to spawn impact particle '" << particleName << "': " << e.what());
		}
	}
}
