// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "texture_null.h"

namespace mmo
{
	TextureNull::TextureNull(GraphicsDeviceNull & device, uint16 width, uint16 height)
		: m_device(device)
	{
		m_header.width = width;
		m_header.height = height;
	}

	void TextureNull::Load(std::unique_ptr<std::istream>& stream)
	{
		// Load the texture infos from stream
		Texture::Load(stream);
		
	}

	void TextureNull::LoadRaw(void * data, size_t dataSize)
	{

	}

	uint32 TextureNull::GetMemorySize() const
	{
		// For now, all textures are uncompressed RGBAs
		switch (m_header.format)
		{
		case tex::v1_0::DXT1:
			return (m_header.width * m_header.height * sizeof(uint32)) / 8;
		case tex::v1_0::DXT5:
			return (m_header.width * m_header.height * sizeof(uint32)) / 4;
		default:
			return m_header.width * m_header.height * sizeof(uint32);
		}
	}
	
	void TextureNull::Bind(ShaderType shader, uint32 slot)
	{
		m_device.SetTextureAddressMode(GetTextureAddressModeU(), GetTextureAddressModeV(), GetTextureAddressModeW());
		m_device.SetTextureFilter(GetTextureFilter());
	}
}
