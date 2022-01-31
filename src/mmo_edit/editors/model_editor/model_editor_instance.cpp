// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "model_editor_instance.h"

#include <imgui_internal.h>

#include "model_editor.h"
#include "editor_host.h"
#include "scene_graph/camera.h"
#include "scene_graph/entity.h"
#include "scene_graph/scene_node.h"

namespace mmo
{
	ModelEditorInstance::ModelEditorInstance(EditorHost& host, ModelEditor& editor, Path asset)
		: EditorInstance(host, std::move(asset))
		, m_editor(editor)
		, m_wireFrame(false)
	{
		m_cameraAnchor = &m_scene.CreateSceneNode("CameraAnchor");
		m_cameraNode = &m_scene.CreateSceneNode("CameraNode");
		m_cameraAnchor->AddChild(*m_cameraNode);
		m_camera = m_scene.CreateCamera("Camera");
		m_cameraNode->AttachObject(*m_camera);
		m_cameraNode->SetPosition(Vector3::UnitZ * 35.0f);
		m_cameraAnchor->SetOrientation(Quaternion(Degree(-35.0f), Vector3::UnitX));

		m_scene.GetRootSceneNode().AddChild(*m_cameraAnchor);

		m_worldGrid = std::make_unique<WorldGrid>(m_scene, "WorldGrid");
		m_axisDisplay = std::make_unique<AxisDisplay>(m_scene, "DebugAxis");
			m_scene.GetRootSceneNode().AddChild(m_axisDisplay->GetSceneNode());

		m_entity = m_scene.CreateEntity("Entity", GetAssetPath().string());
		if (m_entity)
		{
			m_scene.GetRootSceneNode().AttachObject(*m_entity);
			m_cameraNode->SetPosition(Vector3::UnitZ * m_entity->GetBoundingRadius() * 2.0f);
		}

		m_renderConnection = m_editor.GetHost().beforeUiUpdate.connect(this, &ModelEditorInstance::Render);
	}

	ModelEditorInstance::~ModelEditorInstance()
	{
		if (m_entity)
		{
			m_scene.DestroyEntity(*m_entity);
		}

		m_worldGrid.reset();
		m_axisDisplay.reset();
		m_scene.Clear();
	}

	void ModelEditorInstance::Render()
	{
		if (!m_viewportRT) return;
		if (m_lastAvailViewportSize.x <= 0.0f || m_lastAvailViewportSize.y <= 0.0f) return;

		auto& gx = GraphicsDevice::Get();

		// Render the scene first
		gx.Reset();
		gx.SetClearColor(Color::Black);
		m_viewportRT->Activate();
		m_viewportRT->Clear(mmo::ClearFlags::All);
		gx.SetViewport(0, 0, m_lastAvailViewportSize.x, m_lastAvailViewportSize.y, 0.0f, 1.0f);
		m_camera->SetAspectRatio(m_lastAvailViewportSize.x / m_lastAvailViewportSize.y);

		gx.SetFillMode(m_wireFrame ? FillMode::Wireframe : FillMode::Solid);
		
		m_scene.Render(*m_camera);
		
		m_viewportRT->Update();
	}

	void ModelEditorInstance::Draw()
	{
		const auto dockspaceId = ImGui::GetID("##model_dockspace_");
		ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
		
		if (ImGui::Begin("##model_details_"))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
		    if (ImGui::BeginTable("split", 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable))
		    {
				if (m_entity != nullptr)
				{
					for (size_t i = 0; i < m_entity->GetNumSubEntities(); ++i)
					{
						ImGui::PushID(i); // Use field index as identifier.
						ImGui::TableNextRow();
					    ImGui::TableSetColumnIndex(0);
					    ImGui::AlignTextToFramePadding();
					    bool node_open = ImGui::TreeNode("Object", "SubEntity %zu", i);
						
					    if (node_open)
					    {
							ImGui::TableNextRow();
				            ImGui::TableSetColumnIndex(0);
				            ImGui::AlignTextToFramePadding();
							ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
				            ImGui::TreeNodeEx("Field", flags, "Material");

							ImGui::TableSetColumnIndex(1);
							ImGui::SetNextItemWidth(-FLT_MIN);
							
							ImGui::NextColumn();
							ImGui::TreePop();
						}
						ImGui::PopID();
					}
				}
				
		        ImGui::EndTable();
		    }
		    ImGui::PopStyleVar();
		}
		ImGui::End();

		if (ImGui::Begin("##model_ed_viewport_"))
		{
			// Determine the current viewport position
			auto viewportPos = ImGui::GetWindowContentRegionMin();
			viewportPos.x += ImGui::GetWindowPos().x;
			viewportPos.y += ImGui::GetWindowPos().y;

			// Determine the available size for the viewport window and either create the render target
			// or resize it if needed
			const auto availableSpace = ImGui::GetContentRegionAvail();
			
			if (m_viewportRT == nullptr)
			{
				m_viewportRT = GraphicsDevice::Get().CreateRenderTexture("Viewport", std::max(1.0f, availableSpace.x), std::max(1.0f, availableSpace.y));
				m_lastAvailViewportSize = availableSpace;
			}
			else if (m_lastAvailViewportSize.x != availableSpace.x || m_lastAvailViewportSize.y != availableSpace.y)
			{
				m_viewportRT->Resize(availableSpace.x, availableSpace.y);
				m_lastAvailViewportSize = availableSpace;
			}

			// Render the render target content into the window as image object
			ImGui::Image(m_viewportRT->GetTextureObject(), availableSpace);
		}
		ImGui::End();
		
		ImGui::DockBuilderFinish(dockspaceId);
	}

	void ModelEditorInstance::OnMouseButtonDown(uint32 button, uint16 x, uint16 y)
	{
		m_lastMouseX = x;
		m_lastMouseY = y;
		
		if (button == 0)
		{
			m_leftButtonPressed = true;
		}
		else if (button == 1)
		{
			m_rightButtonPressed = true;
		}
	}

	void ModelEditorInstance::OnMouseButtonUp(uint32 button, uint16 x, uint16 y)
	{
		if (button == 0)
		{
			m_leftButtonPressed = false;
		}
		else if (button == 1)
		{
			m_rightButtonPressed = false;
		}
	}

	void ModelEditorInstance::OnMouseMoved(const uint16 x, const uint16 y)
	{
		// Calculate mouse move delta
		const int16 deltaX = static_cast<int16>(x) - m_lastMouseX;
		const int16 deltaY = static_cast<int16>(y) - m_lastMouseY;

		if (m_leftButtonPressed || m_rightButtonPressed)
		{
			m_cameraAnchor->Yaw(-Degree(deltaX), TransformSpace::World);
			m_cameraAnchor->Pitch(-Degree(deltaY), TransformSpace::Local);
		}

		m_lastMouseX = x;
		m_lastMouseY = y;
	}
}
