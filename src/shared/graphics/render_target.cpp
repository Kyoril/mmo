// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "render_target.h"
#include "graphics_device.h"

#include <utility>

namespace mmo
{
	RenderTarget::RenderTarget(std::string name, uint16 width, uint16 height) noexcept
		: m_name(std::move(name))
		, m_width(width)
		, m_height(height)
	{
	}

	void RenderTarget::Activate()
	{
		GraphicsDevice::Get().RenderTargetActivated(shared_from_this());
	}

}