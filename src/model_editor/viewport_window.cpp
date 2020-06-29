// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "viewport_window.h"

#include "graphics/graphics_device.h"


namespace mmo
{
	static const std::string s_viewportInstructionText = "Drag & Drop an FBX file to create a new model";

	ViewportWindow::ViewportWindow()
		: m_visible(true)
	{
	}

	void ViewportWindow::Render()
	{
		// Only render if the viewport is visible at all
		if (!m_visible || m_viewportRT == nullptr)
			return;

		auto& gx = GraphicsDevice::Get();

		// Render the scene first
		gx.Reset();
		m_viewportRT->Activate();
		m_viewportRT->Clear(mmo::ClearFlags::All);
		gx.SetViewport(0, 0, m_lastAvailViewportSize.x, m_lastAvailViewportSize.y, 0.0f, 1.0f);

		if (m_vertBuf && m_indexBuf)
		{
			const float aspect = m_lastAvailViewportSize.x / m_lastAvailViewportSize.y;
			Matrix4 view = Matrix4::MakeView(Vector3(0.0f, 0.0f, 5.0f), Vector3());
			Matrix4 proj = Matrix4::MakeProjection(60.0f * 3.1415927f / 180.0f, aspect, 0.001f, 100.0f);
			
			// Setup camera mode
			gx.SetTransformMatrix(TransformType::View, view);
			gx.SetTransformMatrix(TransformType::Projection, proj);

			// Draw buffers
			gx.SetTopologyType(TopologyType::TriangleList);
			gx.SetVertexFormat(VertexFormat::PosColor);
			gx.SetBlendMode(BlendMode::Opaque);
			m_vertBuf->Set();
			m_indexBuf->Set();
			gx.DrawIndexed();
		}
		
		m_viewportRT->Update();
	}

	void ViewportWindow::SetMesh(VertexBufferPtr vertBuf, IndexBufferPtr indexBuf)
	{
		m_vertBuf = std::move(vertBuf);
		m_indexBuf = std::move(indexBuf);
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
			ImVec2 viewportPos = ImGui::GetWindowContentRegionMin();
			viewportPos.x += ImGui::GetWindowPos().x;
			viewportPos.y += ImGui::GetWindowPos().y;

			// Determine the available size for the viewport window and either create the render target
			// or resize it if needed
			ImVec2 availableSpace = ImGui::GetContentRegionAvail();
			if (m_viewportRT == nullptr)
			{
				m_viewportRT = GraphicsDevice::Get().CreateRenderTexture("Viewport", availableSpace.x, availableSpace.y);
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
				const ImVec2 textSize = ImGui::CalcTextSize(s_viewportInstructionText.c_str(), nullptr);

				// Draw the instruction text at the center of the viewport window
				ImGui::GetWindowDrawList()->AddText(
					ImGui::GetFont(),
					ImGui::GetFontSize(),
					ImVec2(viewportPos.x + (m_lastAvailViewportSize.x / 2.0f - textSize.x / 2.0f), viewportPos.y + (m_lastAvailViewportSize.y / 2.0f - textSize.y / 2.0f)),
					IM_COL32_WHITE,
					s_viewportInstructionText.c_str());
			}
		}
		ImGui::End();

		return false;
	}

	bool ViewportWindow::DrawViewMenuItem()
	{
		if (ImGui::MenuItem("Viewport", nullptr, &m_visible)) Show();

		return false;
	}
}
