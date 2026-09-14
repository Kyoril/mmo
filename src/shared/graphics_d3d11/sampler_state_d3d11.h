// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics_device_d3d11.h"
#include "graphics/sampler_state.h"

namespace mmo
{
	/// @brief Direct3D 11 sampler state.
	class SamplerStateD3D11 final : public SamplerState
	{
	public:
		SamplerStateD3D11(GraphicsDeviceD3D11& device, const SamplerDesc& desc);
		~SamplerStateD3D11() override = default;

	public:
		void Bind(ShaderType stage, uint32 slot) override;

	private:
		GraphicsDeviceD3D11& m_device;
		ComPtr<ID3D11SamplerState> m_state;
	};
}
