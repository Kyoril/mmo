// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "texture_metal.h"

namespace mmo
{
	TextureMetal::TextureMetal(GraphicsDeviceMetal & device, uint16 width, uint16 height)
		: m_device(device)
	{
		m_header.width = width;
		m_header.height = height;
	}

	void TextureMetal::Load(std::unique_ptr<std::istream>& stream)
	{
		// Load the texture infos from stream
		Texture::Load(stream);
		
	}

	void TextureMetal::LoadRaw(void * data, size_t dataSize)
	{

	}

    uint32 TextureMetal::GetPixelDataSize() const
    {
        return 0;
    }

    void* TextureMetal::GetRawTexture() const
    {
        return nullptr;
    }

    void TextureMetal::CopyPixelDataTo(uint8* destination)
    {
        
    }

	uint32 TextureMetal::GetMemorySize() const
	{
		switch (m_header.format)
		{
		case tex::v1_0::DXT1:
			return (m_header.width * m_header.height * sizeof(uint32)) / 8;
		case tex::v1_0::DXT5:
			return (m_header.width * m_header.height * sizeof(uint32)) / 4;
		case tex::v1_0::R8:
			return m_header.width * m_header.height * sizeof(uint8);
		case tex::v1_0::RG8:
			return m_header.width * m_header.height * sizeof(uint8) * 2;
		case tex::v1_0::BC4:
			return std::max(1, (m_header.width + 3) / 4) * std::max(1, (m_header.height + 3) / 4) * 8;
		case tex::v1_0::BC5:
			return std::max(1, (m_header.width + 3) / 4) * std::max(1, (m_header.height + 3) / 4) * 16;
		default:
			return m_header.width * m_header.height * sizeof(uint32);
		}
	}
	
	void TextureMetal::Bind(ShaderType shader, uint32 slot)
	{
		m_device.SetTextureAddressMode(GetTextureAddressModeU(), GetTextureAddressModeV(), GetTextureAddressModeW());
		m_device.SetTextureFilter(GetTextureFilter());
	}

	void TextureMetal::UpdateFromMemory(void* data, size_t dataSize)
	{

	}

}
