// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "post_process_pass.h"

#include "scene_graph/camera.h"

// --- Shader bytecode seam ---------------------------------------------------------------
// The only backend-bound fact in this file: which compiled blob to hand CreateShader. Mirrors
// the seam in ssao_pass.cpp; the deferred path is D3D11-only today.
#ifdef _WIN32
#	include <Windows.h>
#	include "shaders/PS_Underwater.h"
#	define MMO_UNDERWATER_PS_BYTECODE g_PS_Underwater
#	define MMO_UNDERWATER_PS_SIZE std::size(g_PS_Underwater)
#else
#	define MMO_UNDERWATER_PS_BYTECODE nullptr
#	define MMO_UNDERWATER_PS_SIZE 0
#endif
// ---------------------------------------------------------------------------------------

namespace mmo
{
	namespace
	{
		/// @brief Mirrors the UnderwaterBuffer cbuffer in PS_Underwater.hlsl (b2).
		struct alignas(16) UnderwaterConstants
		{
			float fogColorR;
			float fogColorG;
			float fogColorB;
			float fogDensity;

			float absorptionR;
			float absorptionG;
			float absorptionB;
			float absorptionPadding;

			float screenWidth;
			float screenHeight;
			float invScreenWidth;
			float invScreenHeight;

			float submersionDepth;
			float transitionPhase;
			float time;
			float distortionStrength;

			float causticsStrength;
			float godRaysEnabled;
			float sunScreenU;
			float sunScreenV;

			float surfaceHeight;
			float padding0;
			float padding1;
			float padding2;
		};
	}

	PostProcessPass::PostProcessPass(GraphicsDevice& device, const uint32 width, const uint32 height)
		: m_device(device)
		, m_sceneWidth(width)
		, m_sceneHeight(height)
	{
		m_underwaterBuffer = m_device.CreateConstantBuffer(sizeof(UnderwaterConstants), nullptr);
		ASSERT(m_underwaterBuffer);

		m_underwaterPs = m_device.CreateShader(ShaderType::PixelShader,
			MMO_UNDERWATER_PS_BYTECODE, MMO_UNDERWATER_PS_SIZE);
		ASSERT(m_underwaterPs);

		// A 1x1 black texture stands in for the caustics while no profile has supplied one, so
		// the shader can sample unconditionally with no permutation and no branch.
		m_blackTexture = m_device.CreateTexture(1, 1, BufferUsage::Static);
		ASSERT(m_blackTexture);

		uint32 blackPixel = 0xFF000000;
		m_blackTexture->LoadRaw(&blackPixel, sizeof(blackPixel));
		m_blackTexture->SetDebugName("UnderwaterNoCaustics");
	}

	void PostProcessPass::Resize(const uint32 width, const uint32 height)
	{
		m_sceneWidth = width;
		m_sceneHeight = height;

		// Rebuilt lazily on the next Render, so a resize while the camera is dry costs nothing
		// and allocates nothing.
		m_outputRT.reset();
		m_targetWidth = 0;
		m_targetHeight = 0;
	}

	void PostProcessPass::EnsureTargets()
	{
		const uint32 desiredWidth = m_sceneWidth > 0 ? m_sceneWidth : 1u;
		const uint32 desiredHeight = m_sceneHeight > 0 ? m_sceneHeight : 1u;

		if (m_outputRT && m_targetWidth == desiredWidth && m_targetHeight == desiredHeight)
		{
			return;
		}

		// Matches the scene colour format so nothing is clipped on the way through: the deferred
		// output is R16G16B16A16 and the sun shafts deliberately add above 1.0.
		m_outputRT = m_device.CreateRenderTexture("PostProcessOutput",
			static_cast<uint16>(desiredWidth), static_cast<uint16>(desiredHeight),
			RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView,
			PixelFormat::R16G16B16A16);
		ASSERT(m_outputRT);

		m_targetWidth = desiredWidth;
		m_targetHeight = desiredHeight;
	}

	void PostProcessPass::Render(const UnderwaterState& state, Camera& camera, RenderTexture& sceneColor,
		RenderTexture& gbufferNormalRT, VertexBuffer& quad, ShaderBase& fullscreenVs,
		const float sunScreenU, const float sunScreenV, const float elapsedSeconds)
	{
		if (!WouldRun(state))
		{
			// Dry. Hold no target at all so the feature costs no video memory until the camera
			// first goes under.
			if (m_outputRT)
			{
				m_outputRT.reset();
				m_targetWidth = 0;
				m_targetHeight = 0;
			}

			return;
		}

		EnsureTargets();

		UnderwaterConstants constants{};
		constants.fogColorR = state.fogColor[0];
		constants.fogColorG = state.fogColor[1];
		constants.fogColorB = state.fogColor[2];
		constants.fogDensity = state.fogDensity;
		constants.absorptionR = state.absorptionColor[0];
		constants.absorptionG = state.absorptionColor[1];
		constants.absorptionB = state.absorptionColor[2];
		constants.screenWidth = static_cast<float>(m_targetWidth);
		constants.screenHeight = static_cast<float>(m_targetHeight);
		constants.invScreenWidth = 1.0f / static_cast<float>(m_targetWidth);
		constants.invScreenHeight = 1.0f / static_cast<float>(m_targetHeight);
		constants.submersionDepth = state.submersionDepth;
		constants.transitionPhase = state.transitionPhase;
		constants.time = elapsedSeconds;
		constants.distortionStrength = state.distortionStrength;
		constants.causticsStrength = state.causticsStrength;
		constants.godRaysEnabled = state.godRaysEnabled ? 1.0f : 0.0f;
		constants.sunScreenU = sunScreenU;
		constants.sunScreenV = sunScreenV;

		// The shader measures each view ray's path through the water against this plane, which is
		// what keeps the surface overhead - and anything seen through it - from being fogged as if
		// it lay at full scene depth.
		constants.surfaceHeight = state.surfaceHeight;

		m_underwaterBuffer->Update(&constants);

		// The shader reads InverseProjection and matInvView from b12 to rebuild world position
		// from depth for the caustic projection, so the transforms must be set before the draw.
		m_device.SetTransformMatrix(World, Matrix4::Identity);
		m_device.SetTransformMatrix(View, camera.GetViewMatrix());
		m_device.SetTransformMatrix(Projection, camera.GetProjectionMatrix());

		m_outputRT->Activate();
		m_outputRT->Clear(ClearFlags::Color);
		m_device.SetViewport(0, 0, static_cast<int32>(m_targetWidth), static_cast<int32>(m_targetHeight), 0.0f, 1.0f);

		m_device.SetDepthEnabled(false);
		m_device.SetDepthWriteEnabled(false);
		m_device.SetFillMode(FillMode::Solid);
		m_device.SetFaceCullMode(FaceCullMode::None);
		m_device.SetTextureAddressMode(TextureAddressMode::Clamp, TextureAddressMode::Clamp, TextureAddressMode::Clamp);
		m_device.SetTextureFilter(TextureFilter::Bilinear);

		sceneColor.Bind(ShaderType::PixelShader, 0);
		gbufferNormalRT.Bind(ShaderType::PixelShader, 1);
		m_device.BindTexture(m_causticsTexture ? m_causticsTexture : m_blackTexture, ShaderType::PixelShader, 2);

		m_device.SetVertexFormat(VertexFormat::PosColorTex1);
		m_device.SetTopologyType(TopologyType::TriangleList);

		fullscreenVs.Set();
		m_underwaterPs->Set();

		quad.Set(0);
		m_underwaterBuffer->BindToStage(ShaderType::PixelShader, 2);

		m_device.Draw(6, 0);

		// Release the SRVs so they cannot collide with render target bindings next frame - the
		// scene colour in particular is a render target for the rest of the frame graph.
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 1);
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 2);
	}
}
