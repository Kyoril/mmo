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
	{
	}

#ifdef _WIN32
	GraphicsDevice & GraphicsDevice::CreateD3D11()
	{
		assert(!s_currentDevice);

		// Allocate a new graphics device object
		s_currentDevice = std::make_unique<GraphicsDeviceD3D11>();

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
}
