// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "render_target.h"
#include "texture.h"


namespace mmo
{
	/// Base class for a texture that can be used as a render target.
	class RenderTexture
		: public RenderTarget
		, public Texture
	{
	public:
		RenderTexture(std::string name, uint16 width, uint16 height) noexcept;
		~RenderTexture() noexcept override = default;

	public:
		virtual TexturePtr StoreToTexture() = 0;
	};

	typedef std::shared_ptr<RenderTexture> RenderTexturePtr;
}