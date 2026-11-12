// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "upscale_pass.h"

#include "graphics/vertex_format.h"

#include <iterator>

// --- Shader bytecode seam ---------------------------------------------------------------
// The only backend-bound fact in this file: which compiled blobs to hand CreateShader. Mirrors
// the seam in post_process_pass.cpp; the Metal build generates the same symbols from the HLSL.
#if defined(_WIN32) || defined(__APPLE__)
#	ifdef _WIN32
#		include <Windows.h>
#		include "shaders/VS_DeferredLighting.h"
#		include "shaders/PS_Upscale.h"
#		include "shaders/PS_FsrEasu.h"
#		include "shaders/PS_FsrRcas.h"
#	else
#		include "VS_DeferredLighting.metal.h"
#		include "PS_Upscale.metal.h"
#		include "PS_FsrEasu.metal.h"
#		include "PS_FsrRcas.metal.h"
#	endif
#	define MMO_UPSCALE_SHADER(name) g_##name, std::size(g_##name)
#else
#	define MMO_UPSCALE_SHADER(name) nullptr, 0
#endif
// ---------------------------------------------------------------------------------------

namespace mmo
{
	namespace
	{
		/// @brief Mirrors the UpscaleBuffer cbuffer in UpscaleCommon.hlsli (b2).
		struct alignas(16) UpscaleConstants
		{
			float sourceWidth;
			float sourceHeight;
			float invSourceWidth;
			float invSourceHeight;

			float outputWidth;
			float outputHeight;
			float sourcePerOutputX;
			float sourcePerOutputY;

			float sharpness;
			float mode;
			float padding0;
			float padding1;
		};

		/// Output format of the passes. The input is display-encoded and already dithered, and the
		/// world frame is shown on an 8-bit back buffer, so 16-bit targets would only cost bandwidth.
		constexpr PixelFormat UpscaleFormat = PixelFormat::R8G8B8A8;
	}

	UpscalePass::UpscalePass(GraphicsDevice& device)
		: m_device(device)
	{
		const uint32 color = 0xFFFFFFFF;
		const POS_COL_TEX_VERTEX vertices[6]{
			{ { -1.0f,	-1.0f,		0.0f }, color, { 0.0f, 1.0f } },
			{ { -1.0f,	1.0f,		0.0f }, color, { 0.0f, 0.0f } },
			{ { 1.0f,	1.0f,		0.0f }, color, { 1.0f, 0.0f } },
			{ { 1.0f,	1.0f,		0.0f }, color, { 1.0f, 0.0f } },
			{ { 1.0f,	-1.0f,		0.0f }, color, { 1.0f, 1.0f } },
			{ { -1.0f,	-1.0f,		0.0f }, color, { 0.0f, 1.0f } }
		};
		m_quad = m_device.CreateVertexBuffer(6, sizeof(POS_COL_TEX_VERTEX), BufferUsage::StaticWriteOnly, vertices);
		ASSERT(m_quad);

		m_vertexShader = m_device.CreateShader(ShaderType::VertexShader, MMO_UPSCALE_SHADER(VS_DeferredLighting));
		m_upscalePs = m_device.CreateShader(ShaderType::PixelShader, MMO_UPSCALE_SHADER(PS_Upscale));
		m_easuPs = m_device.CreateShader(ShaderType::PixelShader, MMO_UPSCALE_SHADER(PS_FsrEasu));
		m_rcasPs = m_device.CreateShader(ShaderType::PixelShader, MMO_UPSCALE_SHADER(PS_FsrRcas));
		ASSERT(m_vertexShader && m_upscalePs && m_easuPs && m_rcasPs);

		m_constants = m_device.CreateConstantBuffer(sizeof(UpscaleConstants), nullptr);
		ASSERT(m_constants);
	}

	void UpscalePass::ReleaseTargets()
	{
		m_outputRT.reset();
		m_easuRT.reset();
		m_targetWidth = 0;
		m_targetHeight = 0;
	}

	void UpscalePass::EnsureTargets(const upscale_filter::Type filter, const uint32 width, const uint32 height)
	{
		if (m_targetWidth != width || m_targetHeight != height)
		{
			ReleaseTargets();
		}

		if (!m_outputRT)
		{
			m_outputRT = m_device.CreateRenderTexture("UpscaleOutput", static_cast<uint16>(width), static_cast<uint16>(height),
				RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, UpscaleFormat);
			ASSERT(m_outputRT);
		}

		if (filter == upscale_filter::Fsr)
		{
			if (!m_easuRT)
			{
				m_easuRT = m_device.CreateRenderTexture("UpscaleEasu", static_cast<uint16>(width), static_cast<uint16>(height),
					RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, UpscaleFormat);
				ASSERT(m_easuRT);
			}
		}
		else
		{
			// Only FSR needs the intermediate target; do not keep a full-resolution one around.
			m_easuRT.reset();
		}

		m_targetWidth = width;
		m_targetHeight = height;
	}

	void UpscalePass::DrawPass(const TexturePtr& source, RenderTexture& target, ShaderBase& pixelShader)
	{
		target.Activate();
		m_device.SetViewport(0, 0, static_cast<int32>(m_targetWidth), static_cast<int32>(m_targetHeight), 0.0f, 1.0f);

		m_device.BindTexture(source, ShaderType::PixelShader, 0);
		pixelShader.Set();
		m_device.Draw(6, 0);

		// Release the SRV: the source may be a render target again in the next pass or frame.
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);
	}

	TexturePtr UpscalePass::Render(const TexturePtr& source, const uint32 sourceWidth, const uint32 sourceHeight,
		const uint32 outputWidth, const uint32 outputHeight, const upscale_filter::Type filter, const float sharpness)
	{
		if (!source || !UpscaleNeedsPass(filter, sourceWidth, sourceHeight, outputWidth, outputHeight))
		{
			// Native resolution or bilinear: the caller shows the source directly. Hold no targets.
			ReleaseTargets();
			return source;
		}

		EnsureTargets(filter, outputWidth, outputHeight);

		UpscaleConstants constants{};
		constants.sourceWidth = static_cast<float>(sourceWidth);
		constants.sourceHeight = static_cast<float>(sourceHeight);
		constants.invSourceWidth = 1.0f / constants.sourceWidth;
		constants.invSourceHeight = 1.0f / constants.sourceHeight;
		constants.outputWidth = static_cast<float>(outputWidth);
		constants.outputHeight = static_cast<float>(outputHeight);
		constants.sourcePerOutputX = constants.sourceWidth / constants.outputWidth;
		constants.sourcePerOutputY = constants.sourceHeight / constants.outputHeight;
		constants.sharpness = sharpness;
		constants.mode = filter == upscale_filter::Bicubic ? 1.0f : 0.0f;

		m_device.SetDepthEnabled(false);
		m_device.SetDepthWriteEnabled(false);
		m_device.SetBlendMode(BlendMode::Opaque);
		m_device.SetFillMode(FillMode::Solid);
		m_device.SetFaceCullMode(FaceCullMode::None);
		m_device.SetTextureAddressMode(TextureAddressMode::Clamp, TextureAddressMode::Clamp, TextureAddressMode::Clamp);
		m_device.SetTextureFilter(TextureFilter::Bilinear);
		m_device.SetVertexFormat(VertexFormat::PosColorTex1);
		m_device.SetTopologyType(TopologyType::TriangleList);

		m_vertexShader->Set();
		m_quad->Set(0);

		if (filter == upscale_filter::Fsr)
		{
			// EASU reads the internal-resolution frame, RCAS the EASU result at output resolution.
			m_constants->Update(&constants);
			m_constants->BindToStage(ShaderType::PixelShader, 2);
			DrawPass(source, *m_easuRT, *m_easuPs);

			constants.sourceWidth = constants.outputWidth;
			constants.sourceHeight = constants.outputHeight;
			constants.invSourceWidth = 1.0f / constants.outputWidth;
			constants.invSourceHeight = 1.0f / constants.outputHeight;
			constants.sourcePerOutputX = 1.0f;
			constants.sourcePerOutputY = 1.0f;
			m_constants->Update(&constants);
			m_constants->BindToStage(ShaderType::PixelShader, 2);
			DrawPass(m_easuRT, *m_outputRT, *m_rcasPs);
		}
		else
		{
			m_constants->Update(&constants);
			m_constants->BindToStage(ShaderType::PixelShader, 2);
			DrawPass(source, *m_outputRT, *m_upscalePs);
		}

		return m_outputRT;
	}
}
