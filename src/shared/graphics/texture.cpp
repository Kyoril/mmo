// Copyright (C) 2019, Robin Klimonow. All rights reserved.

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
}
