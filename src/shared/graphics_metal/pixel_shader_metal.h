// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "graphics/pixel_shader.h"


namespace mmo
{
	class GraphicsDeviceMetal;

	/// Null implementation of a pixel shader.
	class PixelShaderMetal : public PixelShader
	{
	public:

		explicit PixelShaderMetal(GraphicsDeviceMetal& InDevice, const void* InShaderCode, size_t InShaderCodeSize);

	public:
		// Begin CGxPixelShader
		virtual void Set() override;
		// End CGxPixelShader

	public:
		GraphicsDeviceMetal& Device;
	};

}
