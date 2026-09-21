// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "tonemap_pass.h"

#include "color_grading.h"
#include "graphics/texture_mgr.h"
#include "graphics/sampler_state.h"
#include "log/default_log_levels.h"

#include <map>

// --- Shader bytecode seam ---------------------------------------------------------------
// Mirrors the seam in ssao_pass.cpp; the deferred path is D3D11-only today.
#ifdef _WIN32
#	include <Windows.h>
#	include "shaders/PS_Tonemap.h"
#	define MMO_TONEMAP_PS_BYTECODE g_PS_Tonemap
#	define MMO_TONEMAP_PS_SIZE std::size(g_PS_Tonemap)
#else
#	define MMO_TONEMAP_PS_BYTECODE nullptr
#	define MMO_TONEMAP_PS_SIZE 0
#endif
// ---------------------------------------------------------------------------------------

namespace mmo
{
	namespace
	{
		/// @brief Mirrors the TonemapBuffer cbuffer in PS_Tonemap.hlsl (b2).
		struct alignas(16) TonemapConstants
		{
			float exposure;
			float bloomScale;
			float ditherStrength;
			float saturation;

			float colorFilter[3];
			float contrast;

			float lutBlend;
			float lutSize;
			float lutFromSize;
			float gradingPadding;
		};

		static_assert(sizeof(TonemapConstants) == 48, "TonemapConstants must match the HLSL layout");
	}

	TonemapPass::TonemapPass(GraphicsDevice& device, const uint32 width, const uint32 height)
		: m_device(device)
		, m_width(width)
		, m_height(height)
	{
		m_tonemapBuffer = m_device.CreateConstantBuffer(sizeof(TonemapConstants), nullptr);
		ASSERT(m_tonemapBuffer);

		m_tonemapPs = m_device.CreateShader(ShaderType::PixelShader, MMO_TONEMAP_PS_BYTECODE, MMO_TONEMAP_PS_SIZE);
		ASSERT(m_tonemapPs);

		// R16G16B16A16 like the scene target: the underwater pass reads this output and its sun
		// shafts add above 1.0.
		m_outputRT = m_device.CreateRenderTexture("TonemapOutput", static_cast<uint16>(width), static_cast<uint16>(height),
			RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, PixelFormat::R16G16B16A16);
		ASSERT(m_outputRT);

		m_blackTexture = m_device.CreateTexture(1, 1, BufferUsage::Static);
		ASSERT(m_blackTexture);
		uint32 blackPixel = 0xFF000000;
		m_blackTexture->LoadRaw(&blackPixel, sizeof(blackPixel));
		m_blackTexture->SetDebugName("TonemapNoBloom");

		SamplerDesc lutSampler;
		lutSampler.filter = SamplerFilter::Linear;
		lutSampler.address = SamplerAddress::Clamp;
		m_lutSampler = m_device.CreateSamplerState(lutSampler);
		ASSERT(m_lutSampler);
	}

	void TonemapPass::Resize(const uint32 width, const uint32 height)
	{
		m_width = width;
		m_height = height;
		m_outputRT->Resize(width, height);
	}

	void TonemapPass::Render(RenderTexture& hdrScene, const TexturePtr& bloom, const float bloomScale, VertexBuffer& quad, ShaderBase& fullscreenVs)
	{
		TonemapConstants constants{};
		constants.exposure = m_settings.exposure;
		constants.bloomScale = bloom ? bloomScale : 0.0f;
		constants.ditherStrength = m_settings.ditherStrength;

		const ColorGradingSettings& grading = m_settings.grading;
		constants.saturation = grading.saturation;
		constants.colorFilter[0] = grading.colorFilter.x;
		constants.colorFilter[1] = grading.colorFilter.y;
		constants.colorFilter[2] = grading.colorFilter.z;
		constants.contrast = grading.contrast;
		constants.lutBlend = m_lutBlend;
		constants.lutSize = m_lut ? static_cast<float>(m_lut->GetHeight()) : 0.0f;
		constants.lutFromSize = m_lutFrom ? static_cast<float>(m_lutFrom->GetHeight()) : 0.0f;
		constants.gradingPadding = 0.0f;

		m_tonemapBuffer->Update(&constants);

		m_outputRT->Activate();
		m_device.SetViewport(0, 0, static_cast<int32>(m_width), static_cast<int32>(m_height), 0.0f, 1.0f);

		// The forward pass earlier in the frame may leave alpha blending enabled (its last
		// translucent material). This pass writes a full-screen quad and must not blend with
		// whatever was left in m_outputRT.
		m_device.SetBlendMode(BlendMode::Opaque);

		m_device.SetDepthEnabled(false);
		m_device.SetDepthWriteEnabled(false);
		m_device.SetFillMode(FillMode::Solid);
		m_device.SetFaceCullMode(FaceCullMode::None);
		m_device.SetTextureAddressMode(TextureAddressMode::Clamp, TextureAddressMode::Clamp, TextureAddressMode::Clamp);
		m_device.SetTextureFilter(TextureFilter::Bilinear);

		hdrScene.Bind(ShaderType::PixelShader, 0);
		m_device.BindTexture(bloom ? bloom : m_blackTexture, ShaderType::PixelShader, 1);
		m_device.BindTexture(m_lut ? m_lut : m_blackTexture, ShaderType::PixelShader, 2);
		m_device.BindTexture(m_lutFrom ? m_lutFrom : m_blackTexture, ShaderType::PixelShader, 3);
		if (m_lutSampler)
		{
			m_lutSampler->Bind(ShaderType::PixelShader, 4);
		}

		m_device.SetVertexFormat(VertexFormat::PosColorTex1);
		m_device.SetTopologyType(TopologyType::TriangleList);
		fullscreenVs.Set();
		m_tonemapPs->Set();
		quad.Set(0);
		m_tonemapBuffer->BindToStage(ShaderType::PixelShader, 2);

		m_device.Draw(6, 0);

		m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 1);
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 2);
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 3);
	}

	void TonemapPass::SetLuts(const String& lut, const String& lutFrom, const float blend)
	{
		m_lut = ResolveLut(lut);
		m_lutFrom = ResolveLut(lutFrom);
		m_lutBlend = std::clamp(blend, 0.0f, 1.0f);
	}

	TexturePtr TonemapPass::ResolveLut(const String& path)
	{
		if (path.empty())
		{
			return nullptr;
		}

		if (const auto it = m_lutCache.find(path); it != m_lutCache.end())
		{
			return it->second;
		}

		TexturePtr texture = TextureManager::Get().CreateOrRetrieve(path);
		if (!texture)
		{
			WLOG("Colour grading LUT '" << path << "' could not be loaded; grading without it.");
		}
		else if (!color_grading::IsStripLut(texture->GetWidth(), texture->GetHeight()))
		{
			WLOG("Colour grading LUT '" << path << "' is " << texture->GetWidth() << "x" << texture->GetHeight()
				<< ", expected a strip of N*N x N (256x16 or 1024x32); grading without it.");
			texture = nullptr;
		}

		// Failures are cached too, so each bad path warns once.
		m_lutCache[path] = texture;
		return texture;
	}
}
