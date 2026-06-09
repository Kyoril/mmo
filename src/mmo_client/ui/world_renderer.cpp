// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_renderer.h"
#include "scene_graph/scene.h"
#include "scene_graph/camera.h"

#include "frame_ui/frame.h"
#include "frame_ui/color.h"
#include "frame_ui/geometry_helper.h"

#include "base/profiler.h"
#include "math/clamp.h"
#include "console/console_var.h"

#include <algorithm>

namespace mmo
{
	WorldRenderer::WorldRenderer(const std::string & name, Scene& worldScene)
		: FrameRenderer(name)
		, m_worldFrame(nullptr)
		, m_worldScene(worldScene)
		, m_camera(nullptr)
	{
		m_camera = m_worldScene.GetCamera("Default");

		m_deferredRenderer = std::make_unique<DeferredRenderer>(GraphicsDevice::Get(), m_worldScene, 1920, 1080);
	}

	void WorldRenderer::Render(optional<Color> colorOverride, optional<Rect> clipper)
	{
		// Anything to render here?
		if (!m_deferredRenderer || !m_worldFrame)
		{
			return;
		}

		// Grab the graphics device instance
		auto& gx = GraphicsDevice::Get();

		// Get the current frame rect
		const auto frameRect = m_frame->GetAbsoluteFrameRect();

		// Resolve the render scale from the console variable. The 3D scene (G-Buffer, lighting and
		// shadows) is rendered at frameSize * scale and then upscaled to the full frame size by the
		// textured quad below, so lowering the scale is a large, near-linear performance win on
		// fill-rate/bandwidth limited GPUs while leaving the UI rendered at native resolution.
		static ConsoleVar* s_renderScaleVar = nullptr;
		if (!s_renderScaleVar)
		{
			s_renderScaleVar = ConsoleVarMgr::FindConsoleVar("gxRenderScale");
		}
		const float renderScale = s_renderScaleVar ? Clamp(s_renderScaleVar->GetFloatValue(), 0.25f, 1.0f) : 1.0f;

		const uint16 internalWidth = static_cast<uint16>(std::max(1.0f, frameRect.GetWidth() * renderScale));
		const uint16 internalHeight = static_cast<uint16>(std::max(1.0f, frameRect.GetHeight() * renderScale));

		// Resize the internal render target when either the frame size or the render scale changes.
		if (m_internalWidth != internalWidth || m_internalHeight != internalHeight)
		{
			m_deferredRenderer->Resize(internalWidth, internalHeight);
			m_internalWidth = internalWidth;
			m_internalHeight = internalHeight;
		}

		// If frame rect mismatches or buffer empty...
		if (m_lastFrameRect != frameRect || m_frame->GetGeometryBuffer().GetBatchCount() == 0)
		{
			// Reset the buffer first
			m_frame->GetGeometryBuffer().Reset();

			// Populate the frame's geometry buffer
			m_frame->GetGeometryBuffer().SetActiveTexture(m_deferredRenderer->GetFinalRenderTarget());
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
		gx.Reset();

		if (m_camera)
		{
			m_camera->SetAspectRatio(frameRect.GetWidth() / frameRect.GetHeight());
			m_deferredRenderer->Render(m_worldScene, *m_camera);
		}
		
		// Restore state before drawing the frame's geometry buffer
		GraphicsDevice::Get().RestoreState();
		m_frame->GetGeometryBuffer().Draw();

		// Apply frame rect
		m_lastFrameRect = frameRect;
	}

	void WorldRenderer::NotifyFrameAttached()
	{
		// Try to obtain the model frame instance. We do the cast here so that
		// we avoid a cast every time the frame is rendered. DynamicCast is used since
		// this renderer should not crash the game when not attached to a ModelFrame for now.
		m_worldFrame = dynamic_cast<WorldFrame*>(m_frame);

		// We reset the buffer contents manually as we only really need to change it when the
		// frame is moved
		m_frame->AddFlags(static_cast<uint32>(FrameFlags::ManualResetBuffer));

		// Get the frame's last rectangle and initialize it
		m_lastFrameRect = m_frame->GetAbsoluteFrameRect();

		// After the frame has been rendered, invalidate it to re-render next frame automatically
		m_frameRenderEndCon = m_frame->RenderingEnded.connect([this]() {
			m_frame->Invalidate(false);
		});
	}

	void WorldRenderer::NotifyFrameDetached()
	{
		// We no longer manually reset the frame
		m_frame->RemoveFlags(static_cast<uint32>(FrameFlags::ManualResetBuffer));

		// Disconnect frame rendered event
		m_frameRenderEndCon.disconnect();

		// Reset the render texture
		m_worldFrame = nullptr;
	}
}
