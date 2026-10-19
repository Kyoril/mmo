// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_renderer.h"
#include "scene_graph/scene.h"
#include "scene_graph/camera.h"
#include "scene_graph/scene_node.h"

#include "frame_ui/frame.h"
#include "frame_ui/color.h"
#include "frame_ui/geometry_helper.h"

#include "base/profiler.h"
#include "base/clock.h"
#include "math/clamp.h"
#include "console/console_var.h"
#include "frame_pacing.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#ifdef _WIN32
#	include <d3d11.h>
#	include "graphics_d3d11/graphics_device_d3d11.h"
#	include <DirectXPackedVector.h>
#	include <wrl/client.h>
#	include "graphics_d3d11/render_texture_d3d11.h"
	using Microsoft::WRL::ComPtr;
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
		/// Diagnostic (cvar gxShadowDump N): raw depth of the fog's shadow map and its view-projection matrix for the
		/// next N frames, to capture/shadow_NNNNN.bin (uint32 width, height, 16 floats matrix, then the raw texels).
		void DumpFogShadowMap(DeferredRenderer& renderer)
		{
#ifdef _WIN32
			static int32 s_index = 0;
			auto* map = static_cast<RenderTextureD3D11*>(renderer.GetFogShadowMap());
			if (!map)
			{
				return;
			}

			ComPtr<ID3D11Resource> resource;
			map->GetDepthShaderResourceView()->GetResource(&resource);
			ComPtr<ID3D11Texture2D> source;
			if (FAILED(resource.As(&source)))
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
			ComPtr<ID3D11Texture2D> staging;
			if (FAILED(d3dDevice.CreateTexture2D(&desc, nullptr, &staging)))
			{
				return;
			}
			context.CopyResource(staging.Get(), source.Get());

			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(context.Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
			{
				std::filesystem::create_directories("capture");
				char name[64];
				std::snprintf(name, sizeof(name), "capture/shadow_%05d.bin", s_index++);
				std::ofstream file(name, std::ios::binary);
				const uint32 w = desc.Width, h = desc.Height;
				file.write(reinterpret_cast<const char*>(&w), sizeof(w));
				file.write(reinterpret_cast<const char*>(&h), sizeof(h));
				const Matrix4& m = renderer.GetFogShadowViewProj();
				for (int r = 0; r < 4; ++r)
				{
					for (int c = 0; c < 4; ++c)
					{
						const float v = m[r][c];
						file.write(reinterpret_cast<const char*>(&v), sizeof(v));
					}
				}
				for (UINT y = 0; y < h; ++y)
				{
					file.write(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch, w * 4);
				}
				context.Unmap(staging.Get(), 0);
			}
#endif
		}

		/// Diagnostic full-rate recorder (cvar gxCaptureFast N): copies the next N final scene targets into a ring of
		/// staging textures and reads each back four frames later, when the GPU is long done with it, so the frame rate
		/// stays (nearly) untouched. Writes the luminance of every 4th pixel as float32 to capture/fast_NNNNN.bin and
		/// a microsecond timestamp plus camera position per frame to capture/fast.txt.
		void FastCaptureFrame(DeferredRenderer& renderer, const Camera& camera, const bool record)
		{
#ifdef _WIN32
			static constexpr uint32 RingSize = 4;
			static ID3D11Texture2D* s_staging[RingSize] = {};
			static int64 s_pending[RingSize] = { -1, -1, -1, -1 };
			static double s_times[RingSize] = {};
			static Vector3 s_positions[RingSize];
			static UINT s_width = 0, s_height = 0;
			static DXGI_FORMAT s_format = DXGI_FORMAT_UNKNOWN;
			static int64 s_next = 0;

			auto& device = static_cast<GraphicsDeviceD3D11&>(GraphicsDevice::Get());
			ID3D11Device& d3dDevice = device;
			ID3D11DeviceContext& context = device;

			const TexturePtr target = renderer.GetFinalRenderTarget();
			auto* source = static_cast<ID3D11Texture2D*>(target ? target->GetRawTexture() : nullptr);
			if (!source)
			{
				return;
			}

			D3D11_TEXTURE2D_DESC desc;
			source->GetDesc(&desc);
			const uint32 slot = static_cast<uint32>(s_next % RingSize);

			// Read back the frame this slot held (RingSize frames old).
			if (s_pending[slot] >= 0 && s_staging[slot])
			{
				D3D11_MAPPED_SUBRESOURCE mapped;
				if (SUCCEEDED(context.Map(s_staging[slot], 0, D3D11_MAP_READ, 0, &mapped)))
				{
					const UINT outW = s_width / 4, outH = s_height / 4;
					std::vector<float> lum(static_cast<size_t>(outW) * outH);
					for (UINT y = 0; y < outH; ++y)
					{
						const uint8* row = static_cast<const uint8*>(mapped.pData) + static_cast<size_t>(y * 4) * mapped.RowPitch;
						for (UINT x = 0; x < outW; ++x)
						{
							float r, g, b;
							if (s_format == DXGI_FORMAT_R16G16B16A16_FLOAT)
							{
								const auto* px = reinterpret_cast<const DirectX::PackedVector::HALF*>(row) + static_cast<size_t>(x * 4) * 4;
								r = DirectX::PackedVector::XMConvertHalfToFloat(px[0]);
								g = DirectX::PackedVector::XMConvertHalfToFloat(px[1]);
								b = DirectX::PackedVector::XMConvertHalfToFloat(px[2]);
							}
							else
							{
								const uint8* px = row + static_cast<size_t>(x * 4) * 4;
								r = px[0] / 255.0f; g = px[1] / 255.0f; b = px[2] / 255.0f;
							}
							lum[static_cast<size_t>(y) * outW + x] = 0.2126f * r + 0.7152f * g + 0.0722f * b;
						}
					}
					context.Unmap(s_staging[slot], 0);

					char name[64];
					std::snprintf(name, sizeof(name), "capture/fast_%05d.bin", static_cast<int>(s_pending[slot]));
					std::ofstream file(name, std::ios::binary);
					file.write(reinterpret_cast<const char*>(&outW), sizeof(outW));
					file.write(reinterpret_cast<const char*>(&outH), sizeof(outH));
					file.write(reinterpret_cast<const char*>(lum.data()), static_cast<std::streamsize>(lum.size() * sizeof(float)));

					std::ofstream log("capture/fast.txt", std::ios::app);
					log << std::fixed << s_pending[slot] << " " << s_times[slot] << " " << s_positions[slot].x << " " << s_positions[slot].y << " " << s_positions[slot].z << "\n";
				}
				s_pending[slot] = -1;
			}

			if (!record)
			{
				// Keep draining the ring after the recording ends.
				bool anyPending = false;
				for (const int64 pending : s_pending)
				{
					anyPending = anyPending || pending >= 0;
				}
				if (anyPending)
				{
					++s_next;
				}
				return;
			}

			if (desc.Width != s_width || desc.Height != s_height || desc.Format != s_format)
			{
				for (uint32 i = 0; i < RingSize; ++i)
				{
					if (s_staging[i])
					{
						s_staging[i]->Release();
						s_staging[i] = nullptr;
					}
					s_pending[i] = -1;
				}
				s_width = desc.Width;
				s_height = desc.Height;
				s_format = desc.Format;
			}

			if (!s_staging[slot])
			{
				D3D11_TEXTURE2D_DESC stagingDesc = desc;
				stagingDesc.Usage = D3D11_USAGE_STAGING;
				stagingDesc.BindFlags = 0;
				stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				stagingDesc.MiscFlags = 0;
				stagingDesc.MipLevels = 1;
				stagingDesc.ArraySize = 1;
				stagingDesc.SampleDesc.Count = 1;
				if (FAILED(d3dDevice.CreateTexture2D(&stagingDesc, nullptr, &s_staging[slot])))
				{
					return;
				}
			}

			std::filesystem::create_directories("capture");
			context.CopyResource(s_staging[slot], source);
			s_pending[slot] = s_next;
			s_times[slot] = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
			s_positions[slot] = camera.GetDerivedPosition();
			++s_next;
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
		const float playerScale = s_renderScaleVar ? Clamp(s_renderScaleVar->GetFloatValue(), 0.25f, 2.0f) : 1.0f;
		const float renderScale = std::max(0.25f, playerScale * FramePacing::GetDynamicRenderScale()); // TEMP: up to 2 for the supersampling test

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
			// Diagnostic scripted path (cvar gxFogPath N): the next N frames render from a straight path that starts at
			// the current camera pose and advances 10 cm per frame along the horizontal view direction, independent of
			// the frame rate, and are recorded like gxCaptureFrames. Replaying it with different settings gives exact
			// A/B comparisons in the real scene. The camera node is restored after each frame.
			static ConsoleVar* s_pathVar = nullptr;
			if (!s_pathVar)
			{
				s_pathVar = ConsoleVarMgr::FindConsoleVar("gxFogPath");
			}
			static bool s_pathActive = false;
			static Vector3 s_pathStart;
			static Quaternion s_pathOrientation;
			static Vector3 s_pathForward;
			static int32 s_pathIndex = 0;
			static float s_pathDistance = 0.0f;
			static ConsoleVar* s_pathStepVar = nullptr;
			static ConsoleVar* s_pathJitterVar = nullptr;
			if (!s_pathStepVar)
			{
				s_pathStepVar = ConsoleVarMgr::FindConsoleVar("gxFogPathStep");
				s_pathJitterVar = ConsoleVarMgr::FindConsoleVar("gxFogPathJitter");
			}
			SceneNode* cameraNode = m_camera->GetParentSceneNode();
			const bool onPath = s_pathVar && s_pathVar->GetIntValue() > 0 && cameraNode;
			Vector3 savedPosition;
			Quaternion savedOrientation;
			if (onPath)
			{
				if (!s_pathActive)
				{
					s_pathActive = true;
					s_pathIndex = 0;
					s_pathDistance = 0.0f;
					s_pathStart = m_camera->GetDerivedPosition();
					s_pathOrientation = cameraNode->GetDerivedOrientation();
					s_pathForward = m_camera->GetDerivedDirection();
					s_pathForward.y = 0.0f;
					s_pathForward.Normalize();
				}

				savedPosition = cameraNode->GetPosition();
				savedOrientation = cameraNode->GetOrientation();
				// Step per frame (gxFogPathStep, metres); gxFogPathJitter 1 adds the per-frame unevenness of a real
				// walk (+-7 % step, +-2 mm vertical bob) from a fixed pseudo-random sequence.
				const float step = s_pathStepVar ? s_pathStepVar->GetFloatValue() : 0.1f;
				const bool jitter = s_pathJitterVar && s_pathJitterVar->GetIntValue() != 0;
				const uint32 hash = static_cast<uint32>(s_pathIndex) * 2654435761u;
				const float noise = static_cast<float>((hash >> 8) & 0xFFFF) / 65535.0f * 2.0f - 1.0f;
				if (s_pathIndex > 0)
				{
					s_pathDistance += step * (jitter ? 1.0f + 0.07f * noise : 1.0f);
				}
				const float bob = jitter ? ((s_pathIndex & 1) ? 0.002f : -0.002f) : 0.0f;
				cameraNode->SetDerivedPosition(s_pathStart + s_pathForward * s_pathDistance + Vector3(0.0f, bob, 0.0f));
				cameraNode->SetDerivedOrientation(s_pathOrientation);
			}
			else
			{
				s_pathActive = false;
			}

			m_deferredRenderer->Render(m_worldScene, *m_camera);

			if (onPath)
			{
				static ConsoleVar* s_pathRecordVar = ConsoleVarMgr::FindConsoleVar("gxFogPathRecord");
				if (!s_pathRecordVar || s_pathRecordVar->GetIntValue() != 0)
				{
					CaptureSceneFrame(*m_deferredRenderer, *m_camera);
				}
				cameraNode->SetPosition(savedPosition);
				cameraNode->SetOrientation(savedOrientation);
				++s_pathIndex;
				s_pathVar->Set(s_pathVar->GetIntValue() - 1);
			}

			// Diagnostic (cvar gxCameraLog N): camera pose and a high-resolution timestamp for the next N frames to
			// capture/camera.txt, without reading back the image (so the frame rate is unaffected).
			static ConsoleVar* s_cameraLogVar = nullptr;
			if (!s_cameraLogVar)
			{
				s_cameraLogVar = ConsoleVarMgr::FindConsoleVar("gxCameraLog");
			}
			if (s_cameraLogVar && s_cameraLogVar->GetIntValue() > 0)
			{
				std::filesystem::create_directories("capture");
				const Vector3 position = m_camera->GetDerivedPosition();
				const Vector3 direction = m_camera->GetDerivedDirection();
				const double micros = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
				std::ofstream("capture/camera.txt", std::ios::app) << std::fixed << micros << " " << position.x << " " << position.y << " " << position.z
					<< " " << direction.x << " " << direction.y << " " << direction.z << "\n";
				s_cameraLogVar->Set(s_cameraLogVar->GetIntValue() - 1);
			}

			static ConsoleVar* s_shadowDumpVar = nullptr;
			if (!s_shadowDumpVar)
			{
				s_shadowDumpVar = ConsoleVarMgr::FindConsoleVar("gxShadowDump");
			}
			if (s_shadowDumpVar && s_shadowDumpVar->GetIntValue() > 0)
			{
				DumpFogShadowMap(*m_deferredRenderer);
				s_shadowDumpVar->Set(s_shadowDumpVar->GetIntValue() - 1);
			}

			static ConsoleVar* s_fastVar = nullptr;
			if (!s_fastVar)
			{
				s_fastVar = ConsoleVarMgr::FindConsoleVar("gxCaptureFast");
			}
			const bool recordFast = s_fastVar && s_fastVar->GetIntValue() > 0;
			FastCaptureFrame(*m_deferredRenderer, *m_camera, recordFast);
			if (recordFast)
			{
				s_fastVar->Set(s_fastVar->GetIntValue() - 1);
			}

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
