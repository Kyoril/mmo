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
		m_cameraPos = Vector3(0.0f, 0.0f, 5.0f);
		m_cameraRotation = Quaternion::Identity;
	}

	void ViewportWindow::Render() const
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

		gx.SetFillMode(m_wireFrame ? FillMode::Wireframe : FillMode::Solid);
		gx.SetFaceCullMode(FaceCullMode::Front);

		if (m_vertBuf && m_indexBuf)
		{
			const auto view = MakeViewMatrix(m_cameraPos, Quaternion::Identity);

			// Setup camera mode
			Matrix3 rot{};
			m_cameraRotation.ToRotationMatrix(rot);
			Matrix4 world = Matrix4::Identity;
			world = rot;
			
			gx.SetTransformMatrix(TransformType::World, world);
			gx.SetTransformMatrix(TransformType::View, view);
			gx.SetTransformMatrix(TransformType::Projection, m_projMatrix);

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

	void ViewportWindow::MoveCamera(const Vector3 & offset)
	{
		m_cameraPos.z += offset.y;
		if (m_cameraPos.z < 1.0f) m_cameraPos.z = 1.0f;
		if (m_cameraPos.z > 100.0f) m_cameraPos.z = 100.0f;
	}

	void ViewportWindow::MoveCameraTarget(const Vector3 & offset)
	{
		const auto yaw = Radian(offset.x);
		Quaternion qYaw{ yaw, Vector3::UnitY };
		qYaw.Normalize();

		const auto pitch = Radian(offset.y);
		Quaternion qPitch{ pitch, Vector3::UnitX };
		qPitch.Normalize();

		m_cameraRotation = qYaw * m_cameraRotation;
		m_cameraRotation = qPitch * m_cameraRotation;
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
				
				UpdateProjectionMatrix();
			}
			else if (m_lastAvailViewportSize.x != availableSpace.x || m_lastAvailViewportSize.y != availableSpace.y)
			{
				m_viewportRT->Resize(availableSpace.x, availableSpace.y);
				m_lastAvailViewportSize = availableSpace;

				UpdateProjectionMatrix();
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

	void ViewportWindow::UpdateProjectionMatrix()
	{
		const auto aspect = m_lastAvailViewportSize.x / m_lastAvailViewportSize.y;
		m_projMatrix = GraphicsDevice::Get().MakeProjectionMatrix(Degree(45.0f), aspect, 0.001f, 100.0f);
	}
}
