// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "sampler_state_d3d11.h"

#include "base/macros.h"

namespace mmo
{
	namespace
	{
		D3D11_FILTER toD3dFilter(const SamplerFilter filter)
		{
			switch (filter)
			{
			case SamplerFilter::Point:
				return D3D11_FILTER_MIN_MAG_MIP_POINT;
			case SamplerFilter::Linear:
				return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			case SamplerFilter::Anisotropic:
				return D3D11_FILTER_ANISOTROPIC;
			case SamplerFilter::ComparisonLinear:
				return D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
			case SamplerFilter::ComparisonAnisotropic:
				return D3D11_FILTER_COMPARISON_ANISOTROPIC;
			}

			return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		}

		D3D11_TEXTURE_ADDRESS_MODE toD3dAddress(const SamplerAddress address)
		{
			switch (address)
			{
			case SamplerAddress::Clamp:
				return D3D11_TEXTURE_ADDRESS_CLAMP;
			case SamplerAddress::Wrap:
				return D3D11_TEXTURE_ADDRESS_WRAP;
			case SamplerAddress::Border:
				return D3D11_TEXTURE_ADDRESS_BORDER;
			}

			return D3D11_TEXTURE_ADDRESS_CLAMP;
		}

		D3D11_COMPARISON_FUNC toD3dComparison(const SamplerComparison comparison)
		{
			switch (comparison)
			{
			case SamplerComparison::LessEqual:
				return D3D11_COMPARISON_LESS_EQUAL;
			case SamplerComparison::Less:
				return D3D11_COMPARISON_LESS;
			case SamplerComparison::Always:
				return D3D11_COMPARISON_ALWAYS;
			}

			return D3D11_COMPARISON_LESS_EQUAL;
		}
	}

	SamplerStateD3D11::SamplerStateD3D11(GraphicsDeviceD3D11& device, const SamplerDesc& desc)
		: m_device(device)
	{
		D3D11_SAMPLER_DESC d3dDesc{};
		d3dDesc.Filter = toD3dFilter(desc.filter);
		d3dDesc.AddressU = toD3dAddress(desc.address);
		d3dDesc.AddressV = d3dDesc.AddressU;
		d3dDesc.AddressW = d3dDesc.AddressU;
		d3dDesc.ComparisonFunc = toD3dComparison(desc.comparison);
		for (int i = 0; i < 4; ++i)
		{
			d3dDesc.BorderColor[i] = desc.borderColor[i];
		}
		d3dDesc.MinLOD = 0.0f;
		d3dDesc.MaxLOD = D3D11_FLOAT32_MAX;
		d3dDesc.MaxAnisotropy = desc.maxAnisotropy < 1u ? 1u : desc.maxAnisotropy;
		d3dDesc.MipLODBias = 0.0f;

		ID3D11Device& d3dDevice = m_device;
		VERIFY(SUCCEEDED(d3dDevice.CreateSamplerState(&d3dDesc, &m_state)));
	}

	void SamplerStateD3D11::Bind(const ShaderType stage, const uint32 slot)
	{
		ID3D11DeviceContext& context = m_device;
		ID3D11SamplerState* const states = m_state.Get();

		switch (stage)
		{
		case ShaderType::VertexShader:
			context.VSSetSamplers(slot, 1, &states);
			break;
		case ShaderType::PixelShader:
			context.PSSetSamplers(slot, 1, &states);
			break;
		case ShaderType::ComputeShader:
			context.CSSetSamplers(slot, 1, &states);
			break;
		default:
			ASSERT(!"Unsupported shader stage for sampler binding");
			break;
		}
	}
}
