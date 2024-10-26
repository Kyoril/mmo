// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "texture.h"

#include <stdexcept>


namespace mmo
{
	Texture::Texture()
		: m_header(tex::Version_1_0)
	{
	}

	void Texture::Load(std::unique_ptr<std::istream>& stream)
	{
		ASSERT(stream);

		// Generate stream source with reader object for header parsing
		io::StreamSource source{ *stream };
		io::Reader reader{ source };

		// Load the pre header
		tex::PreHeader preHeader;
		if (!tex::loadPreHeader(preHeader, reader))
		{
			throw std::runtime_error("Failed to load texture pre header. File might be damaged.");
		}

		if (preHeader.version != tex::Version_1_0)
		{
			throw std::runtime_error("Texture has unsupported file format version.");
		}

		// Load the texture header
		if (!tex::v1_0::loadHeader(m_header, reader))
		{
			throw std::runtime_error("Failed to load texture header. File might be damaged.");
		}
	}

	void Texture::Unload()
	{

	}

	PixelFormat Texture::GetPixelFormat() const
	{
		switch (m_header.format)
		{
		case tex::v1_0::RGB:
			return PixelFormat::R8G8B8A8;
		case tex::v1_0::RGBA:
			return PixelFormat::R8G8B8A8;
		case tex::v1_0::DXT1:
			return PixelFormat::DXT1;
		case tex::v1_0::DXT5:
			return PixelFormat::DXT5;
		case tex::v1_0::FLOAT_RGB:
			return PixelFormat::R32G32B32A32;
		case tex::v1_0::FLOAT_RGBA:
			return PixelFormat::R32G32B32A32;
		default:
			return PixelFormat::Unknown;
		}
	}

	void Texture::SetDebugName(String debugName)
	{
#ifdef _DEBUG
		m_debugName = std::move(debugName);
#endif
	}
}
