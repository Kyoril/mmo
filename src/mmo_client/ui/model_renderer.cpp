// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "model_renderer.h"

#include "frame_ui/frame.h"
#include "frame_ui/color.h"
#include "frame_ui/geometry_helper.h"

#include "math/quaternion.h"
#include "scene_graph/render_operation.h"

namespace mmo
{
	ModelRenderer::ModelRenderer(const std::string & name)
		: FrameRenderer(name)
		, m_modelFrame(nullptr)
	{
	}

	void ModelRenderer::Update(const float elapsedSeconds)
	{
		FrameRenderer::Update(elapsedSeconds);

	}

	void ModelRenderer::Render(optional<Color> colorOverride, optional<Rect> clipper)
	{
		// Anything to render here?
		if (!m_renderTexture || !m_modelFrame)
		{
			return;
		}

		// Grab the graphics device instance
		auto& gx = GraphicsDevice::Get();

		// Get the current frame rect
		const auto frameRect = m_frame->GetAbsoluteFrameRect();

		bool shouldRenderScene = m_modelFrame ? m_modelFrame->ShouldRenderModel() : false;

		// Need to resize the render target first?
		if (m_lastFrameRect.GetSize() != frameRect.GetSize())
		{
			// Resize render target
			m_renderTexture->Resize(static_cast<uint16>(frameRect.GetWidth()), static_cast<uint16>(frameRect.GetHeight()));
			shouldRenderScene = true;
		}

		// If frame rect mismatches or buffer empty...
		if (m_lastFrameRect != frameRect || m_frame->GetGeometryBuffer().GetBatchCount() == 0)
		{
			// Reset the buffer first
			m_frame->GetGeometryBuffer().Reset();

			// Populate the frame's geometry buffer
			m_frame->GetGeometryBuffer().SetActiveTexture(m_renderTexture);
			const Color color{ 1.0f, 1.0f, 1.0f };
			const Rect dst{ frameRect.left, frameRect.top, frameRect.right, frameRect.bottom };
			const GeometryBuffer::Vertex vertices[6]{
				{ { dst.left,	dst.top,	 0.0f }, color, { 0.0f, 0.0f } },
				{ { dst.left,	dst.bottom, 0.0f }, color, { 0.0f, 1.0f } },
				{ { dst.right,	dst.bottom, 0.0f }, color, { 1.0f, 1.0f } },
				{ { dst.right,	dst.bottom, 0.0f }, color, { 1.0f, 1.0f } },
				{ { dst.right,	dst.top,	 0.0f }, color, { 1.0f, 0.0f } },
				{ { dst.left,	dst.top,	 0.0f }, color, { 0.0f, 0.0f } }
			};
			m_frame->GetGeometryBuffer().AppendGeometry(vertices, 6);
		}

		if (shouldRenderScene)
		{
			// Capture the old graphics state (including the render target)
			gx.CaptureState();

			// Activate render target
			m_renderTexture->Activate();
			m_renderTexture->Clear(mmo::ClearFlags::All);

			if (m_modelFrame && m_modelFrame->GetCamera())
			{
				m_modelFrame->GetCamera()->SetAspectRatio(frameRect.GetWidth() / frameRect.GetHeight());
				m_modelFrame->GetScene().Render(*m_modelFrame->GetCamera(), PixelShaderType::Forward);
			}

			// Restore state before drawing the frame's geometry buffer
			GraphicsDevice::Get().RestoreState();
		}
		
		m_frame->GetGeometryBuffer().Draw();

		// Apply frame rect
		m_lastFrameRect = frameRect;
	}

	void ModelRenderer::NotifyFrameAttached()
	{
		// Try to obtain the model frame instance. We do the cast here so that
		// we avoid a cast every time the frame is rendered. DynamicCast is used since
		// this renderer should not crash the game when not attached to a ModelFrame for now.
		m_modelFrame = dynamic_cast<ModelFrame*>(m_frame);

		// We reset the buffer contents manually as we only really need to change it when the
		// frame is moved
		m_frame->AddFlags(static_cast<uint32>(FrameFlags::ManualResetBuffer));

		// Get the frame's last rectangle and initialize it
		auto lastFrameRect = m_frame->GetAbsoluteFrameRect();

		// Create the render texture
		if (!m_renderTexture)
		{
			m_renderTexture = GraphicsDevice::Get().CreateRenderTexture(
				m_frame->GetName(), static_cast<uint16>(lastFrameRect.GetWidth()), static_cast<uint16>(lastFrameRect.GetHeight()), RenderTextureFlags::HasColorBuffer | RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView);
			ASSERT(m_renderTexture);
		}
		
		// After the frame has been rendered, invalidate it to re-render next frame automatically
		m_frameRenderEndCon = m_frame->RenderingEnded.connect([this]() {
			m_frame->Invalidate(false);
		});
	}

	void ModelRenderer::NotifyFrameDetached()
	{
		// We no longer manually reset the frame
		m_frame->RemoveFlags(static_cast<uint32>(FrameFlags::ManualResetBuffer));

		// Disconnect frame rendered event
		m_frameRenderEndCon.disconnect();

		// Reset the render texture
		m_renderTexture.reset();
		m_modelFrame = nullptr;
	}
}
