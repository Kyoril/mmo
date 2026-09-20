// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "tonemap_pass.h"

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
			float padding0;
		};

		static_assert(sizeof(TonemapConstants) == 16, "TonemapConstants must match the HLSL layout");
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

		m_device.SetVertexFormat(VertexFormat::PosColorTex1);
		m_device.SetTopologyType(TopologyType::TriangleList);
		fullscreenVs.Set();
		m_tonemapPs->Set();
		quad.Set(0);
		m_tonemapBuffer->BindToStage(ShaderType::PixelShader, 2);

		m_device.Draw(6, 0);

		m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 1);
	}
}
