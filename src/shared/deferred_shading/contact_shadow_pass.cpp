// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "contact_shadow_pass.h"

#include "scene_graph/camera.h"

// --- Shader bytecode seam ---------------------------------------------------------------
// The only backend-bound fact in this file: which compiled blob to hand CreateShader. The
// deferred path is D3D11-only today (there are no .metal shaders in the tree and
// ShaderCompilerMetal is a stub), so only the D3D_SM5 blob exists. When a Metal deferred
// path appears, this seam gains a branch and nothing below it changes.
#ifdef _WIN32
#	include <Windows.h>
#	include "shaders/PS_ContactShadows.h"
#	include "shaders/PS_ContactShadowBlur.h"
#	define MMO_CONTACT_SHADOW_PS_BYTECODE g_PS_ContactShadows
#	define MMO_CONTACT_SHADOW_PS_SIZE std::size(g_PS_ContactShadows)
#	define MMO_CONTACT_SHADOW_BLUR_PS_BYTECODE g_PS_ContactShadowBlur
#	define MMO_CONTACT_SHADOW_BLUR_PS_SIZE std::size(g_PS_ContactShadowBlur)
#else
#	define MMO_CONTACT_SHADOW_PS_BYTECODE nullptr
#	define MMO_CONTACT_SHADOW_PS_SIZE 0
#	define MMO_CONTACT_SHADOW_BLUR_PS_BYTECODE nullptr
#	define MMO_CONTACT_SHADOW_BLUR_PS_SIZE 0
#endif
// ---------------------------------------------------------------------------------------

namespace mmo
{
	namespace
	{
		/// @brief Mirrors the ContactShadowBuffer cbuffer in PS_ContactShadows.hlsl (b2).
		/// @remark MUST stay field-for-field in sync with it: a drift of one field silently makes
		///         every field after it read garbage, with no error anywhere.
		struct alignas(16) ContactShadowConstants
		{
			float sunDirectionX;
			float sunDirectionY;
			float sunDirectionZ;
			float rayLength;

			float screenWidth;
			float screenHeight;
			float invScreenWidth;
			float invScreenHeight;

			float thickness;
			float intensity;
			float normalBias;
			float fadeStart;

			float fadeEnd;
			uint32 stepCount;
			float padding0;
			float padding1;
		};

		/// @brief Mirrors the ContactShadowBlurBuffer cbuffer in PS_ContactShadowBlur.hlsl (b3).
		struct alignas(16) ContactShadowBlurConstants
		{
			float directionX;
			float directionY;
			float invScreenWidth;
			float invScreenHeight;
		};

		static_assert(sizeof(ContactShadowConstants) % 16 == 0, "ContactShadowConstants must be 16-byte aligned to match the HLSL cbuffer layout");
		static_assert(sizeof(ContactShadowBlurConstants) % 16 == 0, "ContactShadowBlurConstants must be 16-byte aligned to match the HLSL cbuffer layout");
	}

	ContactShadowPass::ContactShadowPass(GraphicsDevice& device, uint32 width, uint32 height)
		: m_device(device)
		, m_gbufferWidth(width)
		, m_gbufferHeight(height)
	{
		m_contactShadowBuffer = m_device.CreateConstantBuffer(sizeof(ContactShadowConstants), nullptr);
		ASSERT(m_contactShadowBuffer);

		m_contactShadowBlurBuffer = m_device.CreateConstantBuffer(sizeof(ContactShadowBlurConstants), nullptr);
		ASSERT(m_contactShadowBlurBuffer);

		m_contactShadowPs = m_device.CreateShader(ShaderType::PixelShader, MMO_CONTACT_SHADOW_PS_BYTECODE, MMO_CONTACT_SHADOW_PS_SIZE);
		m_contactShadowBlurPs = m_device.CreateShader(ShaderType::PixelShader, MMO_CONTACT_SHADOW_BLUR_PS_BYTECODE, MMO_CONTACT_SHADOW_BLUR_PS_SIZE);
		ASSERT(m_contactShadowPs);
		ASSERT(m_contactShadowBlurPs);

		// A 1x1 opaque white texture stands in for the term while the effect is disabled, so the
		// lighting shader always has something to sample.
		m_whiteTexture = m_device.CreateTexture(1, 1, BufferUsage::Static);
		ASSERT(m_whiteTexture);

		uint32 whitePixel = 0xFFFFFFFF;
		m_whiteTexture->LoadRaw(&whitePixel, sizeof(whitePixel));
		m_whiteTexture->SetDebugName("ContactShadowDisabledWhite");
	}

	void ContactShadowPass::Resize(uint32 width, uint32 height)
	{
		m_gbufferWidth = width;
		m_gbufferHeight = height;

		// Targets are rebuilt lazily on the next Render so a resize while disabled costs nothing.
		ReleaseTargets();
	}

	void ContactShadowPass::ReleaseTargets()
	{
		m_termRT.reset();
		m_blurRT.reset();
		m_targetWidth = 0;
		m_targetHeight = 0;
	}

	void ContactShadowPass::EnsureTargets()
	{
		const uint32 desiredWidth = m_gbufferWidth > 0 ? m_gbufferWidth : 1u;
		const uint32 desiredHeight = m_gbufferHeight > 0 ? m_gbufferHeight : 1u;

		if (m_termRT && m_targetWidth == desiredWidth && m_targetHeight == desiredHeight)
		{
			return;
		}

		m_termRT = m_device.CreateRenderTexture("ContactShadowRaw",
			static_cast<uint16>(desiredWidth), static_cast<uint16>(desiredHeight),
			RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView,
			PixelFormat::R8);
		ASSERT(m_termRT);

		m_blurRT = m_device.CreateRenderTexture("ContactShadowBlur",
			static_cast<uint16>(desiredWidth), static_cast<uint16>(desiredHeight),
			RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView,
			PixelFormat::R8);
		ASSERT(m_blurRT);

		m_targetWidth = desiredWidth;
		m_targetHeight = desiredHeight;
	}

	TexturePtr ContactShadowPass::GetResult() const
	{
		if (!m_settings.enabled || !m_termRT)
		{
			return m_whiteTexture;
		}

		return m_termRT;
	}

	void ContactShadowPass::Render(Camera& camera, RenderTexture& gbufferNormalRT, RenderTexture& gbufferDepthRT,
		const Vector3& sunDirection, VertexBuffer& quad, ShaderBase& fullscreenVs)
	{
		if (!m_settings.enabled)
		{
			// Disabled: hold no targets at all, so a disabled pass costs no video memory.
			if (m_termRT)
			{
				ReleaseTargets();
			}

			return;
		}

		EnsureTargets();

		ContactShadowConstants constants{};
		constants.sunDirectionX = sunDirection.x;
		constants.sunDirectionY = sunDirection.y;
		constants.sunDirectionZ = sunDirection.z;
		constants.rayLength = m_settings.rayLength;
		constants.screenWidth = static_cast<float>(m_targetWidth);
		constants.screenHeight = static_cast<float>(m_targetHeight);
		constants.invScreenWidth = 1.0f / static_cast<float>(m_targetWidth);
		constants.invScreenHeight = 1.0f / static_cast<float>(m_targetHeight);
		constants.thickness = m_settings.thickness;
		constants.intensity = m_settings.intensity;
		constants.normalBias = m_settings.normalBias;
		constants.fadeStart = m_settings.fadeStart;
		constants.fadeEnd = m_settings.fadeEnd;
		// GetEffectiveStepCount folds in `enabled`, giving the shader a single uniform value to
		// branch on. Reaching here means enabled is true, but route through it anyway so there is
		// exactly one definition of "off".
		constants.stepCount = m_settings.GetEffectiveStepCount();
		m_contactShadowBuffer->Update(&constants);

		// The shader reads matView / matProj / InverseProjection from b12, which the device uploads
		// from these transforms. They must be set before the draw.
		m_device.SetTransformMatrix(World, Matrix4::Identity);
		m_device.SetTransformMatrix(View, camera.GetViewMatrix());
		m_device.SetTransformMatrix(Projection, camera.GetProjectionMatrix());

		m_termRT->Activate();
		m_termRT->Clear(ClearFlags::Color);
		m_device.SetViewport(0, 0, static_cast<int32>(m_targetWidth), static_cast<int32>(m_targetHeight), 0.0f, 1.0f);

		m_device.SetDepthEnabled(false);
		m_device.SetDepthWriteEnabled(false);
		m_device.SetFillMode(FillMode::Solid);
		m_device.SetFaceCullMode(FaceCullMode::None);
		m_device.SetTextureAddressMode(TextureAddressMode::Clamp, TextureAddressMode::Clamp, TextureAddressMode::Clamp);
		// Point filtering is mandatory, not a preference: a bilinear depth tap interpolates across
		// silhouettes and fabricates an occluder that does not exist.
		m_device.SetTextureFilter(TextureFilter::None);

		// t1 = G-Buffer normals (+ radial depth in alpha), t2 = the stencil plane carrying the
		// "does not cast shadows" tag written during the G-Buffer pass.
		gbufferNormalRT.Bind(ShaderType::PixelShader, 1);
		gbufferDepthRT.BindStencil(ShaderType::PixelShader, 2);

		m_device.SetVertexFormat(VertexFormat::PosColorTex1);
		m_device.SetTopologyType(TopologyType::TriangleList);

		fullscreenVs.Set();
		m_contactShadowPs->Set();

		quad.Set(0);
		m_contactShadowBuffer->BindToStage(ShaderType::PixelShader, 2);

		m_device.Draw(6, 0);

		// Release the SRVs so they cannot collide with render target bindings later. The stencil
		// view in particular aliases the depth buffer, which is bound as a DSV during the G-Buffer
		// pass of the next frame.
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 1);
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 2);

		// --- Separable bilateral blur ---------------------------------------------------
		// Resolves the position-keyed jitter. Horizontal into m_blurRT, then vertical back into
		// m_termRT so GetResult() always names the finished term.
		m_contactShadowBlurPs->Set();
		m_contactShadowBlurBuffer->BindToStage(ShaderType::PixelShader, 3);

		ContactShadowBlurConstants blurConstants{};
		blurConstants.invScreenWidth = constants.invScreenWidth;
		blurConstants.invScreenHeight = constants.invScreenHeight;

		// Horizontal: m_termRT -> m_blurRT.
		blurConstants.directionX = 1.0f;
		blurConstants.directionY = 0.0f;
		m_contactShadowBlurBuffer->Update(&blurConstants);

		m_blurRT->Activate();
		m_blurRT->Clear(ClearFlags::Color);
		m_device.SetViewport(0, 0, static_cast<int32>(m_targetWidth), static_cast<int32>(m_targetHeight), 0.0f, 1.0f);
		m_device.BindTexture(m_termRT, ShaderType::PixelShader, 0);
		gbufferNormalRT.Bind(ShaderType::PixelShader, 1);
		m_device.Draw(6, 0);
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);

		// Vertical: m_blurRT -> m_termRT.
		blurConstants.directionX = 0.0f;
		blurConstants.directionY = 1.0f;
		m_contactShadowBlurBuffer->Update(&blurConstants);

		m_termRT->Activate();
		m_termRT->Clear(ClearFlags::Color);
		m_device.SetViewport(0, 0, static_cast<int32>(m_targetWidth), static_cast<int32>(m_targetHeight), 0.0f, 1.0f);
		m_device.BindTexture(m_blurRT, ShaderType::PixelShader, 0);
		gbufferNormalRT.Bind(ShaderType::PixelShader, 1);
		m_device.Draw(6, 0);

		// Release SRVs so they cannot collide with render target bindings next frame.
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 1);
	}
}
