// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "ssao_pass.h"

#include "scene_graph/camera.h"

// --- Shader bytecode seam ---------------------------------------------------------------
// The only backend-bound fact in this file: which compiled blob to hand CreateShader. The
// deferred path is D3D11-only today (there are no .metal shaders in the tree and
// ShaderCompilerMetal is a stub), so only the D3D_SM5 blob exists. When a Metal deferred
// path appears, this seam gains a branch and nothing below it changes.
#ifdef _WIN32
#	include <Windows.h>
#	include "shaders/PS_Ssao.h"
#	include "shaders/PS_SsaoBlur.h"
#	define MMO_SSAO_PS_BYTECODE g_PS_Ssao
#	define MMO_SSAO_PS_SIZE std::size(g_PS_Ssao)
#	define MMO_SSAO_BLUR_PS_BYTECODE g_PS_SsaoBlur
#	define MMO_SSAO_BLUR_PS_SIZE std::size(g_PS_SsaoBlur)
#else
#	define MMO_SSAO_PS_BYTECODE nullptr
#	define MMO_SSAO_PS_SIZE 0
#	define MMO_SSAO_BLUR_PS_BYTECODE nullptr
#	define MMO_SSAO_BLUR_PS_SIZE 0
#endif
// ---------------------------------------------------------------------------------------

namespace mmo
{
	namespace
	{
		/// @brief Mirrors the SsaoBuffer cbuffer in PS_Ssao.hlsl / PS_SsaoBlur.hlsl (b2).
		struct alignas(16) SsaoConstants
		{
			// screenWidth/screenHeight: reserved for layout only. PS_Ssao.hlsl marches entirely
			// in UV space and never reads ScreenSize; PS_SsaoBlur.hlsl only reads InvScreenSize.
			// Kept assigned below anyway since it's harmless and self-documenting.
			float screenWidth;
			float screenHeight;
			float invScreenWidth;
			float invScreenHeight;

			float radius;
			float intensity;
			float thickness;
			uint32 sliceCount;

			uint32 stepCount;
			// _padding0: reserved purely to preserve the b2 layout (was "debugMode"; read by
			// neither shader). Real debug visualization is routed through
			// ShadowBuffer.SsaoDebugMode (b3), consumed by PS_DeferredLighting.hlsl, because the
			// lighting pass can't see this cbuffer. Do not repurpose this field for debug output.
			uint32 _padding0;
			float padding0;
			float padding1;
		};

		/// @brief Mirrors the SsaoBlurBuffer cbuffer in PS_SsaoBlur.hlsl (b3).
		struct alignas(16) SsaoBlurConstants
		{
			float directionX;
			float directionY;
			float padding0;
			float padding1;
		};
	}

	SsaoPass::SsaoPass(GraphicsDevice& device, uint32 width, uint32 height)
		: m_device(device)
		, m_gbufferWidth(width)
		, m_gbufferHeight(height)
	{
		m_ssaoBuffer = m_device.CreateConstantBuffer(sizeof(SsaoConstants), nullptr);
		ASSERT(m_ssaoBuffer);

		m_ssaoBlurBuffer = m_device.CreateConstantBuffer(sizeof(SsaoBlurConstants), nullptr);
		ASSERT(m_ssaoBlurBuffer);

		m_ssaoPs = m_device.CreateShader(ShaderType::PixelShader, MMO_SSAO_PS_BYTECODE, MMO_SSAO_PS_SIZE);
		m_ssaoBlurPs = m_device.CreateShader(ShaderType::PixelShader, MMO_SSAO_BLUR_PS_BYTECODE, MMO_SSAO_BLUR_PS_SIZE);
		ASSERT(m_ssaoPs);
		ASSERT(m_ssaoBlurPs);

		// A 1x1 opaque white texture stands in for the AO term while SSAO is disabled, so the
		// lighting shader always has something to sample. CreateTexture takes no pixel data —
		// LoadRaw uploads it and builds an R8G8B8A8_UNORM texture from these dimensions.
		m_whiteTexture = m_device.CreateTexture(1, 1, BufferUsage::Static);
		ASSERT(m_whiteTexture);

		uint32 whitePixel = 0xFFFFFFFF;
		m_whiteTexture->LoadRaw(&whitePixel, sizeof(whitePixel));
		m_whiteTexture->SetDebugName("SsaoDisabledWhite");
	}

	void SsaoPass::Resize(uint32 width, uint32 height)
	{
		m_gbufferWidth = width;
		m_gbufferHeight = height;

		// Targets are rebuilt lazily on the next Render so a resize while disabled costs nothing.
		ReleaseTargets();
	}

	void SsaoPass::ReleaseTargets()
	{
		m_aoRT.reset();
		m_blurRT.reset();
		m_targetWidth = 0;
		m_targetHeight = 0;
	}

	void SsaoPass::EnsureTargets()
	{
		const uint32 divisor = m_settings.halfResolution ? 2u : 1u;
		const uint32 desiredWidth = m_gbufferWidth / divisor > 0 ? m_gbufferWidth / divisor : 1u;
		const uint32 desiredHeight = m_gbufferHeight / divisor > 0 ? m_gbufferHeight / divisor : 1u;

		if (m_aoRT && m_targetWidth == desiredWidth && m_targetHeight == desiredHeight)
		{
			return;
		}

		m_aoRT = m_device.CreateRenderTexture("SsaoRaw",
			static_cast<uint16>(desiredWidth), static_cast<uint16>(desiredHeight),
			RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView,
			PixelFormat::R8);
		ASSERT(m_aoRT);

		m_blurRT = m_device.CreateRenderTexture("SsaoBlur",
			static_cast<uint16>(desiredWidth), static_cast<uint16>(desiredHeight),
			RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView,
			PixelFormat::R8);
		ASSERT(m_blurRT);

		m_targetWidth = desiredWidth;
		m_targetHeight = desiredHeight;
	}

	TexturePtr SsaoPass::GetResult() const
	{
		if (!m_settings.enabled || !m_aoRT)
		{
			return m_whiteTexture;
		}

		return m_aoRT;
	}

	void SsaoPass::Render(Camera& camera, RenderTexture& gbufferNormalRT, VertexBuffer& quad, ShaderBase& fullscreenVs)
	{
		if (!m_settings.enabled)
		{
			// Disabled: hold no targets at all, so a disabled pass costs no video memory.
			if (m_aoRT)
			{
				ReleaseTargets();
			}

			return;
		}

		EnsureTargets();

		SsaoConstants constants{};
		constants.screenWidth = static_cast<float>(m_targetWidth);
		constants.screenHeight = static_cast<float>(m_targetHeight);
		constants.invScreenWidth = 1.0f / static_cast<float>(m_targetWidth);
		constants.invScreenHeight = 1.0f / static_cast<float>(m_targetHeight);
		constants.radius = m_settings.radius;
		constants.intensity = m_settings.intensity;
		constants.thickness = m_settings.thickness;
		constants.sliceCount = m_settings.sliceCount;
		constants.stepCount = m_settings.stepCount;
		// _padding0 (formerly debugMode) is intentionally left unset — it is unread padding.
		// Real debug visualization goes through ShadowBuffer.SsaoDebugMode instead; see the
		// SsaoConstants field comment above.
		m_ssaoBuffer->Update(&constants);

		// The shader reads matView / matProj / InverseProjection from b12, which the device
		// uploads from these transforms. They must be set before the draw.
		m_device.SetTransformMatrix(World, Matrix4::Identity);
		m_device.SetTransformMatrix(View, camera.GetViewMatrix());
		m_device.SetTransformMatrix(Projection, camera.GetProjectionMatrix());

		m_aoRT->Activate();
		m_aoRT->Clear(ClearFlags::Color);
		m_device.SetViewport(0, 0, static_cast<int32>(m_targetWidth), static_cast<int32>(m_targetHeight), 0.0f, 1.0f);

		m_device.SetDepthEnabled(false);
		m_device.SetDepthWriteEnabled(false);
		m_device.SetFillMode(FillMode::Solid);
		m_device.SetFaceCullMode(FaceCullMode::None);
		m_device.SetTextureAddressMode(TextureAddressMode::Clamp, TextureAddressMode::Clamp, TextureAddressMode::Clamp);
		m_device.SetTextureFilter(TextureFilter::None);

		// Bind the G-Buffer normal target at t1, matching PS_Ssao.hlsl.
		gbufferNormalRT.Bind(ShaderType::PixelShader, 1);

		m_device.SetVertexFormat(VertexFormat::PosColorTex1);
		m_device.SetTopologyType(TopologyType::TriangleList);

		fullscreenVs.Set();
		m_ssaoPs->Set();

		quad.Set(0);
		m_ssaoBuffer->BindToStage(ShaderType::PixelShader, 2);

		m_device.Draw(6, 0);

		// Release the normal target SRV so it cannot collide with render target bindings later.
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 1);

		// --- Separable bilateral blur ---------------------------------------------------
		// Resolves the position-keyed noise pattern. Horizontal into m_blurRT, then vertical
		// back into m_aoRT so GetResult() always names the finished term.
		m_ssaoBlurPs->Set();
		m_ssaoBlurBuffer->BindToStage(ShaderType::PixelShader, 3);

		// Horizontal: m_aoRT -> m_blurRT.
		SsaoBlurConstants blurConstants{};
		blurConstants.directionX = 1.0f;
		blurConstants.directionY = 0.0f;
		m_ssaoBlurBuffer->Update(&blurConstants);

		m_blurRT->Activate();
		m_blurRT->Clear(ClearFlags::Color);
		m_device.SetViewport(0, 0, static_cast<int32>(m_targetWidth), static_cast<int32>(m_targetHeight), 0.0f, 1.0f);
		m_device.BindTexture(m_aoRT, ShaderType::PixelShader, 0);
		gbufferNormalRT.Bind(ShaderType::PixelShader, 1);
		m_device.Draw(6, 0);
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);

		// Vertical: m_blurRT -> m_aoRT.
		blurConstants.directionX = 0.0f;
		blurConstants.directionY = 1.0f;
		m_ssaoBlurBuffer->Update(&blurConstants);

		m_aoRT->Activate();
		m_aoRT->Clear(ClearFlags::Color);
		m_device.SetViewport(0, 0, static_cast<int32>(m_targetWidth), static_cast<int32>(m_targetHeight), 0.0f, 1.0f);
		m_device.BindTexture(m_blurRT, ShaderType::PixelShader, 0);
		gbufferNormalRT.Bind(ShaderType::PixelShader, 1);
		m_device.Draw(6, 0);

		// Release SRVs so they cannot collide with render target bindings next frame.
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 1);
	}
}
