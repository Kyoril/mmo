// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "viewport_window.h"

#include "graphics/graphics_device.h"

namespace mmo
{
	/// The text that is being rendered when there is no mesh loaded in the editor.
	static const char* s_viewportInstructionText = "Drag & Drop an FBX file to create a new model";

	
	ViewportWindow::ViewportWindow()
		: m_visible(true)
		, m_wireFrame(false)
	{
		m_cameraAnchor = &m_scene.CreateSceneNode("CameraAnchor");
		m_cameraNode = &m_scene.CreateSceneNode("CameraNode");
		m_cameraAnchor->AddChild(*m_cameraNode);
		m_camera = m_scene.CreateCamera("Camera");
		m_cameraNode->AttachObject(*m_camera);
		m_cameraNode->SetPosition(Vector3::UnitZ * 15.0f);
		m_cameraAnchor->SetOrientation(Quaternion(Degree(-45.0f), Vector3::UnitX));

		m_scene.GetRootSceneNode().AddChild(*m_cameraAnchor);
	}

	ViewportWindow::~ViewportWindow()
	{
		m_worldGrid.reset();
		m_axisDisplay.reset();
		m_scene.Clear();
	}

	void ViewportWindow::Render()
	{
		// Only render if the viewport is visible at all
		if (!m_visible || m_viewportRT == nullptr)
			return;

		if (!m_worldGrid)
		{
			m_worldGrid = std::make_unique<WorldGrid>(m_scene, "WorldGrid");	
		}
		if (!m_axisDisplay)
		{
			m_axisDisplay = std::make_unique<AxisDisplay>(m_scene, "DebugAxis");
			m_scene.GetRootSceneNode().AddChild(m_axisDisplay->GetSceneNode());
		}

		auto& gx = GraphicsDevice::Get();

		// Render the scene first
		gx.Reset();
		m_viewportRT->Activate();
		m_viewportRT->Clear(mmo::ClearFlags::All);
		gx.SetViewport(0, 0, m_lastAvailViewportSize.x, m_lastAvailViewportSize.y, 0.0f, 1.0f);
		m_camera->SetAspectRatio(m_lastAvailViewportSize.x / m_lastAvailViewportSize.y);

		gx.SetFillMode(m_wireFrame ? FillMode::Wireframe : FillMode::Solid);
		
		m_scene.Render(*m_camera);
		
		m_viewportRT->Update();
	}

	void ViewportWindow::SetMesh(VertexBufferPtr vertBuf, IndexBufferPtr indexBuf)
	{
		m_vertBuf = std::move(vertBuf);
		m_indexBuf = std::move(indexBuf);
	}

	void ViewportWindow::MoveCamera(const Vector3 & offset)
	{
		if (!m_cameraAnchor)
		{
			return;
		}

		m_cameraAnchor->Yaw(Radian(offset.x), TransformSpace::World);
		m_cameraAnchor->Pitch(Radian(offset.y), TransformSpace::Local);
	}

	void ViewportWindow::MoveCameraTarget(const Vector3 & offset)
	{
		if (!m_cameraAnchor)
		{
			return;
		}

		m_cameraAnchor->Yaw(Radian(offset.x), TransformSpace::World);
		m_cameraAnchor->Pitch(Radian(offset.y), TransformSpace::Local);
	}

	bool ViewportWindow::Draw()
	{
		// Anything to draw at all?
		if (!m_visible)
			return false;

		// Add the viewport
		if (ImGui::Begin("Viewport", &m_visible))
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

			// Is there any geometry to render?
			if (!m_vertBuf || !m_indexBuf)
			{
				// Calculate the size required to render the viewport instruction text on screen (used for alignment calculations)
				const auto textSize = ImGui::CalcTextSize(s_viewportInstructionText, nullptr);

				// Draw the instruction text at the center of the viewport window
				ImGui::GetWindowDrawList()->AddText(
					ImGui::GetFont(),
					ImGui::GetFontSize(),
					ImVec2(viewportPos.x + (m_lastAvailViewportSize.x / 2.0f - textSize.x / 2.0f), viewportPos.y + (m_lastAvailViewportSize.y / 2.0f - textSize.y / 2.0f)),
					IM_COL32_WHITE,
					s_viewportInstructionText);
			}
		}
		ImGui::End();

		return false;
	}

	bool ViewportWindow::DrawViewMenuItem()
	{
		if (ImGui::MenuItem("Viewport", nullptr, &m_visible)) 
		{
			Show();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Wireframe", nullptr, m_wireFrame))
		{
			m_wireFrame = !m_wireFrame;
		}

		return false;
	}
}
