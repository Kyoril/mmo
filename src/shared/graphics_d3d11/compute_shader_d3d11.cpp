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
		if (FAILED(d3dDevice.CreateComputeShader(shaderCode, shaderCodeSize, nullptr, &m_shader)))
		{
			// Leave m_shader null: IsValid() reports the failure so GraphicsDeviceD3D11::CreateShader can
			// log it and return nullptr instead of this half-constructed object.
			m_shader.Reset();
		}
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
