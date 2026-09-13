// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bloom_pass.h"

// --- Shader bytecode seam ---------------------------------------------------------------
// Mirrors the seam in ssao_pass.cpp; the deferred path is D3D11-only today.
#ifdef _WIN32
#	include <Windows.h>
#	include "shaders/PS_BloomDownsample.h"
#	include "shaders/PS_BloomUpsample.h"
#	define MMO_BLOOM_DOWN_PS_BYTECODE g_PS_BloomDownsample
#	define MMO_BLOOM_DOWN_PS_SIZE std::size(g_PS_BloomDownsample)
#	define MMO_BLOOM_UP_PS_BYTECODE g_PS_BloomUpsample
#	define MMO_BLOOM_UP_PS_SIZE std::size(g_PS_BloomUpsample)
#else
#	define MMO_BLOOM_DOWN_PS_BYTECODE nullptr
#	define MMO_BLOOM_DOWN_PS_SIZE 0
#	define MMO_BLOOM_UP_PS_BYTECODE nullptr
#	define MMO_BLOOM_UP_PS_SIZE 0
#endif
// ---------------------------------------------------------------------------------------

#include <string>

namespace mmo
{
	namespace
	{
		/// @brief Mirrors BloomDownsampleBuffer in PS_BloomDownsample.hlsl (b2).
		struct alignas(16) BloomDownsampleConstants
		{
			float texelWidth;
			float texelHeight;
			uint32 applyPrefilter;
			float threshold;

			float knee;
			float padding0;
			float padding1;
			float padding2;
		};

		/// @brief Mirrors BloomUpsampleBuffer in PS_BloomUpsample.hlsl (b2).
		struct alignas(16) BloomUpsampleConstants
		{
			float lowerTexelWidth;
			float lowerTexelHeight;
			float radius;
			float padding0;
		};

		static_assert(sizeof(BloomDownsampleConstants) == 32, "BloomDownsampleConstants must match the HLSL layout");
		static_assert(sizeof(BloomUpsampleConstants) == 16, "BloomUpsampleConstants must match the HLSL layout");
	}

	BloomPass::BloomPass(GraphicsDevice& device, const uint32 width, const uint32 height)
		: m_device(device)
		, m_sceneWidth(width)
		, m_sceneHeight(height)
	{
		m_downsampleBuffer = m_device.CreateConstantBuffer(sizeof(BloomDownsampleConstants), nullptr);
		m_upsampleBuffer = m_device.CreateConstantBuffer(sizeof(BloomUpsampleConstants), nullptr);
		ASSERT(m_downsampleBuffer && m_upsampleBuffer);

		m_downsamplePs = m_device.CreateShader(ShaderType::PixelShader, MMO_BLOOM_DOWN_PS_BYTECODE, MMO_BLOOM_DOWN_PS_SIZE);
		m_upsamplePs = m_device.CreateShader(ShaderType::PixelShader, MMO_BLOOM_UP_PS_BYTECODE, MMO_BLOOM_UP_PS_SIZE);
		ASSERT(m_downsamplePs && m_upsamplePs);
	}

	void BloomPass::Resize(const uint32 width, const uint32 height)
	{
		m_sceneWidth = width;
		m_sceneHeight = height;
		ReleaseTargets();
	}

	void BloomPass::ReleaseTargets()
	{
		m_downTargets.clear();
		m_upTargets.clear();
		m_levelWidths.clear();
		m_levelHeights.clear();
		m_builtDivisor = 0;
		m_builtLevels = 0;
		m_builtWidth = 0;
		m_builtHeight = 0;
	}

	void BloomPass::EnsureTargets()
	{
		const uint32 divisor = m_settings.startDivisor > 0 ? m_settings.startDivisor : 1u;
		const uint32 levels = m_settings.levelCount;

		if (!m_downTargets.empty() && m_builtDivisor == divisor && m_builtLevels == levels
			&& m_builtWidth == m_sceneWidth && m_builtHeight == m_sceneHeight)
		{
			return;
		}

		ReleaseTargets();

		uint32 levelWidth = m_sceneWidth / divisor > 0 ? m_sceneWidth / divisor : 1u;
		uint32 levelHeight = m_sceneHeight / divisor > 0 ? m_sceneHeight / divisor : 1u;

		for (uint32 level = 0; level < levels; ++level)
		{
			m_levelWidths.push_back(levelWidth);
			m_levelHeights.push_back(levelHeight);

			m_downTargets.push_back(m_device.CreateRenderTexture("BloomDown" + std::to_string(level),
				static_cast<uint16>(levelWidth), static_cast<uint16>(levelHeight),
				RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, PixelFormat::R16G16B16A16));
			ASSERT(m_downTargets.back());

			// The coarsest level has no upsample target: it is the start of the upsample chain.
			if (level + 1 < levels)
			{
				m_upTargets.push_back(m_device.CreateRenderTexture("BloomUp" + std::to_string(level),
					static_cast<uint16>(levelWidth), static_cast<uint16>(levelHeight),
					RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, PixelFormat::R16G16B16A16));
				ASSERT(m_upTargets.back());
			}

			levelWidth = levelWidth / 2 > 0 ? levelWidth / 2 : 1u;
			levelHeight = levelHeight / 2 > 0 ? levelHeight / 2 : 1u;
		}

		m_builtDivisor = divisor;
		m_builtLevels = levels;
		m_builtWidth = m_sceneWidth;
		m_builtHeight = m_sceneHeight;
	}

	TexturePtr BloomPass::GetResult() const
	{
		if (!m_settings.IsEnabled() || m_downTargets.empty())
		{
			return nullptr;
		}

		return m_upTargets.empty() ? TexturePtr(m_downTargets.front()) : TexturePtr(m_upTargets.front());
	}

	float BloomPass::GetResultScale() const
	{
		return m_settings.levelCount > 0 ? m_settings.intensity / static_cast<float>(m_settings.levelCount) : 0.0f;
	}

	void BloomPass::Render(RenderTexture& sceneColor, VertexBuffer& quad, ShaderBase& fullscreenVs)
	{
		if (!m_settings.IsEnabled())
		{
			if (!m_downTargets.empty())
			{
				ReleaseTargets();
			}

			return;
		}

		EnsureTargets();

		m_device.SetDepthEnabled(false);
		m_device.SetDepthWriteEnabled(false);
		m_device.SetFillMode(FillMode::Solid);
		m_device.SetFaceCullMode(FaceCullMode::None);
		m_device.SetTextureAddressMode(TextureAddressMode::Clamp, TextureAddressMode::Clamp, TextureAddressMode::Clamp);
		m_device.SetTextureFilter(TextureFilter::Bilinear);
		m_device.SetVertexFormat(VertexFormat::PosColorTex1);
		m_device.SetTopologyType(TopologyType::TriangleList);
		fullscreenVs.Set();
		quad.Set(0);

		// --- Downsample chain -----------------------------------------------------------
		m_downsamplePs->Set();
		m_downsampleBuffer->BindToStage(ShaderType::PixelShader, 2);

		const uint32 levels = static_cast<uint32>(m_downTargets.size());
		for (uint32 level = 0; level < levels; ++level)
		{
			BloomDownsampleConstants constants{};
			if (level == 0)
			{
				// Taps are spaced so the 13-tap footprint covers the whole start divisor.
				const float tapScale = static_cast<float>(m_builtDivisor) * 0.5f;
				constants.texelWidth = tapScale / static_cast<float>(m_sceneWidth);
				constants.texelHeight = tapScale / static_cast<float>(m_sceneHeight);
				constants.applyPrefilter = 1;
			}
			else
			{
				constants.texelWidth = 1.0f / static_cast<float>(m_levelWidths[level - 1]);
				constants.texelHeight = 1.0f / static_cast<float>(m_levelHeights[level - 1]);
				constants.applyPrefilter = 0;
			}

			constants.threshold = m_settings.threshold;
			constants.knee = m_settings.knee;
			m_downsampleBuffer->Update(&constants);

			m_downTargets[level]->Activate();
			m_device.SetViewport(0, 0, static_cast<int32>(m_levelWidths[level]), static_cast<int32>(m_levelHeights[level]), 0.0f, 1.0f);

			if (level == 0)
			{
				sceneColor.Bind(ShaderType::PixelShader, 0);
			}
			else
			{
				m_device.BindTexture(m_downTargets[level - 1], ShaderType::PixelShader, 0);
			}

			m_device.Draw(6, 0);
			m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);
		}

		// --- Upsample chain: up[i] = down[i] + tent(coarser) ------------------------------
		m_upsamplePs->Set();
		m_upsampleBuffer->BindToStage(ShaderType::PixelShader, 2);

		for (int32 level = static_cast<int32>(levels) - 2; level >= 0; --level)
		{
			const uint32 coarser = static_cast<uint32>(level) + 1;
			const TexturePtr lower = (coarser == levels - 1) ? TexturePtr(m_downTargets[coarser]) : TexturePtr(m_upTargets[coarser]);

			BloomUpsampleConstants constants{};
			constants.lowerTexelWidth = 1.0f / static_cast<float>(m_levelWidths[coarser]);
			constants.lowerTexelHeight = 1.0f / static_cast<float>(m_levelHeights[coarser]);
			constants.radius = 1.0f;
			m_upsampleBuffer->Update(&constants);

			m_upTargets[level]->Activate();
			m_device.SetViewport(0, 0, static_cast<int32>(m_levelWidths[level]), static_cast<int32>(m_levelHeights[level]), 0.0f, 1.0f);
			m_device.BindTexture(lower, ShaderType::PixelShader, 0);
			m_device.BindTexture(m_downTargets[level], ShaderType::PixelShader, 1);
			m_device.Draw(6, 0);
			m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);
			m_device.BindTexture(nullptr, ShaderType::PixelShader, 1);
		}
	}
}
