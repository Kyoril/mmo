// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "vertex_shader_d3d11.h"
#include "base/macros.h"


namespace mmo
{
	VertexShaderD3D11::VertexShaderD3D11(GraphicsDeviceD3D11 & InDevice, const void * InShaderCode, size_t InShaderCodeSize)
		: VertexShader()
		, Device(InDevice)
	{
		std::copy_n(
			static_cast<const uint8*>(InShaderCode), 
			InShaderCodeSize, 
			std::back_inserter(m_byteCode));

		ID3D11Device& d3dDevice = Device;
		VERIFY(SUCCEEDED(d3dDevice.CreateVertexShader(InShaderCode, InShaderCodeSize, nullptr, &Shader)));
	}

	void VertexShaderD3D11::Set()
	{
		ID3D11DeviceContext& context = Device;
		context.VSSetShader(Shader.Get(), nullptr, 0);

	}
}
