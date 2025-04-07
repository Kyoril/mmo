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
		RenderTexture(std::string name, uint16 width, uint16 height, PixelFormat format = PixelFormat::R8G8B8A8) noexcept;
		~RenderTexture() noexcept override = default;

	public:
		virtual TexturePtr StoreToTexture() = 0;

		/// Gets the pixel format of the render texture.
		PixelFormat GetFormat() const { return m_format; }

	protected:
		PixelFormat m_format;
	};

	typedef std::shared_ptr<RenderTexture> RenderTexturePtr;
}
