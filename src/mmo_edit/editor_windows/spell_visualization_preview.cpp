// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spell_visualization_preview.h"
#include "editor_host.h"

#include "audio/audio.h"
#include "graphics/graphics_device.h"
#include "scene_graph/camera.h"
#include "scene_graph/light.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/scene_node.h"
#include "log/default_log_levels.h"

#include "proto_data/project.h"

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

		// Connect to update signal
		m_updateConnection = m_host.beforeUiUpdate.connect(this, &SpellVisualizationPreview::Update);
	}

	SpellVisualizationPreview::~SpellVisualizationPreview()
	{
		// Clear audio system reference first to prevent StopAllSounds from accessing destroyed audio
		m_audioSystem = nullptr;
		m_activeChannels.clear();

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
		m_scene.Clear();
	}

	void SpellVisualizationPreview::Update()
	{
		if (!m_viewportRT || m_viewportSize.x <= 0.0f || m_viewportSize.y <= 0.0f)
		{
			return;
		}

		// Calculate delta time
		const auto now = std::chrono::high_resolution_clock::now();
		const float deltaTime = std::chrono::duration<float>(now - m_lastUpdateTime).count();
		m_lastUpdateTime = now;

		// Update animations
		UpdateAnimations(deltaTime);

		// Update projectile
		if (m_projectileActive)
		{
			UpdateProjectile(deltaTime);
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
				// Start projectile and wait for impact
				if (!m_projectileActive && m_sequenceTimer < 0.1f)
				{
					StartProjectile();
				}
				// When projectile reaches target, trigger impact
				if (!m_projectileActive && m_sequenceTimer >= 0.1f)
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
						TriggerEvent(PreviewEvent::StartCast);
					}
					else
					{
						m_castSequenceActive = false;
					}
				}
				break;

			default:
				break;
			}
		}

		// Render the scene
		auto& gx = GraphicsDevice::Get();

		gx.Reset();
		gx.SetClearColor(Color(0.12f, 0.14f, 0.18f, 1.0f));
		m_viewportRT->Activate();
		m_viewportRT->Clear(ClearFlags::All);
		gx.SetViewport(0, 0, static_cast<int>(m_viewportSize.x), static_cast<int>(m_viewportSize.y), 0.0f, 1.0f);
		m_camera->SetAspectRatio(m_viewportSize.x / m_viewportSize.y);

		gx.SetFillMode(m_wireFrame ? FillMode::Wireframe : FillMode::Solid);

		m_scene.Render(*m_camera, PixelShaderType::Forward);

		m_viewportRT->Update();
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

			// Create or resize render texture
			if (m_viewportRT == nullptr && availableSpace.x > 0 && availableSpace.y > 0)
			{
				m_viewportRT = GraphicsDevice::Get().CreateRenderTexture("SpellVizPreview",
					static_cast<uint16>(availableSpace.x),
					static_cast<uint16>(availableSpace.y),
					RenderTextureFlags::HasColorBuffer | RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView);
				m_viewportSize = availableSpace;
			}
			else if (m_viewportRT && (m_viewportSize.x != availableSpace.x || m_viewportSize.y != availableSpace.y))
			{
				if (availableSpace.x > 0 && availableSpace.y > 0)
				{
					m_viewportRT->Resize(static_cast<uint16>(availableSpace.x), static_cast<uint16>(availableSpace.y));
					m_viewportSize = availableSpace;
				}
			}

			// Render the viewport image
			if (m_viewportRT)
			{
				ImGui::Image(m_viewportRT->GetTextureObject(), availableSpace);
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
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 0.8f));
		if (ImGui::Button("Play", ImVec2(50, 0)))
		{
			TriggerFullCastSequence();
		}
		ImGui::PopStyleColor();

		ImGui::SameLine();

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 0.8f));
		if (ImGui::Button("Stop", ImVec2(50, 0)))
		{
			StopPreview();
		}
		ImGui::PopStyleColor();

		ImGui::SameLine();
		ImGui::Checkbox("Loop", &m_loopSequence);

		ImGui::SameLine();
		ImGui::Checkbox("Wire", &m_wireFrame);

		ImGui::SameLine();
		ImGui::SetNextItemWidth(70);
		ImGui::DragFloat("##speed", &m_projectileSpeed, 0.5f, 1.0f, 50.0f, "%.0f spd");
		
		ImGui::SameLine();
		ImGui::SetNextItemWidth(70);
		ImGui::DragFloat("##cast", &m_castDuration, 0.1f, 0.5f, 5.0f, "%.1fs");

		// Row 2: Individual event triggers
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.4f, 0.6f, 0.8f));
		if (ImGui::SmallButton("Start"))
		{
			TriggerEvent(PreviewEvent::StartCast);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Casting"))
		{
			TriggerEvent(PreviewEvent::Casting);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("OK"))
		{
			TriggerEvent(PreviewEvent::CastSucceeded);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Impact"))
		{
			TriggerEvent(PreviewEvent::Impact);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Cancel"))
		{
			TriggerEvent(PreviewEvent::CancelCast);
		}
		ImGui::PopStyleColor();
	}

	void SpellVisualizationPreview::TriggerEvent(PreviewEvent event)
	{
		if (!m_currentVisualization)
		{
			return;
		}

		// Stop sounds from previous event before playing new ones
		StopAllSounds();

		// Map PreviewEvent to proto::SpellVisualEvent
		uint32 protoEvent = 0;
		switch (event)
		{
		case PreviewEvent::StartCast:
			protoEvent = 0; // START_CAST
			break;
		case PreviewEvent::CancelCast:
			protoEvent = 1; // CANCEL_CAST
			break;
		case PreviewEvent::Casting:
			protoEvent = 2; // CASTING
			break;
		case PreviewEvent::CastSucceeded:
			protoEvent = 3; // CAST_SUCCEEDED
			StartProjectile();
			break;
		case PreviewEvent::Impact:
			protoEvent = 4; // IMPACT
			break;
		}

		// Apply the kits for this event
		ApplyEventKits(m_currentVisualization, protoEvent);
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
		m_projectileActive = false;
		m_sequenceTimer = 0.0f;

		// Stop all sounds
		StopAllSounds();

		// Reset projectile position
		if (m_projectileNode)
		{
			m_projectileNode->SetPosition(Vector3(0.0f, -1000.0f, 0.0f));
		}

		if (m_projectileEntity)
		{
			m_projectileEntity->SetVisible(false);
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

		// Sync projectile settings if available
		if (visualization.has_projectile())
		{
			const auto& projectile = visualization.projectile();
			
			// Update projectile mesh if specified
			if (projectile.has_mesh_name() && !projectile.mesh_name().empty())
			{
				// Only recreate if mesh path changed
				if (m_projectileMeshPath != projectile.mesh_name())
				{
					m_projectileMeshPath = projectile.mesh_name();

					// Use existing projectile node from CreateCharacterEntities
					if (m_projectileNode == nullptr)
					{
						m_projectileNode = m_scene.GetRootSceneNode().CreateChildSceneNode("ProjectileNode");
						m_projectileNode->SetPosition(Vector3(0.0f, -1000.0f, 0.0f));
					}

					try
					{
						if (m_projectileEntity)
						{
							m_projectileNode->DetachObject(*m_projectileEntity);
							m_scene.DestroyEntity(*m_projectileEntity);
							m_projectileEntity = nullptr;
						}
						
						m_projectileEntity = m_scene.CreateEntity("Projectile_" + std::to_string(visualization.id()), projectile.mesh_name());
						if (m_projectileEntity)
						{
							m_projectileNode->AttachObject(*m_projectileEntity);
							m_projectileEntity->SetVisible(false);
						}
					}
					catch (...)
					{
						WLOG("Failed to load projectile mesh: " << projectile.mesh_name());
					}
				}

				// Always update scale
				if (m_projectileNode)
				{
					if (projectile.has_scale() && projectile.scale() > 0.0f)
					{
						const float scale = projectile.scale();
						m_projectileNode->SetScale(Vector3(scale, scale, scale));
					}
					else
					{
						m_projectileNode->SetScale(Vector3(1.0f, 1.0f, 1.0f));
					}
				}
			}
		}
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

		// Create projectile node (hidden initially)
		m_projectileNode = m_scene.GetRootSceneNode().CreateChildSceneNode("ProjectileNode");
		m_projectileNode->SetPosition(Vector3(0.0f, -1000.0f, 0.0f));
		m_projectileNode->SetScale(Vector3(0.3f, 0.3f, 0.3f));

		// Create default projectile entity (a simple sphere)
		try
		{
			m_projectileEntity = m_scene.CreateEntity("ProjectileDefault", "Editor/Sphere.hmsh");
			if (m_projectileEntity)
			{
				m_projectileNode->AttachObject(*m_projectileEntity);
				m_projectileEntity->SetVisible(false);
			}
		}
		catch (...)
		{
			WLOG("Failed to create default projectile entity");
		}
	}

	void SpellVisualizationPreview::UpdateProjectile(float deltaTime)
	{
		if (!m_projectileActive)
		{
			return;
		}

		const Vector3 direction = m_projectileTargetPos - m_projectileStartPos;
		const float totalDistance = direction.GetLength();
		const float travelTime = totalDistance / m_projectileSpeed;

		m_projectileProgress += deltaTime / travelTime;

		if (m_projectileProgress >= 1.0f)
		{
			m_projectileProgress = 1.0f;
			m_projectileActive = false;

			if (m_projectileEntity)
			{
				m_projectileEntity->SetVisible(false);
			}

			return;
		}

		// Interpolate position with arc
		const float t = m_projectileProgress;
		Vector3 currentPos = m_projectileStartPos + direction * t;

		// Add parabolic arc
		const float arcHeight = 0.8f;
		currentPos.y += ::sin(t * 3.14159f) * arcHeight;

		m_projectileNode->SetPosition(currentPos);

		// Orient towards target
		const Vector3 velocity = (m_projectileTargetPos - currentPos);
		if (velocity.GetSquaredLength() > 0.001f)
		{
			m_projectileNode->SetDirection(velocity.NormalizedCopy(), TransformSpace::World, Vector3::NegativeUnitZ);
		}
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
		if (!m_casterNode || !m_targetNode || !m_projectileNode)
		{
			return;
		}

		m_projectileStartPos = m_casterNode->GetDerivedPosition() + Vector3(0.0f, 1.2f, 0.0f);
		m_projectileTargetPos = m_targetNode->GetDerivedPosition() + Vector3(0.0f, 0.8f, 0.0f);
		m_projectileProgress = 0.0f;
		m_projectileActive = true;

		m_projectileNode->SetPosition(m_projectileStartPos);

		if (m_projectileEntity)
		{
			m_projectileEntity->SetVisible(true);
		}
	}

	void SpellVisualizationPreview::CleanupSpellEffects()
	{
		StopAllSounds();

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

		if (m_projectileEntity)
		{
			m_scene.DestroyEntity(*m_projectileEntity);
			m_projectileEntity = nullptr;
		}
	}

	void SpellVisualizationPreview::ApplyEventKits(proto::SpellVisualization* visualization, uint32 eventValue)
	{
		if (!visualization)
		{
			return;
		}

		const auto& kitsMap = visualization->kits_by_event();
		auto it = kitsMap.find(eventValue);
		if (it == kitsMap.end())
		{
			return;
		}

		const auto& kitList = it->second;

		for (const auto& kit : kitList.kits())
		{
			// Apply animation based on scope
			if (kit.has_animation_name() && !kit.animation_name().empty())
			{
				Entity* targetEntity = nullptr;
				AnimationState** animStatePtr = nullptr;

				switch (kit.scope())
				{
				case proto::CASTER:
					targetEntity = m_casterEntity;
					animStatePtr = &m_casterAnimState;
					break;
				case proto::TARGET:
				case proto::PROJECTILE_IMPACT:
					targetEntity = m_targetEntity;
					animStatePtr = &m_targetAnimState;
					break;
				default:
					break;
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
				
				// Track the channel so we can stop it later
				if (channelIndex != InvalidChannel)
				{
					m_activeChannels.push_back(channelIndex);
				}
			}
		}
		catch (...)
		{
			WLOG("Failed to play sound: " << soundPath);
		}
	}

	void SpellVisualizationPreview::StopAllSounds()
	{
		if (!m_audioSystem)
		{
			return;
		}

		for (auto& channel : m_activeChannels)
		{
			m_audioSystem->StopSound(&channel);
		}
		m_activeChannels.clear();
	}
}
