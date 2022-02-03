// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "render_target_null.h"
#include "graphics_device_null.h"


namespace mmo
{
	RenderTargetNull::RenderTargetNull(GraphicsDeviceNull & device) noexcept
		: m_device(device)
	{
	}

	void RenderTargetNull::Activate()
	{

	}

	void RenderTargetNull::Clear(ClearFlags flags)
	{

	}
}
