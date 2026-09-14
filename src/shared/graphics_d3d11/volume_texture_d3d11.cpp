// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "volume_texture_d3d11.h"

#include "base/macros.h"

namespace mmo
{
	namespace
	{
		DXGI_FORMAT toDxgiFormat(const VolumeFormat format)
		{
			switch (format)
			{
			case VolumeFormat::R8:
				return DXGI_FORMAT_R8_UNORM;
			case VolumeFormat::RGBA16F:
				return DXGI_FORMAT_R16G16B16A16_FLOAT;
			}

			return DXGI_FORMAT_UNKNOWN;
		}

		size_t bytesPerTexel(const VolumeFormat format)
		{
			return format == VolumeFormat::R8 ? 1u : 8u;
		}
	}

	VolumeTextureD3D11::VolumeTextureD3D11(GraphicsDeviceD3D11& device, const uint16 width, const uint16 height, const uint16 depth,
		const VolumeFormat format, const bool writable)
		: VolumeTexture(width, height, depth, format, writable)
		, m_device(device)
	{
		ID3D11Device& d3dDevice = m_device;

		D3D11_TEXTURE3D_DESC desc{};
		desc.Width = width;
		desc.Height = height;
		desc.Depth = depth;
		desc.MipLevels = 1;
		desc.Format = toDxgiFormat(format);
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (writable ? D3D11_BIND_UNORDERED_ACCESS : 0u);
		if (FAILED(d3dDevice.CreateTexture3D(&desc, nullptr, &m_texture)))
		{
			// Allocation failure (e.g. exhausted VRAM): leave m_valid false. The caller
			// (GraphicsDeviceD3D11::CreateVolumeTexture) logs and returns nullptr instead of this
			// half-constructed object.
			return;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
		srvDesc.Texture3D.MipLevels = 1;
		if (FAILED(d3dDevice.CreateShaderResourceView(m_texture.Get(), &srvDesc, &m_shaderView)))
		{
			return;
		}

		if (writable)
		{
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = desc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
			uavDesc.Texture3D.MipSlice = 0;
			uavDesc.Texture3D.FirstWSlice = 0;
			uavDesc.Texture3D.WSize = depth;
			if (FAILED(d3dDevice.CreateUnorderedAccessView(m_texture.Get(), &uavDesc, &m_unorderedView)))
			{
				return;
			}
		}

		m_valid = true;
	}

	void VolumeTextureD3D11::Bind(const ShaderType stage, const uint32 slot)
	{
		ID3D11DeviceContext& context = m_device;
		ID3D11ShaderResourceView* const views = m_shaderView.Get();

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
			ASSERT(!"Unsupported shader stage for volume texture binding");
			break;
		}
	}

	void VolumeTextureD3D11::BindWritable(const uint32 slot)
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

	void VolumeTextureD3D11::Upload(const uint8* data, const size_t size)
	{
		ASSERT(data);
		ASSERT(size == static_cast<size_t>(m_width) * m_height * m_depth * bytesPerTexel(m_format));

		ID3D11DeviceContext& context = m_device;
		const UINT rowPitch = static_cast<UINT>(m_width * bytesPerTexel(m_format));
		const UINT slicePitch = rowPitch * m_height;
		context.UpdateSubresource(m_texture.Get(), 0, nullptr, data, rowPitch, slicePitch);
	}
}
