// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "particle_system_editor_instance.h"

#include <imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "particle_system_editor.h"
#include "editor_host.h"
#include "assets/asset_registry.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/scene_node.h"
#include "binary_io/stream_source.h"
#include "binary_io/stream_sink.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo
{
	ParticleSystemEditorInstance::ParticleSystemEditorInstance(EditorHost& host, ParticleSystemEditor& editor, Path asset)
		: EditorInstance(host, std::move(asset))
		, m_editor(editor)
	{
		// Set up scene camera
		m_cameraAnchor = &m_scene.CreateSceneNode("CameraAnchor");
		m_cameraNode = &m_scene.CreateSceneNode("CameraNode");
		m_cameraAnchor->AddChild(*m_cameraNode);
		m_camera = m_scene.CreateCamera("Camera");
		m_cameraNode->AttachObject(*m_camera);
		m_cameraNode->SetPosition(Vector3::UnitZ * 15.0f);
		m_cameraAnchor->SetOrientation(Quaternion(Degree(-25.0f), Vector3::UnitX));

		m_scene.GetRootSceneNode().AddChild(*m_cameraAnchor);

		// Create grid and axis display
		m_worldGrid = std::make_unique<WorldGrid>(m_scene, "WorldGrid");
		m_axisDisplay = std::make_unique<AxisDisplay>(m_scene, "DebugAxis");
		m_scene.GetRootSceneNode().AddChild(m_axisDisplay->GetSceneNode());

		// Load particle system parameters
		if (!LoadParticleSystem())
		{
			WLOG("Failed to load particle system from " << m_assetPath);
			// Use default parameters
		}

		// Create particle emitter
		m_emitter = m_scene.CreateParticleEmitter("ParticleEmitter");
		if (m_emitter)
		{
			m_emitter->SetParameters(m_parameters);
			m_scene.GetRootSceneNode().AttachObject(*m_emitter);
			
			// Try to load the material
			if (!m_parameters.materialName.empty())
			{
				try
				{
					auto material = MaterialManager::Get().Load(m_parameters.materialName);
					m_emitter->SetMaterial(material);
				}
				catch (...)
				{
					WLOG("Failed to load material: " << m_parameters.materialName);
				}
			}
			
			m_emitter->Play();
		}

		m_renderConnection = m_editor.GetHost().beforeUiUpdate.connect(this, &ParticleSystemEditorInstance::Render);
	}

	ParticleSystemEditorInstance::~ParticleSystemEditorInstance()
	{
		if (m_emitter)
		{
			m_scene.DestroyParticleEmitter(*m_emitter);
		}

		m_worldGrid.reset();
		m_axisDisplay.reset();

		m_scene.Clear();
	}

	void ParticleSystemEditorInstance::Render()
	{
		if (!m_viewportRT) return;
		if (m_lastAvailViewportSize.x <= 0.0f || m_lastAvailViewportSize.y <= 0.0f) return;

		// Update particle emitter if parameters changed
		if (m_parametersDirty && m_emitter)
		{
			UpdateParticleEmitter();
			m_parametersDirty = false;
		}

		// Update particle system
		if (m_emitter)
		{
			m_emitter->Update();
		}

		auto& gx = GraphicsDevice::Get();

		// Render the scene
		gx.Reset();
		gx.SetClearColor(Color(0.1f, 0.1f, 0.15f, 1.0f));
		m_viewportRT->Activate();
		m_viewportRT->Clear(mmo::ClearFlags::All);
		gx.SetViewport(0, 0, m_lastAvailViewportSize.x, m_lastAvailViewportSize.y, 0.0f, 1.0f);
		m_camera->SetAspectRatio(m_lastAvailViewportSize.x / m_lastAvailViewportSize.y);

		gx.SetFillMode(m_wireFrame ? FillMode::Wireframe : FillMode::Solid);

		m_scene.Render(*m_camera, PixelShaderType::Forward);
		
		m_viewportRT->Update();
	}

	void ParticleSystemEditorInstance::Draw()
	{
		ImGui::PushID(GetAssetPath().c_str());

		const auto dockSpaceId = ImGui::GetID("##particle_dockspace_");
		ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		const String viewportId = "Viewport##" + GetAssetPath().string();
		const String parametersId = "Parameters##" + GetAssetPath().string();

		// Draw windows
		DrawViewport(viewportId);
		DrawParameters(parametersId);

		// Init dock layout
		if (m_initDockLayout)
		{
			ImGui::DockBuilderRemoveNode(dockSpaceId);
			ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_AutoHideTabBar);
			ImGui::DockBuilderSetNodeSize(dockSpaceId, ImGui::GetMainViewport()->Size);

			auto mainId = dockSpaceId;
			const auto sideId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Right, 400.0f / ImGui::GetMainViewport()->Size.x, nullptr, &mainId);
			
			ImGui::DockBuilderDockWindow(viewportId.c_str(), mainId);
			ImGui::DockBuilderDockWindow(parametersId.c_str(), sideId);

			m_initDockLayout = false;
		}

		ImGui::DockBuilderFinish(dockSpaceId);

		ImGui::PopID();
	}

	void ParticleSystemEditorInstance::OnMouseButtonDown(uint32 button, uint16 x, uint16 y)
	{
		if (button == 0) m_leftButtonPressed = true;
		if (button == 1) m_rightButtonPressed = true;
		if (button == 2) m_middleButtonPressed = true;

		m_lastMouseX = x;
		m_lastMouseY = y;
	}

	void ParticleSystemEditorInstance::OnMouseButtonUp(uint32 button, uint16 x, uint16 y)
	{
		if (button == 0) m_leftButtonPressed = false;
		if (button == 1) m_rightButtonPressed = false;
		if (button == 2) m_middleButtonPressed = false;
	}

	void ParticleSystemEditorInstance::OnMouseMoved(uint16 x, uint16 y)
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
			const Vector3 panOffset = m_cameraNode->GetOrientation() * Vector3(-deltaX * 0.01f, deltaY * 0.01f, 0.0f);
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

	bool ParticleSystemEditorInstance::Save()
	{
		const auto file = AssetRegistry::CreateNewFile(m_assetPath.string());
		if (!file)
		{
			ELOG("Failed to open particle system file " << m_assetPath << " for writing!");
			return false;
		}

		try
		{
			io::StreamSink sink(*file);
			io::Writer writer(sink);

			// Serialize parameters
			ParticleEmitterSerializer serializer;
			serializer.Serialize(m_parameters, writer);

			ILOG("Saved particle system to " << m_assetPath);
			return true;
		}
		catch (const std::exception& ex)
		{
			ELOG("Failed to save particle system: " << ex.what());
			return false;
		}
	}

	void ParticleSystemEditorInstance::DrawViewport(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			// Determine the available size for the viewport window
			const auto availableSpace = ImGui::GetContentRegionAvail();

			if (m_viewportRT == nullptr)
			{
				m_viewportRT = GraphicsDevice::Get().CreateRenderTexture("ParticleViewport", 
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
				m_cameraNode->Translate(Vector3::UnitZ * ImGui::GetIO().MouseWheel * 0.1f, TransformSpace::Local);
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

	void ParticleSystemEditorInstance::DrawParameters(const String& id)
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

				if (ImGui::BeginMenu("Playback"))
				{
					if (ImGui::MenuItem(m_isPlaying ? "Pause" : "Play"))
					{
						m_isPlaying = !m_isPlaying;
						if (m_emitter)
						{
							if (m_isPlaying)
							{
								m_emitter->Play();
							}
							else
							{
								m_emitter->Stop();
							}
						}
					}

					if (ImGui::MenuItem("Reset"))
					{
						if (m_emitter)
						{
							m_emitter->Reset();
							m_emitter->Play();
							m_isPlaying = true;
						}
					}

					ImGui::EndMenu();
				}

				ImGui::EndMenuBar();
			}

			if (ImGui::CollapsingHeader("Spawn Settings", ImGuiTreeNodeFlags_DefaultOpen))
			{
				if (ImGui::DragFloat("Spawn Rate", &m_parameters.spawnRate, 1.0f, 0.0f, 1000.0f))
				{
					m_parametersDirty = true;
				}
				
				if (ImGui::DragInt("Max Particles", reinterpret_cast<int*>(&m_parameters.maxParticles), 1.0f, 1, 10000))
				{
					m_parametersDirty = true;
				}
			}

			if (ImGui::CollapsingHeader("Emitter Shape", ImGuiTreeNodeFlags_DefaultOpen))
			{
				DrawEmitterShape(id);
			}

			if (ImGui::CollapsingHeader("Particle Properties", ImGuiTreeNodeFlags_DefaultOpen))
			{
				DrawParticleProperties(id);
			}

			if (ImGui::CollapsingHeader("Color Over Lifetime", ImGuiTreeNodeFlags_DefaultOpen))
			{
				DrawColorCurve(id);
			}

			if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
			{
				DrawMaterialSelection(id);
			}
		}
		ImGui::End();
	}

	void ParticleSystemEditorInstance::DrawEmitterShape(const String& id)
	{
		const char* shapeNames[] = { "Point", "Sphere", "Box", "Cone" };
		int currentShape = static_cast<int>(m_parameters.shape);
		
		if (ImGui::Combo("Shape", &currentShape, shapeNames, IM_ARRAYSIZE(shapeNames)))
		{
			m_parameters.shape = static_cast<EmitterShape>(currentShape);
			m_parametersDirty = true;
		}

		switch (m_parameters.shape)
		{
			case EmitterShape::Sphere:
				if (ImGui::DragFloat("Radius", &m_parameters.shapeExtents.x, 0.1f, 0.0f, 100.0f))
				{
					m_parametersDirty = true;
				}
				break;

			case EmitterShape::Box:
				if (ImGui::DragFloat3("Extents", &m_parameters.shapeExtents.x, 0.1f, 0.0f, 100.0f))
				{
					m_parametersDirty = true;
				}
				break;

			case EmitterShape::Cone:
				if (ImGui::DragFloat("Angle (degrees)", &m_parameters.shapeExtents.x, 1.0f, 0.0f, 180.0f))
				{
					// Convert degrees to radians for internal storage
					const float degrees = m_parameters.shapeExtents.x;
					m_parameters.shapeExtents.x = degrees * 3.14159265f / 180.0f;
					m_parametersDirty = true;
				}
				
				if (ImGui::DragFloat("Height", &m_parameters.shapeExtents.y, 0.1f, 0.0f, 100.0f))
				{
					m_parametersDirty = true;
				}
				
				if (ImGui::DragFloat("Base Radius", &m_parameters.shapeExtents.z, 0.1f, 0.0f, 100.0f))
				{
					m_parametersDirty = true;
				}
				break;

			case EmitterShape::Point:
			default:
				ImGui::TextDisabled("No parameters for point emitter");
				break;
		}
	}

	void ParticleSystemEditorInstance::DrawParticleProperties(const String& id)
	{
		// Lifetime
		if (ImGui::DragFloat("Min Lifetime", &m_parameters.minLifetime, 0.1f, 0.0f, 100.0f))
		{
			m_parametersDirty = true;
		}
		
		if (ImGui::DragFloat("Max Lifetime", &m_parameters.maxLifetime, 0.1f, 0.0f, 100.0f))
		{
			m_parametersDirty = true;
		}

		// Velocity
		if (ImGui::DragFloat3("Min Velocity", &m_parameters.minVelocity.x, 0.1f))
		{
			m_parametersDirty = true;
		}
		
		if (ImGui::DragFloat3("Max Velocity", &m_parameters.maxVelocity.x, 0.1f))
		{
			m_parametersDirty = true;
		}

		// Gravity
		if (ImGui::DragFloat3("Gravity", &m_parameters.gravity.x, 0.1f))
		{
			m_parametersDirty = true;
		}

		// Size
		if (ImGui::DragFloat("Start Size", &m_parameters.startSize, 0.01f, 0.0f, 100.0f))
		{
			m_parametersDirty = true;
		}
		
		if (ImGui::DragFloat("End Size", &m_parameters.endSize, 0.01f, 0.0f, 100.0f))
		{
			m_parametersDirty = true;
		}

		// Sprite sheet
		ImGui::Separator();
		ImGui::Text("Sprite Sheet Animation");
		
		if (ImGui::DragInt("Columns", reinterpret_cast<int*>(&m_parameters.spriteSheetColumns), 1.0f, 1, 16))
		{
			m_parametersDirty = true;
		}
		
		if (ImGui::DragInt("Rows", reinterpret_cast<int*>(&m_parameters.spriteSheetRows), 1.0f, 1, 16))
		{
			m_parametersDirty = true;
		}
		
		if (ImGui::Checkbox("Animate Sprites", &m_parameters.animateSprites))
		{
			m_parametersDirty = true;
		}
	}

	void ParticleSystemEditorInstance::DrawColorCurve(const String& id)
	{
		Vector4 startColor = m_parameters.colorOverLifetime.Evaluate(0.0f);
		Vector4 endColor = m_parameters.colorOverLifetime.Evaluate(1.0f);

		if (ImGui::ColorEdit4("Start Color", &startColor.x))
		{
			m_parameters.colorOverLifetime = ColorCurve(startColor, endColor);
			m_parametersDirty = true;
		}

		if (ImGui::ColorEdit4("End Color", &endColor.x))
		{
			m_parameters.colorOverLifetime = ColorCurve(startColor, endColor);
			m_parametersDirty = true;
		}
	}

	void ParticleSystemEditorInstance::DrawMaterialSelection(const String& id)
	{
		if (ImGui::InputText("Material Name", &m_parameters.materialName))
		{
			m_parametersDirty = true;
		}

		if (ImGui::Button("Use Default Additive"))
		{
			m_parameters.materialName = "Particles/Additive.hmat";
			m_parametersDirty = true;
		}

		ImGui::SameLine();

		if (ImGui::Button("Use Default Alpha"))
		{
			m_parameters.materialName = "Particles/Alpha.hmat";
			m_parametersDirty = true;
		}
	}

	bool ParticleSystemEditorInstance::LoadParticleSystem()
	{
		const auto file = AssetRegistry::OpenFile(m_assetPath.string());
		if (!file)
		{
			ELOG("Failed to open particle system file: " << m_assetPath);
			return false;
		}

		try
		{
			io::StreamSource source(*file);
			io::Reader reader(source);

			// Deserialize parameters
			ParticleEmitterSerializer serializer;
			if (!serializer.Deserialize(m_parameters, reader))
			{
				ELOG("Failed to deserialize particle emitter parameters from: " << m_assetPath);
				return false;
			}

			ILOG("Successfully loaded particle system from: " << m_assetPath);
			return true;
		}
		catch (const std::exception& ex)
		{
			ELOG("Exception while loading particle system from " << m_assetPath << ": " << ex.what());
			return false;
		}
	}

	void ParticleSystemEditorInstance::UpdateParticleEmitter()
	{
		if (!m_emitter) return;

		// Update parameters
		m_emitter->SetParameters(m_parameters);

		// Update material
		if (!m_parameters.materialName.empty())
		{
			try
			{
				auto material = MaterialManager::Get().Load(m_parameters.materialName);
				m_emitter->SetMaterial(material);
			}
			catch (...)
			{
				WLOG("Failed to load material: " << m_parameters.materialName);
			}
		}

		// Restart emitter to apply changes
		m_emitter->Reset();
		if (m_isPlaying)
		{
			m_emitter->Play();
		}
	}
}
