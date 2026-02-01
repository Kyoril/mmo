// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spell_effect_editor_instance.h"

#include <imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "spell_effect_editor.h"
#include "editor_host.h"
#include "assets/asset_registry.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/light.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/scene_node.h"
#include "binary_io/stream_source.h"
#include "binary_io/stream_sink.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo
{
	SpellEffectEditorInstance::SpellEffectEditorInstance(
		EditorHost& host, 
		SpellEffectEditor& editor, 
		PreviewProviderManager& previewManager,
		Path asset)
		: EditorInstance(host, std::move(asset))
		, m_editor(editor)
		, m_previewManager(previewManager)
	{
		m_lastUpdateTime = std::chrono::high_resolution_clock::now();
		
		// Set up scene camera
		m_cameraAnchor = &m_scene.CreateSceneNode("CameraAnchor");
		m_cameraNode = &m_scene.CreateSceneNode("CameraNode");
		m_cameraAnchor->AddChild(*m_cameraNode);
		m_camera = m_scene.CreateCamera("Camera");
		m_cameraNode->AttachObject(*m_camera);
		m_cameraNode->SetPosition(Vector3(0.0f, 5.0f, 15.0f));
		m_cameraAnchor->SetOrientation(Quaternion(Degree(-15.0f), Vector3::UnitX));
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

		// Initialize ribbon trail parameters
		m_ribbonParams.maxSegments = 32;
		m_ribbonParams.segmentInterval = 0.02f;
		m_ribbonParams.segmentLifetime = 0.4f;
		m_ribbonParams.initialWidth = 0.8f;
		m_ribbonParams.finalWidth = 0.0f;
		m_ribbonParams.initialColor = Vector4(0.3f, 0.7f, 1.0f, 1.0f);
		m_ribbonParams.finalColor = Vector4(0.1f, 0.3f, 0.8f, 0.0f);
		m_ribbonParams.faceCamera = true;

		// Load preview configuration
		LoadPreviewConfig();

		m_renderConnection = m_editor.GetHost().beforeUiUpdate.connect(this, &SpellEffectEditorInstance::Render);
	}

	SpellEffectEditorInstance::~SpellEffectEditorInstance()
	{
		// Clean up spell effect objects
		if (m_projectileTrail)
		{
			m_scene.DestroyRibbonTrail(*m_projectileTrail);
			m_projectileTrail = nullptr;
		}

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

		// Clean up character entities
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

	void SpellEffectEditorInstance::Render()
	{
		if (!m_viewportRT)
		{
			return;
		}

		if (m_lastAvailViewportSize.x <= 0.0f || m_lastAvailViewportSize.y <= 0.0f)
		{
			return;
		}

		// Calculate delta time
		const auto now = std::chrono::high_resolution_clock::now();
		const float deltaTime = std::chrono::duration<float>(now - m_lastUpdateTime).count();
		m_lastUpdateTime = now;

		// Update animations
		if (m_casterAnimState && m_isPlaying)
		{
			m_casterAnimState->AddTime(deltaTime);
		}

		if (m_targetAnimState)
		{
			m_targetAnimState->AddTime(deltaTime);
		}

		// Update projectile
		if (m_projectileActive)
		{
			UpdateProjectile(deltaTime);
		}

		auto& gx = GraphicsDevice::Get();

		// Render the scene
		gx.Reset();
		gx.SetClearColor(Color(0.15f, 0.18f, 0.22f, 1.0f));
		m_viewportRT->Activate();
		m_viewportRT->Clear(mmo::ClearFlags::All);
		gx.SetViewport(0, 0, m_lastAvailViewportSize.x, m_lastAvailViewportSize.y, 0.0f, 1.0f);
		m_camera->SetAspectRatio(m_lastAvailViewportSize.x / m_lastAvailViewportSize.y);

		gx.SetFillMode(m_wireFrame ? FillMode::Wireframe : FillMode::Solid);

		m_scene.Render(*m_camera, PixelShaderType::Forward);
		
		m_viewportRT->Update();
	}

	void SpellEffectEditorInstance::Draw()
	{
		ImGui::PushID(GetAssetPath().c_str());

		const auto dockSpaceId = ImGui::GetID("##spelleffect_dockspace_");
		ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		const String viewportId = "Viewport##" + GetAssetPath().string();
		const String controlsId = "Controls##" + GetAssetPath().string();
		const String spellId = "Spell Selection##" + GetAssetPath().string();
		const String projectileId = "Projectile##" + GetAssetPath().string();
		const String particleId = "Particles##" + GetAssetPath().string();
		const String ribbonId = "Ribbon Trail##" + GetAssetPath().string();

		// Draw windows
		DrawViewport(viewportId);
		DrawControls(controlsId);
		DrawSpellSelection(spellId);
		DrawProjectileSettings(projectileId);
		DrawParticleSettings(particleId);
		DrawRibbonSettings(ribbonId);

		// Init dock layout
		if (m_initDockLayout)
		{
			ImGui::DockBuilderRemoveNode(dockSpaceId);
			ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(dockSpaceId, ImGui::GetMainViewport()->Size);

			auto mainId = dockSpaceId;
			const auto rightId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Right, 0.3f, nullptr, &mainId);
			auto rightTopId = rightId;
			const auto rightBottomId = ImGui::DockBuilderSplitNode(rightTopId, ImGuiDir_Down, 0.5f, nullptr, &rightTopId);
			
			ImGui::DockBuilderDockWindow(viewportId.c_str(), mainId);
			ImGui::DockBuilderDockWindow(controlsId.c_str(), rightTopId);
			ImGui::DockBuilderDockWindow(spellId.c_str(), rightTopId);
			ImGui::DockBuilderDockWindow(projectileId.c_str(), rightBottomId);
			ImGui::DockBuilderDockWindow(particleId.c_str(), rightBottomId);
			ImGui::DockBuilderDockWindow(ribbonId.c_str(), rightBottomId);

			m_initDockLayout = false;
		}

		ImGui::DockBuilderFinish(dockSpaceId);

		ImGui::PopID();
	}

	void SpellEffectEditorInstance::OnMouseButtonDown(uint32 button, uint16 x, uint16 y)
	{
		if (button == 0)
		{
			m_leftButtonPressed = true;
		}

		if (button == 1)
		{
			m_rightButtonPressed = true;
		}

		if (button == 2)
		{
			m_middleButtonPressed = true;
		}

		m_lastMouseX = x;
		m_lastMouseY = y;
	}

	void SpellEffectEditorInstance::OnMouseButtonUp(uint32 button, uint16 x, uint16 y)
	{
		if (button == 0)
		{
			m_leftButtonPressed = false;
		}

		if (button == 1)
		{
			m_rightButtonPressed = false;
		}

		if (button == 2)
		{
			m_middleButtonPressed = false;
		}
	}

	void SpellEffectEditorInstance::OnMouseMoved(uint16 x, uint16 y)
	{
		const int16 deltaX = x - m_lastMouseX;
		const int16 deltaY = y - m_lastMouseY;

		// Left button: Rotate camera
		if (m_leftButtonPressed && m_cameraAnchor)
		{
			m_cameraAnchor->Yaw(Degree(-deltaX * 0.5f), TransformSpace::World);
			m_cameraAnchor->Pitch(Degree(deltaY * 0.5f), TransformSpace::Local);
		}

		// Right button: Pan camera
		if (m_rightButtonPressed && m_cameraAnchor)
		{
			const Vector3 panOffset = m_cameraNode->GetOrientation() * Vector3(-deltaX * 0.02f, deltaY * 0.02f, 0.0f);
			m_cameraAnchor->Translate(panOffset);
		}

		// Middle button: Zoom camera
		if (m_middleButtonPressed && m_cameraNode)
		{
			const Vector3 currentPos = m_cameraNode->GetPosition();
			const float zoomFactor = 1.0f + (deltaY * 0.01f);
			m_cameraNode->SetPosition(currentPos * zoomFactor);
		}

		m_lastMouseX = x;
		m_lastMouseY = y;
	}

	bool SpellEffectEditorInstance::Save()
	{
		const auto file = AssetRegistry::CreateNewFile(m_assetPath.string());
		if (!file)
		{
			ELOG("Failed to open spell effect preview file " << m_assetPath << " for writing!");
			return false;
		}

		try
		{
			io::StreamSink sink(*file);
			io::Writer writer(sink);

			// Write version
			writer << static_cast<uint32>(1);
			
			// Write visualization ID
			writer << m_visualizationId;
			
			// Write model paths (null-terminated strings)
			writer << io::write_range(m_casterMeshPath) << io::write<uint8>(0);
			writer << io::write_range(m_targetMeshPath) << io::write<uint8>(0);
			writer << io::write_range(m_projectileMeshPath) << io::write<uint8>(0);
			
			// Write particle paths
			writer << io::write_range(m_castParticlePath) << io::write<uint8>(0);
			writer << io::write_range(m_impactParticlePath) << io::write<uint8>(0);
			
			// Write projectile settings
			writer << m_projectileSpeed;
			writer << io::write<uint8>(m_useRibbonTrail ? 1 : 0);
			
			// Write ribbon parameters
			writer << m_ribbonParams.maxSegments;
			writer << m_ribbonParams.segmentInterval;
			writer << m_ribbonParams.segmentLifetime;
			writer << m_ribbonParams.initialWidth;
			writer << m_ribbonParams.finalWidth;
			writer << m_ribbonParams.initialColor.x;
			writer << m_ribbonParams.initialColor.y;
			writer << m_ribbonParams.initialColor.z;
			writer << m_ribbonParams.initialColor.w;
			writer << m_ribbonParams.finalColor.x;
			writer << m_ribbonParams.finalColor.y;
			writer << m_ribbonParams.finalColor.z;
			writer << m_ribbonParams.finalColor.w;
			writer << io::write<uint8>(m_ribbonParams.faceCamera ? 1 : 0);

			ILOG("Saved spell effect preview to " << m_assetPath);
			return true;
		}
		catch (const std::exception& ex)
		{
			ELOG("Failed to save spell effect preview: " << ex.what());
			return false;
		}
	}

	void SpellEffectEditorInstance::DrawViewport(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			// Toolbar
			if (ImGui::Button(m_isPlaying ? "Pause" : "Play"))
			{
				m_isPlaying = !m_isPlaying;
			}
			
			ImGui::SameLine();
			
			if (ImGui::Button("Cast Spell"))
			{
				TriggerSpellCast();
			}
			
			ImGui::SameLine();
			
			if (ImGui::Button("Reset"))
			{
				ResetPreview();
			}
			
			ImGui::SameLine();
			ImGui::Checkbox("Wireframe", &m_wireFrame);
			
			ImGui::SameLine();
			ImGui::Checkbox("Loop", &m_loopAnimation);

			ImGui::Separator();

			// Determine the available size for the viewport window
			const auto availableSpace = ImGui::GetContentRegionAvail();

			if (m_viewportRT == nullptr)
			{
				m_viewportRT = GraphicsDevice::Get().CreateRenderTexture("SpellEffectViewport", 
					std::max(1.0f, availableSpace.x), 
					std::max(1.0f, availableSpace.y), 
					RenderTextureFlags::HasColorBuffer | RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView);
				m_lastAvailViewportSize = availableSpace;
			}
			else if (m_lastAvailViewportSize.x != availableSpace.x || m_lastAvailViewportSize.y != availableSpace.y)
			{
				m_viewportRT->Resize(availableSpace.x, availableSpace.y);
				m_lastAvailViewportSize = availableSpace;
			}

			// Render the render target content into the window as image object
			ImGui::Image(m_viewportRT->GetTextureObject(), availableSpace);
			ImGui::SetItemUsingMouseWheel();

			if (ImGui::IsItemHovered())
			{
				m_cameraNode->Translate(Vector3::UnitZ * ImGui::GetIO().MouseWheel * -0.5f, TransformSpace::Local);
			}

			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				m_leftButtonPressed = true;
			}

			if (ImGui::IsItemClicked(ImGuiMouseButton_Middle))
			{
				m_middleButtonPressed = true;
			}

			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				m_rightButtonPressed = true;
			}
		}
		ImGui::End();
	}

	void SpellEffectEditorInstance::DrawControls(const String& id)
	{
		if (ImGui::Begin(id.c_str(), nullptr, ImGuiWindowFlags_MenuBar))
		{
			// Menu bar
			if (ImGui::BeginMenuBar())
			{
				if (ImGui::BeginMenu("File"))
				{
					if (ImGui::MenuItem("Save", "Ctrl+S"))
					{
						Save();
					}
					ImGui::EndMenu();
				}
				ImGui::EndMenuBar();
			}

			ImGui::Text("Spell Effect Preview Editor");
			ImGui::Separator();

			// Caster model selection
			ImGui::Text("Caster Model:");
			ImGui::InputText("##caster_mesh", &m_casterMeshPath);
			ImGui::SameLine();
			if (ImGui::Button("Load##caster"))
			{
				// Reload caster entity
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

			// Target model selection
			ImGui::Text("Target Model:");
			ImGui::InputText("##target_mesh", &m_targetMeshPath);
			ImGui::SameLine();
			if (ImGui::Button("Load##target"))
			{
				// Reload target entity
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

			ImGui::Separator();
			
			// Animation selection
			ImGui::Text("Cast Animation:");
			ImGui::InputText("##cast_anim", &m_castAnimationName);
			
			ImGui::Text("Target Hit Animation:");
			ImGui::InputText("##hit_anim", &m_targetHitAnimationName);
		}
		ImGui::End();
	}

	void SpellEffectEditorInstance::DrawSpellSelection(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			ImGui::Text("Visualization ID:");
			ImGui::InputScalar("##viz_id", ImGuiDataType_U32, &m_visualizationId);
			
			ImGui::Separator();
			
			ImGui::TextWrapped("Enter a spell visualization ID to preview. "
				"This connects to the data in the SpellVisualization editor window.");
		}
		ImGui::End();
	}

	void SpellEffectEditorInstance::DrawProjectileSettings(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			ImGui::Text("Projectile Settings");
			ImGui::Separator();

			ImGui::DragFloat("Speed", &m_projectileSpeed, 0.5f, 1.0f, 100.0f);
			
			ImGui::Text("Projectile Mesh:");
			ImGui::InputText("##proj_mesh", &m_projectileMeshPath);
			ImGui::SameLine();
			if (ImGui::Button("Load##proj"))
			{
				// Reload projectile entity
				if (m_projectileEntity)
				{
					m_scene.DestroyEntity(*m_projectileEntity);
					m_projectileEntity = nullptr;
				}
				
				if (!m_projectileMeshPath.empty())
				{
					try
					{
						m_projectileEntity = m_scene.CreateEntity("Projectile", m_projectileMeshPath);
						if (m_projectileEntity && m_projectileNode)
						{
							m_projectileNode->AttachObject(*m_projectileEntity);
						}
						m_projectileEntity->SetVisible(false);
					}
					catch (...)
					{
						WLOG("Failed to load projectile mesh: " << m_projectileMeshPath);
					}
				}
			}
		}
		ImGui::End();
	}

	void SpellEffectEditorInstance::DrawParticleSettings(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			ImGui::Text("Particle Settings");
			ImGui::Separator();

			ImGui::Text("Cast Particles:");
			ImGui::InputText("##cast_particles", &m_castParticlePath);
			
			ImGui::Text("Impact Particles:");
			ImGui::InputText("##impact_particles", &m_impactParticlePath);
		}
		ImGui::End();
	}

	void SpellEffectEditorInstance::DrawRibbonSettings(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			ImGui::Text("Ribbon Trail Settings");
			ImGui::Separator();

			ImGui::Checkbox("Enable Ribbon Trail", &m_useRibbonTrail);

			if (m_useRibbonTrail)
			{
				bool changed = false;

				int maxSegments = static_cast<int>(m_ribbonParams.maxSegments);
				if (ImGui::DragInt("Max Segments", &maxSegments, 1, 8, 128))
				{
					m_ribbonParams.maxSegments = static_cast<uint32>(maxSegments);
					changed = true;
				}

				changed |= ImGui::DragFloat("Segment Interval", &m_ribbonParams.segmentInterval, 0.001f, 0.01f, 0.5f);
				changed |= ImGui::DragFloat("Segment Lifetime", &m_ribbonParams.segmentLifetime, 0.01f, 0.1f, 5.0f);
				changed |= ImGui::DragFloat("Initial Width", &m_ribbonParams.initialWidth, 0.05f, 0.1f, 5.0f);
				changed |= ImGui::DragFloat("Final Width", &m_ribbonParams.finalWidth, 0.05f, 0.0f, 5.0f);
				
				changed |= ImGui::ColorEdit4("Initial Color", &m_ribbonParams.initialColor.x);
				changed |= ImGui::ColorEdit4("Final Color", &m_ribbonParams.finalColor.x);
				
				changed |= ImGui::Checkbox("Face Camera", &m_ribbonParams.faceCamera);

				// Apply changes to ribbon trail if it exists
				if (changed && m_projectileTrail)
				{
					m_projectileTrail->SetParameters(m_ribbonParams);
				}
			}
		}
		ImGui::End();
	}

	bool SpellEffectEditorInstance::LoadPreviewConfig()
	{
		const auto file = AssetRegistry::OpenFile(m_assetPath.string());
		if (!file)
		{
			WLOG("Failed to open spell effect preview file: " << m_assetPath);
			return false;
		}

		try
		{
			io::StreamSource source(*file);
			io::Reader reader(source);

			// Read version
			uint32 version;
			reader >> version;

			if (version >= 1)
			{
				uint8 useRibbonTrailVal = 0;
				uint8 faceCameraVal = 0;

				reader >> m_visualizationId;
				reader >> io::read_string(m_casterMeshPath);
				reader >> io::read_string(m_targetMeshPath);
				reader >> io::read_string(m_projectileMeshPath);
				reader >> io::read_string(m_castParticlePath);
				reader >> io::read_string(m_impactParticlePath);
				reader >> m_projectileSpeed;
				reader >> useRibbonTrailVal;
				m_useRibbonTrail = (useRibbonTrailVal != 0);

				// Read ribbon parameters
				reader >> m_ribbonParams.maxSegments;
				reader >> m_ribbonParams.segmentInterval;
				reader >> m_ribbonParams.segmentLifetime;
				reader >> m_ribbonParams.initialWidth;
				reader >> m_ribbonParams.finalWidth;
				reader >> m_ribbonParams.initialColor.x;
				reader >> m_ribbonParams.initialColor.y;
				reader >> m_ribbonParams.initialColor.z;
				reader >> m_ribbonParams.initialColor.w;
				reader >> m_ribbonParams.finalColor.x;
				reader >> m_ribbonParams.finalColor.y;
				reader >> m_ribbonParams.finalColor.z;
				reader >> m_ribbonParams.finalColor.w;
				reader >> faceCameraVal;
				m_ribbonParams.faceCamera = (faceCameraVal != 0);
			}

			return true;
		}
		catch (const std::exception& ex)
		{
			WLOG("Failed to load spell effect preview config: " << ex.what());
			return false;
		}
	}

	void SpellEffectEditorInstance::CreateFloorPlane()
	{
		m_floorNode = m_scene.GetRootSceneNode().CreateChildSceneNode("FloorNode");
		m_floorNode->SetPosition(Vector3(0.0f, 0.0f, 0.0f));
		m_floorNode->SetScale(Vector3(20.0f, 1.0f, 20.0f));

		// Try to create a simple floor mesh or use default plane
		try
		{
			m_floorEntity = m_scene.CreateEntity("Floor", "Editor/Plane.hmsh");
			if (m_floorEntity)
			{
				m_floorNode->AttachObject(*m_floorEntity);
				
				// Apply default material
				auto defaultMat = MaterialManager::Get().Load("Models/Default.hmat");
				if (defaultMat)
				{
					m_floorEntity->SetMaterial(defaultMat);
				}
			}
		}
		catch (...)
		{
			WLOG("Could not load floor plane mesh, using grid only");
		}
	}

	void SpellEffectEditorInstance::CreateCharacterEntities()
	{
		// Create caster node and entity (positioned to the left)
		m_casterNode = m_scene.GetRootSceneNode().CreateChildSceneNode("CasterNode");
		m_casterNode->SetPosition(Vector3(-5.0f, 0.0f, 0.0f));
		m_casterNode->Yaw(Degree(90.0f), TransformSpace::Local);

		try
		{
			m_casterEntity = m_scene.CreateEntity("Caster", m_casterMeshPath);
			if (m_casterEntity)
			{
				m_casterNode->AttachObject(*m_casterEntity);
				
				// Try to set up idle animation
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
		m_targetNode->SetPosition(Vector3(5.0f, 0.0f, 0.0f));
		m_targetNode->Yaw(Degree(-90.0f), TransformSpace::Local);

		try
		{
			m_targetEntity = m_scene.CreateEntity("Target", m_targetMeshPath);
			if (m_targetEntity)
			{
				m_targetNode->AttachObject(*m_targetEntity);
				
				// Try to set up idle animation
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
		m_projectileNode->SetPosition(Vector3(0.0f, -1000.0f, 0.0f));  // Hidden below ground

		// Create ribbon trail for projectile
		m_projectileTrail = m_scene.CreateRibbonTrail("ProjectileTrail");
		if (m_projectileTrail)
		{
			m_projectileTrail->SetParameters(m_ribbonParams);
			m_projectileNode->AttachObject(*m_projectileTrail);
			
			// Try to get a default material
			auto trailMat = RibbonTrail::GetDefaultMaterial(true);
			if (trailMat)
			{
				m_projectileTrail->SetMaterial(trailMat);
			}
			else
			{
				// Fallback to particle material
				trailMat = ParticleEmitter::GetDefaultMaterial(true);
				if (trailMat)
				{
					m_projectileTrail->SetMaterial(trailMat);
				}
			}
		}
	}

	void SpellEffectEditorInstance::TriggerSpellCast()
	{
		// Reset any existing projectile
		ResetPreview();

		// Trigger cast animation on caster
		if (m_casterEntity && !m_castAnimationName.empty())
		{
			if (m_casterEntity->HasAnimationState(m_castAnimationName))
			{
				m_casterAnimState = m_casterEntity->GetAnimationState(m_castAnimationName);
				m_casterAnimState->SetEnabled(true);
				m_casterAnimState->SetLoop(m_loopAnimation);
				m_casterAnimState->SetTimePosition(0.0f);
			}
		}

		// Start projectile
		if (m_casterNode && m_targetNode)
		{
			m_projectileStartPos = m_casterNode->GetDerivedPosition() + Vector3(0.0f, 1.5f, 0.0f);
			m_projectileTargetPos = m_targetNode->GetDerivedPosition() + Vector3(0.0f, 1.0f, 0.0f);
			m_projectileProgress = 0.0f;
			m_projectileActive = true;

			// Position projectile at start
			m_projectileNode->SetPosition(m_projectileStartPos);
			
			// Show projectile entity
			if (m_projectileEntity)
			{
				m_projectileEntity->SetVisible(true);
			}

			// Start ribbon trail
			if (m_projectileTrail && m_useRibbonTrail)
			{
				m_projectileTrail->Reset();
				m_projectileTrail->Play();
			}

			// Play cast particles
			if (m_castParticles)
			{
				m_castParticles->Reset();
				m_castParticles->Play();
			}
		}

		m_isPlaying = true;
	}

	void SpellEffectEditorInstance::UpdateProjectile(float deltaTime)
	{
		if (!m_projectileActive)
		{
			return;
		}

		// Calculate distance and travel time
		const Vector3 direction = m_projectileTargetPos - m_projectileStartPos;
		const float totalDistance = direction.GetLength();
		const float travelTime = totalDistance / m_projectileSpeed;

		// Update progress
		m_projectileProgress += deltaTime / travelTime;

		if (m_projectileProgress >= 1.0f)
		{
			// Projectile reached target
			m_projectileProgress = 1.0f;
			m_projectileActive = false;

			// Stop ribbon trail
			if (m_projectileTrail)
			{
				m_projectileTrail->Stop();
			}

			// Hide projectile entity
			if (m_projectileEntity)
			{
				m_projectileEntity->SetVisible(false);
			}

			// Trigger impact on target
			if (m_targetEntity && !m_targetHitAnimationName.empty())
			{
				if (m_targetEntity->HasAnimationState(m_targetHitAnimationName))
				{
					m_targetAnimState = m_targetEntity->GetAnimationState(m_targetHitAnimationName);
					m_targetAnimState->SetEnabled(true);
					m_targetAnimState->SetLoop(false);
					m_targetAnimState->SetTimePosition(0.0f);
				}
			}

			// Play impact particles
			if (m_impactParticles)
			{
				m_impactParticles->Reset();
				m_impactParticles->Play();
			}

			// Loop if enabled
			if (m_loopAnimation)
			{
				// Reset after a short delay (simulated with next frame)
				TriggerSpellCast();
			}

			return;
		}

		// Interpolate position (with slight arc)
		const float t = m_projectileProgress;
		Vector3 currentPos = m_projectileStartPos + direction * t;
		
		// Add a parabolic arc
		const float arcHeight = 1.0f;
		currentPos.y += ::sin(t * 3.14159f) * arcHeight;

		m_projectileNode->SetPosition(currentPos);

		// Orient projectile towards target
		const Vector3 velocity = (m_projectileTargetPos - currentPos);
		if (velocity.GetSquaredLength() > 0.001f)
		{
			const Vector3 normalizedVel = velocity.NormalizedCopy();
			m_projectileNode->SetDirection(normalizedVel, TransformSpace::World, Vector3::NegativeUnitZ);
		}
	}

	void SpellEffectEditorInstance::ResetPreview()
	{
		m_projectileActive = false;
		m_projectileProgress = 0.0f;

		// Reset projectile position
		if (m_projectileNode)
		{
			m_projectileNode->SetPosition(Vector3(0.0f, -1000.0f, 0.0f));
		}

		// Hide projectile entity
		if (m_projectileEntity)
		{
			m_projectileEntity->SetVisible(false);
		}

		// Stop and reset ribbon trail
		if (m_projectileTrail)
		{
			m_projectileTrail->Stop();
			m_projectileTrail->Reset();
		}

		// Reset particles
		if (m_castParticles)
		{
			m_castParticles->Stop();
			m_castParticles->Reset();
		}

		if (m_impactParticles)
		{
			m_impactParticles->Stop();
			m_impactParticles->Reset();
		}

		// Reset caster animation to idle
		if (m_casterEntity && m_casterEntity->HasAnimationState("Idle"))
		{
			m_casterAnimState = m_casterEntity->GetAnimationState("Idle");
			m_casterAnimState->SetEnabled(true);
			m_casterAnimState->SetLoop(true);
		}

		// Reset target animation to idle
		if (m_targetEntity && m_targetEntity->HasAnimationState("Idle"))
		{
			m_targetAnimState = m_targetEntity->GetAnimationState("Idle");
			m_targetAnimState->SetEnabled(true);
			m_targetAnimState->SetLoop(true);
		}
	}
}
