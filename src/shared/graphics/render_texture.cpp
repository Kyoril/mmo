// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "render_texture.h"

#include <utility>


namespace mmo
{
	RenderTexture::RenderTexture(std::string name, uint16 width, uint16 height) noexcept
		: RenderTarget(std::move(name), width, height)
	{
	}
}
