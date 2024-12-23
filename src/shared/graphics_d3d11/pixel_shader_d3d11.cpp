// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "pixel_shader_d3d11.h"
#include "base/macros.h"


namespace mmo
{
	PixelShaderD3D11::PixelShaderD3D11(GraphicsDeviceD3D11 & InDevice, const void * InShaderCode, size_t InShaderCodeSize)
		: PixelShader()
		, Device(InDevice)
	{
		std::copy(
			static_cast<const uint8*>(InShaderCode), 
			static_cast<const uint8*>(InShaderCode) + InShaderCodeSize, 
			std::back_inserter(m_byteCode));

		ID3D11Device& D3DDevice = Device;
		VERIFY(SUCCEEDED(D3DDevice.CreatePixelShader(InShaderCode, InShaderCodeSize, nullptr, &Shader)));
	}

	void PixelShaderD3D11::Set()
	{
		ID3D11DeviceContext& Context = Device;
		Context.PSSetShader(Shader.Get(), nullptr, 0);
	}
}
