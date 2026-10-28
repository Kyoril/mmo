// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "texture_array_d3d11.h"

#include "base/macros.h"

#include <algorithm>

namespace mmo
{
	namespace
	{
		DXGI_FORMAT toDxgiFormat(const TextureArrayFormat format)
		{
			switch (format)
			{
			case TextureArrayFormat::RG8:
				return DXGI_FORMAT_R8G8_UNORM;
			case TextureArrayFormat::RGBA8:
				return DXGI_FORMAT_R8G8B8A8_UNORM;
			case TextureArrayFormat::R32F:
				return DXGI_FORMAT_R32_FLOAT;
			case TextureArrayFormat::RGBA16F:
				return DXGI_FORMAT_R16G16B16A16_FLOAT;
			case TextureArrayFormat::RGBA32F:
				return DXGI_FORMAT_R32G32B32A32_FLOAT;
			}

			return DXGI_FORMAT_UNKNOWN;
		}

		size_t bytesPerTexel(const TextureArrayFormat format)
		{
			switch (format)
			{
			case TextureArrayFormat::RG8:
				return 2u;
			case TextureArrayFormat::RGBA8:
				return 4u;
			case TextureArrayFormat::R32F:
				return 4u;
			case TextureArrayFormat::RGBA16F:
				return 8u;
			case TextureArrayFormat::RGBA32F:
				return 16u;
			}

			return 0u;
		}

		uint32 mipCount(uint32 width, uint32 height)
		{
			uint32 levels = 1;
			while (width > 1 || height > 1)
			{
				width = std::max(width / 2u, 1u);
				height = std::max(height / 2u, 1u);
				++levels;
			}

			return levels;
		}

		void BindView(GraphicsDeviceD3D11& device, const ShaderType stage, const uint32 slot, ID3D11ShaderResourceView* const views)
		{
			ID3D11DeviceContext& context = device;

			switch (stage)
			{
			case ShaderType::VertexShader:
				context.VSSetShaderResources(slot, 1, &views);
				break;
			case ShaderType::PixelShader:
				context.PSSetShaderResources(slot, 1, &views);
				break;
			case ShaderType::ComputeShader:
				context.CSSetShaderResources(slot, 1, &views);
				break;
			default:
				ASSERT(!"Unsupported shader stage for texture array binding");
				break;
			}
		}
	}

	TextureArrayD3D11::TextureArrayD3D11(GraphicsDeviceD3D11& device, const uint16 width, const uint16 height, const uint16 layers,
		const TextureArrayFormat format, const bool writable, const bool mipmapped)
		: TextureArray(width, height, layers, format, writable, mipmapped)
		, m_device(device)
	{
		ID3D11Device& d3dDevice = m_device;

		m_mipLevels = mipmapped ? mipCount(width, height) : 1u;

		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = m_mipLevels;
		desc.ArraySize = layers;
		desc.Format = toDxgiFormat(format);
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		if (writable)
		{
			desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
		}
		if (mipmapped)
		{
			// GenerateMips renders each mip from the one above, so it needs a render-target binding.
			desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
			desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
		}

		if (FAILED(d3dDevice.CreateTexture2D(&desc, nullptr, &m_texture)))
		{
			return;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		srvDesc.Texture2DArray.MostDetailedMip = 0;
		srvDesc.Texture2DArray.MipLevels = m_mipLevels;
		srvDesc.Texture2DArray.FirstArraySlice = 0;
		srvDesc.Texture2DArray.ArraySize = layers;
		if (FAILED(d3dDevice.CreateShaderResourceView(m_texture.Get(), &srvDesc, &m_shaderView)))
		{
			return;
		}

		if (writable)
		{
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = desc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
			uavDesc.Texture2DArray.MipSlice = 0;
			uavDesc.Texture2DArray.FirstArraySlice = 0;
			uavDesc.Texture2DArray.ArraySize = layers;
			if (FAILED(d3dDevice.CreateUnorderedAccessView(m_texture.Get(), &uavDesc, &m_unorderedView)))
			{
				return;
			}
		}

		m_valid = true;
	}

	void TextureArrayD3D11::Bind(const ShaderType stage, const uint32 slot)
	{
		BindView(m_device, stage, slot, m_shaderView.Get());
	}

	void TextureArrayD3D11::Unbind(const ShaderType stage, const uint32 slot)
	{
		BindView(m_device, stage, slot, nullptr);
	}

	void TextureArrayD3D11::BindWritable(const uint32 slot)
	{
		ASSERT(m_writable);
		if (!m_unorderedView)
		{
			return;
		}

		ID3D11DeviceContext& context = m_device;
		ID3D11UnorderedAccessView* const views = m_unorderedView.Get();
		context.CSSetUnorderedAccessViews(slot, 1, &views, nullptr);
	}

	void TextureArrayD3D11::Upload(const uint32 layer, const uint8* data, const size_t size)
	{
		ASSERT(data);
		ASSERT(layer < m_layers);
		ASSERT(size == static_cast<size_t>(m_width) * m_height * bytesPerTexel(m_format));

		ID3D11DeviceContext& context = m_device;
		const UINT rowPitch = static_cast<UINT>(m_width * bytesPerTexel(m_format));
		const UINT subresource = D3D11CalcSubresource(0, layer, m_mipLevels);
		context.UpdateSubresource(m_texture.Get(), subresource, nullptr, data, rowPitch, rowPitch * m_height);
	}

	void TextureArrayD3D11::GenerateMips()
	{
		if (!m_mipmapped || !m_shaderView)
		{
			return;
		}

		ID3D11DeviceContext& context = m_device;
		context.GenerateMips(m_shaderView.Get());
	}
}
