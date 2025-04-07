// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "render_texture.h"

#include <utility>


namespace mmo
{
	RenderTexture::RenderTexture(std::string name, uint16 width, uint16 height, PixelFormat format) noexcept
		: RenderTarget(std::move(name), width, height)
		, m_format(format)
	{
	}
}
