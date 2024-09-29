// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

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
