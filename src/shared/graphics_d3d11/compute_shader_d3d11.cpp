// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "compute_shader_d3d11.h"

#include "base/macros.h"

#include <algorithm>
#include <iterator>

namespace mmo
{
	ComputeShaderD3D11::ComputeShaderD3D11(GraphicsDeviceD3D11& device, const void* shaderCode, const size_t shaderCodeSize)
		: m_device(device)
	{
		std::copy(static_cast<const uint8*>(shaderCode), static_cast<const uint8*>(shaderCode) + shaderCodeSize, std::back_inserter(m_byteCode));

		ID3D11Device& d3dDevice = m_device;
		VERIFY(SUCCEEDED(d3dDevice.CreateComputeShader(shaderCode, shaderCodeSize, nullptr, &m_shader)));
	}

	void ComputeShaderD3D11::Set()
	{
		if (m_device.m_currentComputeShader == this)
		{
			return;
		}

		ID3D11DeviceContext& context = m_device;
		context.CSSetShader(m_shader.Get(), nullptr, 0);
		m_device.m_currentComputeShader = this;
	}
}
