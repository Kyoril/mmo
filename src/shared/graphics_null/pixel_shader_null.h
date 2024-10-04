// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "graphics/pixel_shader.h"


namespace mmo
{
	class GraphicsDeviceNull;

	/// Null implementation of a pixel shader.
	class PixelShaderNull : public PixelShader
	{
	public:

		explicit PixelShaderNull(GraphicsDeviceNull& InDevice, const void* InShaderCode, size_t InShaderCodeSize);

	public:
		// Begin CGxPixelShader
		virtual void Set() override;
		// End CGxPixelShader

	public:
		GraphicsDeviceNull& Device;
	};

}