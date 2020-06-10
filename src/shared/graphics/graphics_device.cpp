// Copyright (C) 2019, Robin Klimonow. All rights reserved.

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

	void GraphicsDevice::SetTransformMatrix(TransformType type, Matrix4 const & matrix)
	{
		m_transform[(uint32)type] = matrix;
		m_transform[(uint32)type].Transpose();
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
}
