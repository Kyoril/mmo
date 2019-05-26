// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "texture_d3d11.h"

#include "base/macros.h"

#include <vector>
#include <stdexcept>


namespace mmo
{
	TextureD3D11::TextureD3D11(GraphicsDeviceD3D11 & device)
		: m_device(device)
	{
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

		// Read first mipmap level into memory
		std::vector<uint8> mipData;
		mipData.resize(m_header.mipmapLengths[0], 0);
		stream->seekg(m_header.mipmapOffsets[0], std::ios::beg);
		stream->read(reinterpret_cast<char*>(mipData.data()), m_header.mipmapLengths[0]);

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
		D3D11_SUBRESOURCE_DATA data;
		ZeroMemory(&data, sizeof(data));
		data.pSysMem = &mipData[0];
		data.SysMemPitch = sizeof(uint32) * m_header.width;

		// Create texture object
		VERIFY(SUCCEEDED(dev.CreateTexture2D(&td, &data, &m_texture)));

		// Create shader resource view description
		D3D11_SHADER_RESOURCE_VIEW_DESC srvd;
		ZeroMemory(&srvd, sizeof(srvd));
		srvd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
	}
}
