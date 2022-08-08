// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "graphics/vertex_shader.h"
#include "graphics_device_metal.h"


namespace mmo
{
	/// Direct3D 11 implementation of a vertex shader.
	class VertexShaderMetal : public VertexShader
	{
	public:
		explicit VertexShaderMetal(GraphicsDeviceMetal& InDevice, const void* InShaderCode, size_t InShaderCodeSize);

	public:
		// Begin CGxVertexShader
		virtual void Set() override;
		// End CGxVertexShader

	public:
		GraphicsDeviceMetal& Device;
	};
}
