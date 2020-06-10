#include "model_renderer.h"

#include "frame_ui/frame.h"
#include "frame_ui/color.h"
#include "frame_ui/geometry_helper.h"

namespace mmo
{
	ModelRenderer::ModelRenderer(const std::string & name)
		: FrameRenderer(name)
	{
		const POS_COL_VERTEX vertices[3] = {
			{  0.0f,  1.0f, 0.0f, Color(1.0f, 0.0f, 0.0f) },
			{  0.5f,  0.0f, 0.0f, Color(0.0f, 1.0f, 0.0f) },
			{  1.0f,  1.0f, 0.0f, Color(0.0f, 0.0f, 1.0f) }
		};

		const uint16 indices[3] = { 0, 1, 2 };

		// Allocate buffers
		m_vBuffer = GraphicsDevice::Get().CreateVertexBuffer(3, sizeof(POS_COL_VERTEX), false, vertices);
		m_iBuffer = GraphicsDevice::Get().CreateIndexBuffer(3, IndexBufferSize::Index_16, indices);
	}

	void ModelRenderer::Render(optional<Color> colorOverride, optional<Rect> clipper)
	{
		// Anything to render here?
		if (!m_renderTexture)
		{
			return;
		}

		// Grab the graphics device instance
		auto& gx = GraphicsDevice::Get();

		// Get the current frame rect
		const auto frameRect = m_frame->GetAbsoluteFrameRect();

		// Need to resize the render target first?
		if (m_lastFrameRect.GetSize() != frameRect.GetSize())
		{
			// Resize render target
			m_renderTexture->Resize(frameRect.GetWidth(), frameRect.GetHeight());
		}

		// If frame rect mismatches or buffer empty...
		if (m_lastFrameRect != frameRect || m_frame->GetGeometryBuffer().GetBatchCount() == 0)
		{
			// Reset the buffer first
			m_frame->GetGeometryBuffer().Reset();

			// Populate the frame's geometry buffer
			m_frame->GetGeometryBuffer().SetActiveTexture(m_renderTexture);
			const Color color{ 1.0f, 1.0f, 1.0f };
			const Rect dst{ 0.0f, 0.0f, frameRect.GetWidth(), frameRect.GetHeight() };
			const GeometryBuffer::Vertex vertices[6]{
				{ { dst.left,	dst.top,		0.0f }, color, { 0.0f, 0.0f } },
				{ { dst.left,	dst.bottom,		0.0f }, color, { 0.0f, 1.0f } },
				{ { dst.right,	dst.bottom,		0.0f }, color, { 1.0f, 1.0f } },
				{ { dst.right,	dst.bottom,		0.0f }, color, { 1.0f, 1.0f } },
				{ { dst.right,	dst.top,		0.0f }, color, { 1.0f, 0.0f } },
				{ { dst.left,	dst.top,		0.0f }, color, { 0.0f, 0.0f } }
			};
			m_frame->GetGeometryBuffer().AppendGeometry(vertices, 6);
		}

		// Capture the old graphics state (including the render target)
		gx.CaptureState();

		// Activate render target
		m_renderTexture->Activate();
		m_renderTexture->Clear(mmo::ClearFlags::All);

		// TODO: Render the actual model in here
		gx.SetTransformMatrix(TransformType::World, Matrix4::Identity);
		gx.SetTransformMatrix(TransformType::View, Matrix4::Identity);
		gx.SetTransformMatrix(TransformType::Projection, Matrix4::MakeOrthographic(0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f));
		gx.SetBlendMode(BlendMode::Opaque);
		gx.SetVertexFormat(VertexFormat::PosColor);
		gx.SetTopologyType(TopologyType::TriangleList);
		m_vBuffer->Set();
		m_iBuffer->Set();
		gx.DrawIndexed();

		// Restore state
		GraphicsDevice::Get().RestoreState();
		m_frame->GetGeometryBuffer().Draw();

		// Apply frame rect
		m_lastFrameRect = frameRect;
	}

	void ModelRenderer::NotifyFrameAttached()
	{
		// We reset the buffer contents manually as we only really need to change it when the
		// frame is moved
		m_frame->AddFlags((uint32)FrameFlags::ManualResetBuffer);

		// Get the frame's last rectangle and initialize it
		m_lastFrameRect = m_frame->GetAbsoluteFrameRect();

		// Create the render texture
		m_renderTexture = GraphicsDevice::Get().CreateRenderTexture(
			m_frame->GetName(), m_lastFrameRect.GetWidth(), m_lastFrameRect.GetHeight());
		ASSERT(m_renderTexture);

		// After the frame has been rendered, invalidate it to re-render next frame automatically
		m_frameRenderEndCon = m_frame->RenderingEnded.connect([this]() {
			m_frame->Invalidate(false);
		});
	}

	void ModelRenderer::NotifyFrameDetached()
	{
		// We no longer manually reset the frame
		m_frame->RemoveFlags((uint32)FrameFlags::ManualResetBuffer);

		// Disconnect frame rendered event
		m_frameRenderEndCon.disconnect();

		// Reset the render texture
		m_renderTexture.reset();
	}
}
