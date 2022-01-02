// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "graphics_device.h"
#ifdef _WIN32
#	include "d3d11/graphics_device_d3d11.h"
#endif

#include <memory>
#include <cassert>

namespace mmo
{
	/// The current graphics device object instance.
	static std::unique_ptr<GraphicsDevice> s_currentDevice;


	GraphicsDevice::GraphicsDevice()
		: m_viewX(0)
		, m_viewY(0)
		, m_viewW(1600)
		, m_viewH(900)
		, m_viewMinZ(0.001f)
		, m_viewMaxZ(100.0f)
		, m_fillMode(FillMode::Solid)
		, m_cullMode(FaceCullMode::Back)
		, m_texAddressMode(TextureAddressMode::Wrap)
		, m_texFilter(TextureFilter::Anisotropic)
	{
	}

#ifdef _WIN32
	GraphicsDevice & GraphicsDevice::CreateD3D11(const GraphicsDeviceDesc& desc)
	{
		assert(!s_currentDevice);

		// Allocate a new graphics device object
		s_currentDevice = std::make_unique<GraphicsDeviceD3D11>();
		s_currentDevice->Create(desc);

		return *s_currentDevice;
	}
#endif

	GraphicsDevice & GraphicsDevice::Get()
	{
		assert(s_currentDevice);
		return *s_currentDevice;
	}

	void GraphicsDevice::Destroy()
	{
		s_currentDevice.reset();
	}

	void GraphicsDevice::SetClearColor(uint32 clearColor)
	{
		m_clearColor = clearColor;
	}

	void GraphicsDevice::Create(const GraphicsDeviceDesc& desc)
	{
		m_viewW = desc.width;
		m_viewH = desc.height;
	}

	void GraphicsDevice::SetTopologyType(TopologyType InType)
	{
		m_topologyType = InType;
	}

	void GraphicsDevice::SetBlendMode(BlendMode InBlendMode)
	{
		m_blendMode = InBlendMode;
	}

	void GraphicsDevice::CaptureState()
	{
		// Remember transformation matrices
		for (uint32 i = 0; i < TransformType::Count; ++i)
		{
			m_restoreTransforms[i] = m_transform[i];
		}

		// Save blend mode
		m_restoreBlendMode = m_blendMode;

		// Remember the current render target
		m_restoreRenderTarget = m_renderTarget;

		m_restoreFillMode = m_fillMode;
		m_restoreCullMode = m_cullMode;
		
		m_restoreTexAddressMode = m_texAddressMode;
		m_restoreTexFilter = m_texFilter;

		m_captureTopologyType = m_topologyType;
	}

	void GraphicsDevice::RestoreState()
	{
		// Restore transformation matrices
		for (uint32 i = 0; i < TransformType::Count; ++i)
		{
			m_transform[i] = m_restoreTransforms[i];
		}

		// Restore blend mode
		if (m_blendMode != m_restoreBlendMode)
		{
			SetBlendMode(m_restoreBlendMode);
		}

		// Reactivate old render target if it has changed and if there was an old
		// render target (which should almost every time be the case but whatever)
		if (m_restoreRenderTarget && 
			m_restoreRenderTarget != m_renderTarget)
		{
			m_restoreRenderTarget->Activate();
		}

		if (m_restoreFillMode != m_fillMode)
		{
			SetFillMode(m_restoreFillMode);
		}

		if (m_restoreCullMode != m_cullMode)
		{
			SetFaceCullMode(m_restoreCullMode);
		}

		if (m_restoreTexAddressMode != m_texAddressMode)
		{
			SetTextureAddressMode(m_restoreTexAddressMode);
		}
		
		if (m_restoreTexFilter != m_texFilter)
		{
			SetTextureFilter(m_restoreTexFilter);
		}

		if (m_topologyType != m_captureTopologyType)
		{
			SetTopologyType(m_captureTopologyType);
		}
		
		// No longer keep the old render target referenced so that it is allowed
		// to be free'd.
		m_restoreRenderTarget = nullptr;
	}

	Matrix4 GraphicsDevice::GetTransformMatrix(TransformType type) const
	{
		return m_transform[(uint32)type];
	}

	void GraphicsDevice::SetTransformMatrix(TransformType type, Matrix4 const & matrix)
	{
		m_transform[(uint32)type] = matrix;
	}

	void GraphicsDevice::GetViewport(int32 * x, int32 * y, int32 * w, int32 * h, float * minZ, float * maxZ)
	{
		if (x) *x = m_viewX;
		if (y) *y = m_viewY;
		if (w) *w = m_viewW;
		if (h) *h = m_viewH;
		if (minZ) *minZ = m_viewMinZ;
		if (maxZ) *maxZ = m_viewMaxZ;
	}

	void GraphicsDevice::SetViewport(int32 x, int32 y, int32 w, int32 h, float minZ, float maxZ)
	{
		m_viewX = x;
		m_viewY = y;
		m_viewW = w;
		m_viewH = h;
		m_viewMinZ = minZ;
		m_viewMaxZ = maxZ;
	}

	void GraphicsDevice::SetFillMode(FillMode mode)
	{
		m_fillMode = mode;
	}

	void GraphicsDevice::SetFaceCullMode(FaceCullMode mode)
	{
		m_cullMode = mode;
	}

	void GraphicsDevice::SetTextureAddressMode(TextureAddressMode mode)
	{
		m_texAddressMode = mode;
	}

	void GraphicsDevice::SetTextureFilter(TextureFilter filter)
	{
		m_texFilter = filter;
	}
}
