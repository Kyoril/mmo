// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "atmosphere_pass.h"

#include "scene_graph/camera.h"

// --- Shader bytecode seam ---------------------------------------------------------------
// Mirrors the seam in ssao_pass.cpp; the deferred path is D3D11-only today.
#ifdef _WIN32
#	include <Windows.h>
#	include "shaders/PS_AtmosphereMarch.h"
#	include "shaders/PS_AtmosphereBlur.h"
#	include "shaders/PS_AtmosphereComposite.h"
#	define MMO_ATMOSPHERE_MARCH_PS_BYTECODE g_PS_AtmosphereMarch
#	define MMO_ATMOSPHERE_MARCH_PS_SIZE std::size(g_PS_AtmosphereMarch)
#	define MMO_ATMOSPHERE_BLUR_PS_BYTECODE g_PS_AtmosphereBlur
#	define MMO_ATMOSPHERE_BLUR_PS_SIZE std::size(g_PS_AtmosphereBlur)
#	define MMO_ATMOSPHERE_COMPOSITE_PS_BYTECODE g_PS_AtmosphereComposite
#	define MMO_ATMOSPHERE_COMPOSITE_PS_SIZE std::size(g_PS_AtmosphereComposite)
#else
#	define MMO_ATMOSPHERE_MARCH_PS_BYTECODE nullptr
#	define MMO_ATMOSPHERE_MARCH_PS_SIZE 0
#	define MMO_ATMOSPHERE_BLUR_PS_BYTECODE nullptr
#	define MMO_ATMOSPHERE_BLUR_PS_SIZE 0
#	define MMO_ATMOSPHERE_COMPOSITE_PS_BYTECODE nullptr
#	define MMO_ATMOSPHERE_COMPOSITE_PS_SIZE 0
#endif
// ---------------------------------------------------------------------------------------

namespace mmo
{
	namespace
	{
		/// @brief Mirrors AtmosphereMarchBuffer in PS_AtmosphereMarch.hlsl (b2).
		struct alignas(16) AtmosphereMarchConstants
		{
			float marchDistance;
			float skyDistance;
			uint32 stepCount;
			uint32 divisor;

			float fullWidth;
			float fullHeight;
			uint32 debugMode;
			float padding0;
		};

		/// @brief Mirrors AtmosphereBlurBuffer in PS_AtmosphereBlur.hlsl (b2).
		struct alignas(16) AtmosphereBlurConstants
		{
			float directionX;
			float directionY;
			int32 lowWidth;
			int32 lowHeight;

			uint32 divisor;
			float padding0;
			float padding1;
			float padding2;
		};

		/// @brief Mirrors AtmosphereCompositeBuffer in PS_AtmosphereComposite.hlsl (b2).
		struct alignas(16) AtmosphereCompositeConstants
		{
			float marchDistance;
			float skyDistance;
			uint32 divisor;
			uint32 debugMode;

			int32 lowWidth;
			int32 lowHeight;
			float padding0;
			float padding1;
		};

		static_assert(sizeof(AtmosphereMarchConstants) == 32, "AtmosphereMarchConstants must match the HLSL layout");
		static_assert(sizeof(AtmosphereBlurConstants) == 32, "AtmosphereBlurConstants must match the HLSL layout");
		static_assert(sizeof(AtmosphereCompositeConstants) == 32, "AtmosphereCompositeConstants must match the HLSL layout");
	}

	AtmospherePass::AtmospherePass(GraphicsDevice& device, const uint32 width, const uint32 height)
		: m_device(device)
		, m_gbufferWidth(width)
		, m_gbufferHeight(height)
	{
		m_marchBuffer = m_device.CreateConstantBuffer(sizeof(AtmosphereMarchConstants), nullptr);
		m_blurBuffer = m_device.CreateConstantBuffer(sizeof(AtmosphereBlurConstants), nullptr);
		m_compositeBuffer = m_device.CreateConstantBuffer(sizeof(AtmosphereCompositeConstants), nullptr);
		ASSERT(m_marchBuffer && m_blurBuffer && m_compositeBuffer);

		m_marchPs = m_device.CreateShader(ShaderType::PixelShader, MMO_ATMOSPHERE_MARCH_PS_BYTECODE, MMO_ATMOSPHERE_MARCH_PS_SIZE);
		m_blurPs = m_device.CreateShader(ShaderType::PixelShader, MMO_ATMOSPHERE_BLUR_PS_BYTECODE, MMO_ATMOSPHERE_BLUR_PS_SIZE);
		m_compositePs = m_device.CreateShader(ShaderType::PixelShader, MMO_ATMOSPHERE_COMPOSITE_PS_BYTECODE, MMO_ATMOSPHERE_COMPOSITE_PS_SIZE);
		ASSERT(m_marchPs && m_blurPs && m_compositePs);

		m_neutralTexture = m_device.CreateTexture(1, 1, BufferUsage::Static);
		ASSERT(m_neutralTexture);
		uint32 neutralPixel = 0xFF000000;
		m_neutralTexture->LoadRaw(&neutralPixel, sizeof(neutralPixel));
		m_neutralTexture->SetDebugName("AtmosphereNoMarch");
	}

	void AtmospherePass::Resize(const uint32 width, const uint32 height)
	{
		m_gbufferWidth = width;
		m_gbufferHeight = height;
		ReleaseTargets();
	}

	void AtmospherePass::ReleaseTargets()
	{
		m_marchRT.reset();
		m_blurRT.reset();
		m_targetWidth = 0;
		m_targetHeight = 0;
		m_targetDivisor = 0;
	}

	void AtmospherePass::EnsureTargets()
	{
		const uint32 divisor = m_settings.resolutionDivisor > 0 ? m_settings.resolutionDivisor : 1u;
		const uint32 desiredWidth = m_gbufferWidth / divisor > 0 ? m_gbufferWidth / divisor : 1u;
		const uint32 desiredHeight = m_gbufferHeight / divisor > 0 ? m_gbufferHeight / divisor : 1u;

		if (m_marchRT && m_targetWidth == desiredWidth && m_targetHeight == desiredHeight && m_targetDivisor == divisor)
		{
			return;
		}

		m_marchRT = m_device.CreateRenderTexture("AtmosphereMarch", static_cast<uint16>(desiredWidth), static_cast<uint16>(desiredHeight),
			RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, PixelFormat::R16G16B16A16);
		m_blurRT = m_device.CreateRenderTexture("AtmosphereBlur", static_cast<uint16>(desiredWidth), static_cast<uint16>(desiredHeight),
			RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, PixelFormat::R16G16B16A16);
		ASSERT(m_marchRT && m_blurRT);

		m_targetWidth = desiredWidth;
		m_targetHeight = desiredHeight;
		m_targetDivisor = divisor;
	}

	void AtmospherePass::Render(Camera& camera, RenderTexture& sceneColor, RenderTexture& gbufferNormalRT, RenderTexture& output,
		const std::array<RenderTexturePtr, NUM_SHADOW_CASCADES>& cascadeShadowMaps, ConstantBuffer& shadowBuffer,
		ConstantBuffer& cameraBuffer, const std::function<void()>& bindShadowSampler, VertexBuffer& quad, ShaderBase& fullscreenVs)
	{
		const bool marchEnabled = m_settings.IsMarchEnabled();
		if (marchEnabled)
		{
			EnsureTargets();
		}
		else if (m_marchRT)
		{
			// Off: hold no targets, so a disabled march costs no video memory.
			ReleaseTargets();
		}

		// The shaders rebuild view rays from b12.
		m_device.SetTransformMatrix(World, Matrix4::Identity);
		m_device.SetTransformMatrix(View, camera.GetViewMatrix());
		m_device.SetTransformMatrix(Projection, camera.GetProjectionMatrix());

		m_device.SetDepthEnabled(false);
		m_device.SetDepthWriteEnabled(false);
		m_device.SetFillMode(FillMode::Solid);
		m_device.SetFaceCullMode(FaceCullMode::None);
		m_device.SetTextureAddressMode(TextureAddressMode::Clamp, TextureAddressMode::Clamp, TextureAddressMode::Clamp);
		m_device.SetTextureFilter(TextureFilter::None);
		m_device.SetVertexFormat(VertexFormat::PosColorTex1);
		m_device.SetTopologyType(TopologyType::TriangleList);

		fullscreenVs.Set();
		quad.Set(0);
		cameraBuffer.BindToStage(ShaderType::PixelShader, 1);

		if (marchEnabled)
		{
			// --- March ------------------------------------------------------------------
			AtmosphereMarchConstants marchConstants{};
			marchConstants.marchDistance = m_settings.marchDistance;
			marchConstants.skyDistance = m_settings.skyDistance;
			marchConstants.stepCount = m_settings.stepCount;
			marchConstants.divisor = m_targetDivisor;
			marchConstants.fullWidth = static_cast<float>(m_gbufferWidth);
			marchConstants.fullHeight = static_cast<float>(m_gbufferHeight);
			marchConstants.debugMode = m_settings.debugMode;
			m_marchBuffer->Update(&marchConstants);

			m_marchRT->Activate();
			m_marchRT->Clear(ClearFlags::Color);
			m_device.SetViewport(0, 0, static_cast<int32>(m_targetWidth), static_cast<int32>(m_targetHeight), 0.0f, 1.0f);

			gbufferNormalRT.Bind(ShaderType::PixelShader, 1);
			for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
			{
				cascadeShadowMaps[i]->Bind(ShaderType::PixelShader, 5 + i);
			}

			shadowBuffer.BindToStage(ShaderType::PixelShader, 3);
			m_marchBuffer->BindToStage(ShaderType::PixelShader, 2);
			m_marchPs->Set();

			// Last, so no device state call above can replace the comparison sampler.
			bindShadowSampler();
			m_device.Draw(6, 0);

			for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
			{
				m_device.BindTexture(nullptr, ShaderType::PixelShader, 5 + i);
			}

			// --- Blur: march -> blur (horizontal) -> march (vertical) --------------------
			m_blurPs->Set();
			m_blurBuffer->BindToStage(ShaderType::PixelShader, 2);

			AtmosphereBlurConstants blurConstants{};
			blurConstants.lowWidth = static_cast<int32>(m_targetWidth);
			blurConstants.lowHeight = static_cast<int32>(m_targetHeight);
			blurConstants.divisor = m_targetDivisor;

			for (uint32 iteration = 0; iteration < m_settings.blurIterations; ++iteration)
			{
				blurConstants.directionX = 1.0f;
				blurConstants.directionY = 0.0f;
				m_blurBuffer->Update(&blurConstants);
				m_blurRT->Activate();
				m_device.SetViewport(0, 0, static_cast<int32>(m_targetWidth), static_cast<int32>(m_targetHeight), 0.0f, 1.0f);
				m_device.BindTexture(m_marchRT, ShaderType::PixelShader, 0);
				m_device.Draw(6, 0);
				m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);

				blurConstants.directionX = 0.0f;
				blurConstants.directionY = 1.0f;
				m_blurBuffer->Update(&blurConstants);
				m_marchRT->Activate();
				m_device.SetViewport(0, 0, static_cast<int32>(m_targetWidth), static_cast<int32>(m_targetHeight), 0.0f, 1.0f);
				m_device.BindTexture(m_blurRT, ShaderType::PixelShader, 0);
				m_device.Draw(6, 0);
				m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);
			}

			m_device.BindTexture(nullptr, ShaderType::PixelShader, 1);
		}

		// --- Composite ------------------------------------------------------------------
		AtmosphereCompositeConstants compositeConstants{};
		compositeConstants.marchDistance = marchEnabled ? m_settings.marchDistance : 0.0f;
		compositeConstants.skyDistance = m_settings.skyDistance;
		compositeConstants.divisor = marchEnabled ? m_targetDivisor : 1u;
		compositeConstants.debugMode = m_settings.debugMode;
		compositeConstants.lowWidth = marchEnabled ? static_cast<int32>(m_targetWidth) : 1;
		compositeConstants.lowHeight = marchEnabled ? static_cast<int32>(m_targetHeight) : 1;
		m_compositeBuffer->Update(&compositeConstants);

		output.Activate();
		m_device.SetViewport(0, 0, static_cast<int32>(m_gbufferWidth), static_cast<int32>(m_gbufferHeight), 0.0f, 1.0f);

		sceneColor.Bind(ShaderType::PixelShader, 0);
		gbufferNormalRT.Bind(ShaderType::PixelShader, 1);
		const TexturePtr marchResult = marchEnabled ? TexturePtr(m_marchRT) : m_neutralTexture;
		m_device.BindTexture(marchResult, ShaderType::PixelShader, 2);

		m_compositeBuffer->BindToStage(ShaderType::PixelShader, 2);
		m_compositePs->Set();
		m_device.Draw(6, 0);

		m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 1);
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 2);
	}
}
