// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "g_buffer.h"
#include "graphics_device.h"
#include "log/default_log_levels.h"

namespace mmo
{
	GBuffer::GBuffer(GraphicsDevice& device, uint32 width, uint32 height)
		: m_device(device)
		, m_width(width)
		, m_height(height)
	{
		// Create render textures for the G-Buffer
		// G-Buffer layout. Formats are kept as small as the data allows because the geometry pass
		// writes every one of these targets at full resolution and the lighting pass reads them all
		// back — on a bandwidth-limited GPU (e.g. integrated) the G-Buffer footprint dominates.
		//   Albedo   : RGBA16F  (HDR-capable base colour + opacity in alpha)
		//   Normal   : RGBA16F  (world-space normal*0.5+0.5 in rgb, linear view depth in alpha)
		//   Material : RGBA8    (metallic / roughness / specular / ao)
		//   Emissive : RGBA16F  (HDR emissive)
		// Normal was previously RGBA32F (16 bytes); half-float is more than enough precision for a
		// unit normal and linear depth, halving its footprint. The former ViewRay target (another
		// full-screen RGBA32F) has been removed entirely: the lighting pass now reconstructs the
		// view-space ray from the inverse projection matrix and the screen UV instead of reading it
		// back from a G-Buffer texture. Together this cuts G-Buffer write bandwidth from 52 to 28
		// bytes/pixel — a major win on bandwidth-limited (e.g. integrated) GPUs.
		m_albedoRT = m_device.CreateRenderTexture("GBuffer_Albedo", width, height, RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, PixelFormat::R16G16B16A16);
		m_normalRT = m_device.CreateRenderTexture("GBuffer_Normal", width, height, RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, PixelFormat::R16G16B16A16);
		m_materialRT = m_device.CreateRenderTexture("GBuffer_Material", width, height, RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, PixelFormat::R8G8B8A8);
		m_emissiveRT = m_device.CreateRenderTexture("GBuffer_Emissive", width, height, RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, PixelFormat::R16G16B16A16);
		m_depthRT = m_device.CreateRenderTexture("GBuffer_Depth", width, height, RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView);

		// Check if all render textures were created successfully
		if (!m_albedoRT || !m_normalRT || !m_materialRT || !m_emissiveRT || !m_depthRT)
		{
			ELOG("Failed to create G-Buffer render textures");
			throw std::runtime_error("Failed to create G-Buffer render textures");
		}
	}

	void GBuffer::Resize(uint32 width, uint32 height)
	{
		// Skip if the size hasn't changed
		if (m_width == width && m_height == height)
			return;

		// Update dimensions
		m_width = width;
		m_height = height;

		// Recreate render textures
		m_albedoRT->Resize(width, height);
		m_normalRT->Resize(width, height);
		m_materialRT->Resize(width, height);
		m_emissiveRT->Resize(width, height);
		m_depthRT->Resize(width, height);
	}

	void GBuffer::Bind(const bool clearDepth)
	{
		// Create an array of render target pointers
		RenderTexturePtr renderTargets[] = {
			m_albedoRT,
			m_normalRT,
			m_materialRT,
			m_emissiveRT
		};

		m_albedoRT->ApplyPendingResize();
		m_normalRT->ApplyPendingResize();
		m_materialRT->ApplyPendingResize();
		m_emissiveRT->ApplyPendingResize();
		m_depthRT->ApplyPendingResize();

		// Set the render targets with depth stencil
		m_device.SetRenderTargetsWithDepthStencil(renderTargets, 4, m_depthRT);

		// Set the viewport to match the G-Buffer size
		m_device.SetViewport(0, 0, m_width, m_height, 0.0f, 1.0f);

		if (clearDepth)
		{
			m_depthRT->Clear(ClearFlags::DepthStencil);
		}
	}

	void GBuffer::BindDepthOnly()
	{
		m_depthRT->ApplyPendingResize();

		// Bind only the depth target, no colour render targets.
		m_device.SetRenderTargetsWithDepthStencil(nullptr, 0, m_depthRT);
		m_device.SetViewport(0, 0, m_width, m_height, 0.0f, 1.0f);
		m_depthRT->Clear(ClearFlags::DepthStencil);
	}

	void GBuffer::Unbind()
	{
		// Reset render targets
		m_device.SetRenderTargets(nullptr, 0);
	}
}
