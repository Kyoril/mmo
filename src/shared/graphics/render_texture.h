// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "render_target.h"
#include "texture.h"


namespace mmo
{
	enum class RenderTextureFlags : uint8
	{
		None = 0,
		HasColorBuffer = 1 << 0,
		HasDepthBuffer = 1 << 1,
		ShaderResourceView = 1 << 2
	};

	inline RenderTextureFlags operator|(RenderTextureFlags a, RenderTextureFlags b)
	{
		return static_cast<RenderTextureFlags>(static_cast<uint8>(a) | static_cast<uint8>(b));
	}

	inline RenderTextureFlags operator&(RenderTextureFlags a, RenderTextureFlags b)
	{
		return static_cast<RenderTextureFlags>(static_cast<uint8>(a) & static_cast<uint8>(b));
	}

	/// Base class for a texture that can be used as a render target.
	class RenderTexture
		: public RenderTarget
		, public Texture
	{
	public:
		RenderTexture(std::string name, uint16 width, uint16 height, RenderTextureFlags flags, PixelFormat colorFormat = PixelFormat::R8G8B8A8, PixelFormat depthFormat = PixelFormat::D32F) noexcept;
		~RenderTexture() noexcept override = default;

	public:
		virtual TexturePtr StoreToTexture() = 0;

		/// Gets the pixel format of the render texture.
		PixelFormat GetColorFormat() const { return m_colorFormat; }

		PixelFormat GetDepthFormat() const { return m_depthFormat; }

		bool HasColorBuffer() const { return (m_flags & RenderTextureFlags::HasColorBuffer) != RenderTextureFlags::None; }

		bool HasDepthBuffer() const { return (m_flags & RenderTextureFlags::HasDepthBuffer) != RenderTextureFlags::None; }

		bool HasShaderResourceView() const { return (m_flags & RenderTextureFlags::ShaderResourceView) != RenderTextureFlags::None; }

	protected:
		RenderTextureFlags m_flags;

		PixelFormat m_colorFormat;

		PixelFormat m_depthFormat;
	};

	typedef std::shared_ptr<RenderTexture> RenderTexturePtr;
}
