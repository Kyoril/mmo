// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "render_texture.h"

#include <utility>


namespace mmo
{
	RenderTexture::RenderTexture(std::string name, uint16 width, uint16 height, RenderTextureFlags flags, PixelFormat colorFormat, PixelFormat depthFormat)
		: RenderTarget(std::move(name), width, height)
		, m_flags(flags)
		, m_colorFormat(colorFormat)
		, m_depthFormat(depthFormat)
	{
	}
}
