// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "render_window_null.h"
#include "graphics_device_null.h"

#include <utility>

#include "log/default_log_levels.h"


namespace mmo
{
	RenderWindowNull::RenderWindowNull(GraphicsDeviceNull & device, std::string name, uint16 width, uint16 height, bool fullScreen)
		: RenderWindow(std::move(name), width, height)
		, RenderTargetNull(device)
	{
	}

	void RenderWindowNull::Activate()
	{
		RenderTarget::Activate();
		RenderTargetNull::Activate();

		m_device.SetViewport(0, 0, m_width, m_height, 0.0f, 1.0f);
	}

	void RenderWindowNull::Clear(ClearFlags flags)
	{
		RenderTargetNull::Clear(flags);
	}

	void RenderWindowNull::Resize(uint16 width, uint16 height)
	{
		if (width == 0) return;
		if (height == 0) return;
		if (width == m_width && height == m_height) return;
	}

	void RenderWindowNull::Update()
	{
	}

	void RenderWindowNull::SetTitle(const std::string & title)
	{

	}
}
