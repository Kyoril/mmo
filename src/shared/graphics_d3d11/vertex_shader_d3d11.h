// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "graphics/vertex_shader.h"
#include "graphics_device_d3d11.h"


namespace mmo
{
	/// Direct3D 11 implementation of a vertex shader.
	class VertexShaderD3D11 : public VertexShader
	{
	public:

		explicit VertexShaderD3D11(GraphicsDeviceD3D11& InDevice, const void* InShaderCode, size_t InShaderCodeSize);

	public:
		// Begin CGxVertexShader
		virtual void Set() override;
		// End CGxVertexShader

	public:
		GraphicsDeviceD3D11& Device;
		ComPtr<ID3D11VertexShader> Shader;
	};
}
