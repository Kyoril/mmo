// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics/compute_shader.h"
#include "graphics_device_d3d11.h"

namespace mmo
{
	/// @brief Direct3D 11 implementation of a compute shader.
	class ComputeShaderD3D11 final : public ComputeShader
	{
	public:
		ComputeShaderD3D11(GraphicsDeviceD3D11& device, const void* shaderCode, size_t shaderCodeSize);

	public:
		void Set() override;

	private:
		GraphicsDeviceD3D11& m_device;
		ComPtr<ID3D11ComputeShader> m_shader;
	};
}
