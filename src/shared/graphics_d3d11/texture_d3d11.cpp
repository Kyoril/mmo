// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "texture_d3d11.h"

#include "base/macros.h"

#include <vector>
#include <stdexcept>


namespace mmo
{
	TextureD3D11::TextureD3D11(GraphicsDeviceD3D11 & device, uint16 width, uint16 height)
		: m_device(device)
	{
		m_header.width = width;
		m_header.height = height;
	}

	void TextureD3D11::Load(std::unique_ptr<std::istream>& stream)
	{
		// Load the texture infos from stream
		Texture::Load(stream);

		// Check data
		if (m_header.mipmapOffsets[0] == 0 || m_header.mipmapLengths[0] == 0)
		{
			throw std::runtime_error("Invalid or missing texture data");
		}

		// Read mipmap into memory
		std::vector mipData(m_header.mipmapLengths.size(), std::vector<uint8>());
		for (uint32 i = 0; i < m_header.mipmapLengths.size(); ++i)
		{
			if (m_header.mipmapLengths[i] == 0)
			{
				break;
			}

			mipData[i].resize(m_header.mipmapLengths[i], 0);
			stream->seekg(m_header.mipmapOffsets[i], std::ios::beg);
			stream->read(reinterpret_cast<char*>(mipData[i].data()), m_header.mipmapLengths[i]);
		}
		
		// Obtain ID3D11Device object by casting
		ID3D11Device& dev = m_device;

		// Create texture description
		D3D11_TEXTURE2D_DESC td;
		ZeroMemory(&td, sizeof(td));
		td.ArraySize = 1;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		switch (m_header.format)
		{
		case tex::v1_0::RGB:
		case tex::v1_0::RGBA:
			td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			break;
		case tex::v1_0::DXT1:
			td.Format = DXGI_FORMAT_BC1_UNORM;
			break;
		case tex::v1_0::DXT5:
			td.Format = DXGI_FORMAT_BC3_UNORM;
			break;
		default:
			throw std::runtime_error("Unsupported texture format for d3d11 texture!");
			break;
		}

		td.Height = m_header.height;
		td.Width = m_header.width;

		// Determine number of mip levels
		td.MipLevels = 1;
		td.SampleDesc.Count = 1;
		td.SampleDesc.Quality = 0;
		td.Usage = D3D11_USAGE_IMMUTABLE;

		uint32 actualMipLevelCount = 0;

		// Prepare usage data
		D3D11_SUBRESOURCE_DATA data[16];
		ZeroMemory(&data, sizeof(data));
		for (uint32 mipLevel = 0; mipLevel < 16; ++mipLevel)
		{
			// Calculate size of mip level
			const int32 width = m_header.width >> mipLevel;
			const int32 height = m_header.height >> mipLevel;
			if (width <= 1 || height <= 1 || mipData[mipLevel].size() == 0)
			{
				break;
			}

			data[mipLevel].pSysMem = mipData[mipLevel].data();
			switch (m_header.format)
			{
			case tex::v1_0::DXT1:
				data[mipLevel].SysMemPitch = 16 * (width / 8);
				data[mipLevel].SysMemSlicePitch = data[mipLevel].SysMemPitch * (height / 8);
				break;
			case tex::v1_0::DXT5:
				data[mipLevel].SysMemPitch = 16 * (width / 4);
				data[mipLevel].SysMemSlicePitch = data[mipLevel].SysMemPitch * (height / 4);
				break;
			default:
				data[mipLevel].SysMemPitch = sizeof(uint32) * width;
				break;
			}

			if (data[mipLevel].SysMemPitch == 0)
			{
				break;
			}

			actualMipLevelCount++;
		}

		td.MipLevels = actualMipLevelCount;

		// Create texture object
		VERIFY(SUCCEEDED(dev.CreateTexture2D(&td, data, &m_texture)));

		CreateShaderResourceView();
	}

	void TextureD3D11::LoadRaw(void * data, size_t dataSize)
	{
		// Obtain ID3D11Device object by casting
		ID3D11Device& dev = m_device;

		// Create texture description
		D3D11_TEXTURE2D_DESC td;
		ZeroMemory(&td, sizeof(td));
		td.ArraySize = 1;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.Height = m_header.height;
		td.Width = m_header.width;
		td.MipLevels = 1;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_IMMUTABLE;

		// Prepare usage data
		D3D11_SUBRESOURCE_DATA initialData;
		ZeroMemory(&initialData, sizeof(initialData));
		initialData.pSysMem = data;
		initialData.SysMemPitch = sizeof(uint32) * m_header.width;

		// Create texture object
		VERIFY(SUCCEEDED(dev.CreateTexture2D(&td, &initialData, &m_texture)));

		CreateShaderResourceView();
	}

	uint32 TextureD3D11::GetMemorySize() const
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

	void TextureD3D11::CreateShaderResourceView()
	{
		// Obtain ID3D11Device object by casting
		ID3D11Device& dev = m_device;

		// Create shader resource view description
		D3D11_SHADER_RESOURCE_VIEW_DESC srvd;
		ZeroMemory(&srvd, sizeof(srvd));

		switch (m_header.format)
		{
		case tex::v1_0::RGB:
		case tex::v1_0::RGBA:
			srvd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			break;
		case tex::v1_0::DXT1:
			srvd.Format = DXGI_FORMAT_BC1_UNORM;
			break;
		case tex::v1_0::DXT5:
			srvd.Format = DXGI_FORMAT_BC3_UNORM;
			break;
		default:
			throw std::runtime_error("Unsupported texture format for d3d11 texture!");
			break;
		}

		srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvd.Texture2D.MipLevels = 1;

		// Create shader resource view
		VERIFY(SUCCEEDED(dev.CreateShaderResourceView(m_texture.Get(), &srvd, &m_shaderView)));
	}

	void TextureD3D11::Bind(ShaderType shader, uint32 slot)
	{
		ID3D11DeviceContext& context = m_device;

		ID3D11ShaderResourceView* const views = m_shaderView.Get();
		switch (shader)
		{
		case ShaderType::VertexShader:
			context.VSSetShaderResources(slot, 1, &views);
			break;
		case ShaderType::PixelShader:
			context.PSSetShaderResources(slot, 1, &views);
			break;
		}

		m_device.SetTextureAddressMode(GetTextureAddressModeU(), GetTextureAddressModeV(), GetTextureAddressModeW());
		m_device.SetTextureFilter(GetTextureFilter());
	}
}
