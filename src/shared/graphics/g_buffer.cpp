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
		m_albedoRT = m_device.CreateRenderTexture("GBuffer_Albedo", width, height, RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, PixelFormat::R16G16B16A16);
		m_normalRT = m_device.CreateRenderTexture("GBuffer_Normal", width, height, RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, PixelFormat::R32G32B32A32);
		m_materialRT = m_device.CreateRenderTexture("GBuffer_Material", width, height, RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, PixelFormat::R8G8B8A8);
		m_emissiveRT = m_device.CreateRenderTexture("GBuffer_Emissive", width, height, RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, PixelFormat::R16G16B16A16);
		m_viewRayRT = m_device.CreateRenderTexture("GBuffer_ViewRay", width, height, RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, PixelFormat::R32G32B32A32);
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
		m_viewRayRT->Resize(width, height);
		m_depthRT->Resize(width, height);
	}

	void GBuffer::Bind()
	{
		// Create an array of render target pointers
		RenderTexturePtr renderTargets[] = {
			m_albedoRT,
			m_normalRT,
			m_materialRT,
			m_emissiveRT,
			m_viewRayRT
		};

		m_albedoRT->ApplyPendingResize();
		m_normalRT->ApplyPendingResize();
		m_materialRT->ApplyPendingResize();
		m_emissiveRT->ApplyPendingResize();
		m_viewRayRT->ApplyPendingResize();
		m_depthRT->ApplyPendingResize();

		// Set the render targets with depth stencil
		m_device.SetRenderTargetsWithDepthStencil(renderTargets, 5, m_depthRT);

		// Set the viewport to match the G-Buffer size
		m_device.SetViewport(0, 0, m_width, m_height, 0.0f, 1.0f);

		m_depthRT->Clear(ClearFlags::DepthStencil);
	}

	void GBuffer::Unbind()
	{
		// Reset render targets
		m_device.SetRenderTargets(nullptr, 0);
	}
}
