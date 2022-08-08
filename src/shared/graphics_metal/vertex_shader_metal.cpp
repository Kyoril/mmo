// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "vertex_shader_metal.h"
#include "base/macros.h"


namespace mmo
{
    VertexShaderMetal::VertexShaderMetal(GraphicsDeviceMetal & InDevice, const void * InShaderCode, size_t InShaderCodeSize)
		: VertexShader()
		, Device(InDevice)
	{
		std::copy(
			static_cast<const uint8*>(InShaderCode), 
			static_cast<const uint8*>(InShaderCode) + InShaderCodeSize, 
			std::back_inserter(m_byteCode));
	}

	void VertexShaderMetal::Set()
	{

	}
}
