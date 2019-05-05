// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "graphics/pixel_shader.h"
#include "graphics_device_d3d11.h"


namespace mmo
{
	/// Direct3D 11 implementation of a pixel shader.
	class PixelShaderD3D11 : public PixelShader
	{
	public:

		explicit PixelShaderD3D11(GraphicsDeviceD3D11& InDevice, const void* InShaderCode, size_t InShaderCodeSize);

	public:
		// Begin CGxPixelShader
		virtual void Set() override;
		// End CGxPixelShader

	public:

		GraphicsDeviceD3D11& Device;
		ComPtr<ID3D11PixelShader> Shader;
	};

}