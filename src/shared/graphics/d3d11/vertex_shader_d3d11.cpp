// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "vertex_shader_d3d11.h"
#include "base/macros.h"


namespace mmo
{
	VertexShaderD3D11::VertexShaderD3D11(GraphicsDeviceD3D11 & InDevice, const void * InShaderCode, size_t InShaderCodeSize)
		: VertexShader()
		, Device(InDevice)
	{
		ID3D11Device& D3DDevice = Device;
		VERIFY(SUCCEEDED(D3DDevice.CreateVertexShader(InShaderCode, InShaderCodeSize, nullptr, &Shader)));
	}

	void VertexShaderD3D11::Set()
	{
		ID3D11DeviceContext& Context = Device;
		Context.VSSetShader(Shader.Get(), nullptr, 0);
	}
}
