// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "graphics/vertex_shader.h"
#include "graphics_device_null.h"


namespace mmo
{
	/// Direct3D 11 implementation of a vertex shader.
	class VertexShaderNull : public VertexShader
	{
	public:
		explicit VertexShaderNull(GraphicsDeviceNull& InDevice, const void* InShaderCode, size_t InShaderCodeSize);

	public:
		// Begin CGxVertexShader
		virtual void Set() override;
		// End CGxVertexShader

	public:
		GraphicsDeviceNull& Device;
	};
}
