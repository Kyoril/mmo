// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "null_device.h"

#include "graphics/graphics_device.h"

namespace mmo::test
{
	GraphicsDevice& EnsureNullDevice()
	{
		static GraphicsDevice& device = GraphicsDevice::CreateNull(GraphicsDeviceDesc());
		return device;
	}
}
