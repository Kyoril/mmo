// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_renderer.h"
#include "scene_graph/scene.h"
#include "scene_graph/camera.h"

#include "frame_ui/frame.h"
#include "frame_ui/color.h"
#include "frame_ui/geometry_helper.h"

#include "base/profiler.h"
#include "base/clock.h"
#include "math/clamp.h"
#include "console/console_var.h"
#include "frame_pacing.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#	include <d3d11.h>
#	include "graphics_d3d11/graphics_device_d3d11.h"
#endif

namespace mmo
{
	namespace
	{
		/// Diagnostic frame capture (cvar gxCaptureFrames N): writes the next N final scene targets as raw
		/// texels to capture/NNNN.bin plus camera poses and timestamps to capture/frames.txt, so motion
		/// artefacts the eye reports can be measured offline. Reads back synchronously; expect a frame drop.
		void CaptureSceneFrame(DeferredRenderer& renderer, const Camera& camera)
		{
#ifdef _WIN32
			static int32 s_index = 0;
			const TexturePtr target = renderer.GetFinalRenderTarget();
			auto* source = static_cast<ID3D11Texture2D*>(target ? target->GetRawTexture() : nullptr);
			if (!source)
			{
				return;
			}

			auto& device = static_cast<GraphicsDeviceD3D11&>(GraphicsDevice::Get());
			ID3D11Device& d3dDevice = device;
			ID3D11DeviceContext& context = device;

			D3D11_TEXTURE2D_DESC desc;
			source->GetDesc(&desc);
			desc.Usage = D3D11_USAGE_STAGING;
			desc.BindFlags = 0;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			desc.MiscFlags = 0;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.SampleDesc.Count = 1;

			ID3D11Texture2D* staging = nullptr;
			if (FAILED(d3dDevice.CreateTexture2D(&desc, nullptr, &staging)))
			{
				return;
			}

			context.CopyResource(staging, source);

			std::filesystem::create_directories("capture");
			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(context.Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
			{
				const uint32 bytesPerPixel = desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4;
				std::ofstream("capture/meta.txt") << desc.Width << " " << desc.Height << " " << static_cast<int>(desc.Format) << "\n";

				char name[64];
				std::snprintf(name, sizeof(name), "capture/%04d.bin", s_index);
				std::ofstream file(name, std::ios::binary);
				for (UINT y = 0; y < desc.Height; ++y)
				{
					file.write(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch, desc.Width * bytesPerPixel);
				}
				context.Unmap(staging, 0);

				const Vector3 position = camera.GetDerivedPosition();
				const Vector3 direction = camera.GetDerivedDirection();
				std::ofstream log("capture/frames.txt", std::ios::app);
				log << s_index << " " << GetAsyncTimeMs() << " " << position.x << " " << position.y << " " << position.z
					<< " " << direction.x << " " << direction.y << " " << direction.z << "\n";
				++s_index;
			}

			staging->Release();
#endif
		}
	}

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
		// The frame rate threshold (FramePacing) lowers it further while the GPU cannot keep up.
		const float playerScale = s_renderScaleVar ? Clamp(s_renderScaleVar->GetFloatValue(), 0.25f, 1.0f) : 1.0f;
		const float renderScale = std::max(0.25f, playerScale * FramePacing::GetDynamicRenderScale());

		const uint16 internalWidth = static_cast<uint16>(std::max(1.0f, frameRect.GetWidth() * renderScale));
		const uint16 internalHeight = static_cast<uint16>(std::max(1.0f, frameRect.GetHeight() * renderScale));

		// Resize the internal render target when either the frame size or the render scale changes.
		if (m_internalWidth != internalWidth || m_internalHeight != internalHeight)
		{
			m_deferredRenderer->Resize(internalWidth, internalHeight);
			m_internalWidth = internalWidth;
			m_internalHeight = internalHeight;
		}

		// Capture the old graphics state (including the render target)
		gx.CaptureState();
		gx.Reset();

		if (m_camera)
		{
			m_camera->SetAspectRatio(frameRect.GetWidth() / frameRect.GetHeight());
			m_deferredRenderer->Render(m_worldScene, *m_camera);

			static ConsoleVar* s_captureVar = nullptr;
			if (!s_captureVar)
			{
				s_captureVar = ConsoleVarMgr::FindConsoleVar("gxCaptureFrames");
			}
			if (s_captureVar && s_captureVar->GetIntValue() > 0)
			{
				CaptureSceneFrame(*m_deferredRenderer, *m_camera);
				s_captureVar->Set(s_captureVar->GetIntValue() - 1);
			}
		}

		// Restore state before drawing the frame's geometry buffer
		GraphicsDevice::Get().RestoreState();

		// Rebuild the quad after rendering, not before: only now does the deferred renderer know
		// which texture holds this frame. The geometry buffer stores the texture pointer per batch,
		// so a changed final target - diving in, surfacing, the post-process output being
		// recreated - needs a rebuild just like a changed frame rect does.
		const TexturePtr finalTexture = m_deferredRenderer->GetFinalRenderTarget();
		if (m_lastFrameRect != frameRect
			|| m_frame->GetGeometryBuffer().GetBatchCount() == 0
			|| finalTexture != m_displayedTexture)
		{
			// Reset the buffer first
			m_frame->GetGeometryBuffer().Reset();

			// Populate the frame's geometry buffer
			m_frame->GetGeometryBuffer().SetActiveTexture(finalTexture);
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

			m_displayedTexture = finalTexture;
		}

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
